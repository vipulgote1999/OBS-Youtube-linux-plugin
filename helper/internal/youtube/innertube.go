package youtube

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	stdhtml "html"
	"io"
	"net/http"
	"net/http/cookiejar"
	"net/url"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/tidwall/gjson"

	"obs-youtube-chat-tts/helper/internal/protocol"
)

const (
	// defaultInnerTubeBaseURL is YouTube's public web origin. Tests point
	// BaseURL at an httptest server.
	defaultInnerTubeBaseURL = "https://www.youtube.com"

	// innerTubeTimeout bounds one watch-page or chat request.
	innerTubeTimeout = 30 * time.Second

	// defaultPollInterval is used when YouTube omits timeoutMs.
	defaultPollInterval = 2 * time.Second

	// desktopUserAgent identifies the helper as a desktop Chrome browser. The
	// InnerTube web endpoints return the chat page layout for this agent.
	desktopUserAgent = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 " +
		"(KHTML, like Gecko) Chrome/129.0.0.0 Safari/537.36"

	// acceptLanguage asks YouTube for the English watch page so its
	// continuation titles are stable enough to match "Live chat".
	acceptLanguage = "en-US,en;q=0.9"

	// maxInnerTubeBytes caps a watch page or chat response.
	maxInnerTubeBytes = 8 << 20

	// refreshAfterFailures is how many consecutive retryable request failures
	// trigger a fresh watch page, which supplies a new continuation token.
	refreshAfterFailures = 3
)

// innerTubeBackoff is the retry delay sequence for HTTP 429, 5xx, and
// transport failures. It holds at 60 seconds.
var innerTubeBackoff = []time.Duration{
	5 * time.Second,
	10 * time.Second,
	20 * time.Second,
	40 * time.Second,
	60 * time.Second,
}

// watchPage is the subset of a YouTube watch page the InnerTube provider needs:
// the embedded ytcfg key and client identity plus the live chat continuation.
type watchPage struct {
	APIKey        string
	Context       json.RawMessage
	ClientName    string
	ClientVersion string
	Continuation  string

	// InitialContinuation is the chat renderer's own continuation token
	// (liveChatRenderer.continuations). Production YouTube rejects the
	// sub-menu token when it is sent to get_live_chat, so the poller starts
	// from this token and keeps Continuation as the spec-defined fallback.
	InitialContinuation string
}

// InnerTubeProvider streams live chat through YouTube's private InnerTube web
// API. It needs no API key, but it is experimental: YouTube does not document
// or guarantee these endpoints, so it may break without notice.
//
// Empty fields use production defaults; tests override BaseURL with an
// httptest server.
type InnerTubeProvider struct {
	HTTP    *http.Client
	BaseURL string
}

// Run resolves the configured video or channel, reads the watch page, and polls
// the live chat endpoint until the chat ends or ctx is cancelled.
func (p *InnerTubeProvider) Run(ctx context.Context, cfg Config, emit func(protocol.Event)) error {
	client := p.client()

	videoID, err := p.resolveVideoID(ctx, client, cfg)
	if err != nil {
		return err
	}

	page, err := p.loadWatchPage(ctx, client, videoID, cfg.HL)
	if err != nil {
		return err
	}

	return p.stream(ctx, client, cfg, videoID, page, emit)
}

// baseURL returns the configured origin without a trailing slash.
func (p *InnerTubeProvider) baseURL() string {
	if strings.TrimSpace(p.BaseURL) != "" {
		return strings.TrimRight(p.BaseURL, "/")
	}
	return defaultInnerTubeBaseURL
}

// client returns the provider's HTTP client. The default client keeps cookies
// so the SOCS consent cookie survives the watch-page and chat requests.
func (p *InnerTubeProvider) client() *http.Client {
	if p.HTTP != nil {
		return p.HTTP
	}

	jar, _ := cookiejar.New(nil)
	if u, err := url.Parse(p.baseURL()); err == nil {
		jar.SetCookies(u, []*http.Cookie{{Name: "SOCS", Value: "CAI", Path: "/"}})
	}
	return &http.Client{Timeout: innerTubeTimeout, Jar: jar}
}

// resolveVideoID turns cfg.Video into a video ID. Channel references are
// resolved through the channel's /live page.
func (p *InnerTubeProvider) resolveVideoID(ctx context.Context, client *http.Client, cfg Config) (string, error) {
	videoID, channel, err := ParseInput(cfg.Video)
	if err != nil {
		return "", &Error{
			Code:    CodeParse,
			Message: fmt.Sprintf("invalid YouTube input: %v", err),
			Fatal:   true,
		}
	}
	if videoID != "" {
		return videoID, nil
	}

	body, err := p.getPage(ctx, client, p.channelLiveURL(channel), false)
	if err != nil {
		return "", err
	}
	if id := videoIDFromChannelPage(body); id != "" {
		return id, nil
	}

	return "", &Error{
		Code:    CodeNotLive,
		Message: fmt.Sprintf("channel %q has no active live stream", channel),
		Fatal:   true,
	}
}

// channelLiveURL builds the /live URL for a channel reference from ParseInput:
// an @handle, a legacy c/ or user/ name, or a bare channel ID.
func (p *InnerTubeProvider) channelLiveURL(channel string) string {
	switch {
	case strings.HasPrefix(channel, "@"):
		return p.baseURL() + "/" + channel + "/live"
	case strings.HasPrefix(channel, "c/"), strings.HasPrefix(channel, "user/"):
		return p.baseURL() + "/" + channel + "/live"
	default:
		return p.baseURL() + "/channel/" + channel + "/live"
	}
}

// loadWatchPage fetches and parses a video's watch page. If YouTube serves a
// consent interstitial it retries once with the CONSENT cookie accepted.
func (p *InnerTubeProvider) loadWatchPage(ctx context.Context, client *http.Client, videoID, hl string) (watchPage, error) {
	p.acceptCookies(client, false)

	body, err := p.getPage(ctx, client, p.watchURL(videoID, hl), false)
	if err != nil {
		return watchPage{}, err
	}

	page, parseErr := parseWatchPage(body)
	if parseErr == nil {
		return page, nil
	}
	if !isConsentPage(body) {
		return watchPage{}, parseErr
	}

	p.acceptCookies(client, true)
	body, err = p.getPage(ctx, client, p.watchURL(videoID, hl), true)
	if err != nil {
		return watchPage{}, err
	}
	page, parseErr = parseWatchPage(body)
	if parseErr != nil {
		return watchPage{}, parseErr
	}
	return page, nil
}

// watchURL returns the English watch page for a video.
func (p *InnerTubeProvider) watchURL(videoID, hl string) string {
	if strings.TrimSpace(hl) == "" {
		hl = "en"
	}
	q := url.Values{"v": {videoID}, "hl": {hl}}
	return p.baseURL() + "/watch?" + q.Encode()
}

// acceptCookies seeds the consent cookie in the client's jar when it has one.
// Clients without a jar carry the same cookies in a request header instead.
func (p *InnerTubeProvider) acceptCookies(client *http.Client, consented bool) {
	if client.Jar == nil {
		return
	}
	u, err := url.Parse(p.baseURL())
	if err != nil {
		return
	}
	cookies := []*http.Cookie{{Name: "SOCS", Value: "CAI", Path: "/"}}
	if consented {
		cookies = append(cookies, &http.Cookie{Name: "CONSENT", Value: "YES+", Path: "/"})
	}
	client.Jar.SetCookies(u, cookies)
}

// getPage performs one GET for an HTML page. Errors never include the full
// request URL.
func (p *InnerTubeProvider) getPage(ctx context.Context, client *http.Client, pageURL string, consented bool) ([]byte, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, pageURL, nil)
	if err != nil {
		return nil, &Error{
			Code:    CodeParse,
			Message: fmt.Sprintf("could not build the YouTube page request: %v", err),
			Fatal:   true,
		}
	}
	req.Header.Set("User-Agent", desktopUserAgent)
	req.Header.Set("Accept-Language", acceptLanguage)
	req.Header.Set("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8")
	if client.Jar == nil {
		req.Header.Set("Cookie", consentCookieHeader(consented))
	}

	resp, err := client.Do(req)
	if err != nil {
		if ctx.Err() != nil {
			return nil, ctx.Err()
		}
		return nil, &Error{
			Code:    CodeNetwork,
			Message: redactSecrets(fmt.Sprintf("YouTube page request failed: %v", err)),
			Fatal:   false,
		}
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(io.LimitReader(resp.Body, maxInnerTubeBytes))
	if err != nil {
		if ctx.Err() != nil {
			return nil, ctx.Err()
		}
		return nil, &Error{
			Code:    CodeNetwork,
			Message: redactSecrets(fmt.Sprintf("could not read the YouTube page: %v", err)),
			Fatal:   false,
		}
	}
	if resp.StatusCode != http.StatusOK {
		return nil, pageHTTPError(resp.StatusCode)
	}
	return body, nil
}

// consentCookieHeader returns the Cookie header for clients without a jar.
func consentCookieHeader(consented bool) string {
	if consented {
		return "SOCS=CAI; CONSENT=YES+"
	}
	return "SOCS=CAI"
}

// isConsentPage reports whether body is YouTube's consent interstitial rather
// than a watch page.
func isConsentPage(body []byte) bool {
	return bytes.Contains(body, []byte("consent.youtube.com")) ||
		bytes.Contains(body, []byte("Before you continue to YouTube"))
}

// pageHTTPError maps a non-200 page response to an Error.
func pageHTTPError(status int) error {
	if status == http.StatusTooManyRequests || status >= http.StatusInternalServerError {
		return &Error{
			Code:    CodeNetwork,
			Message: fmt.Sprintf("YouTube returned HTTP %d; retrying", status),
			Fatal:   false,
		}
	}
	return &Error{
		Code:    CodeNetwork,
		Message: fmt.Sprintf("YouTube returned HTTP %d", status),
		Fatal:   true,
	}
}

// parseWatchPage extracts the InnerTube configuration and chat continuation
// from a watch page. A page with no chat renderer is reported as
// CodeChatDisabled rather than as a parse failure.
func parseWatchPage(page []byte) (watchPage, error) {
	text := string(page)

	dataText, _, err := findAssignedJSON(text, "ytInitialData")
	if err != nil {
		return watchPage{}, &Error{
			Code:    CodeParse,
			Message: "could not find ytInitialData in the watch page",
			Fatal:   true,
		}
	}

	cfgText, _, err := findCallObject(text, "ytcfg.set")
	if err != nil {
		return watchPage{}, &Error{
			Code:    CodeParse,
			Message: "could not find ytcfg.set in the watch page",
			Fatal:   true,
		}
	}

	wp := watchPage{
		APIKey:        gjson.Get(cfgText, "INNERTUBE_API_KEY").String(),
		Context:       json.RawMessage(gjson.Get(cfgText, "INNERTUBE_CONTEXT").Raw),
		ClientName:    textValue(gjson.Get(cfgText, "INNERTUBE_CONTEXT_CLIENT_NAME")),
		ClientVersion: gjson.Get(cfgText, "INNERTUBE_CONTEXT_CLIENT_VERSION").String(),
	}
	if wp.APIKey == "" {
		return watchPage{}, &Error{
			Code:    CodeParse,
			Message: "the watch page has no INNERTUBE_API_KEY",
			Fatal:   true,
		}
	}
	if len(wp.Context) == 0 || string(wp.Context) == "null" {
		return watchPage{}, &Error{
			Code:    CodeParse,
			Message: "the watch page has no INNERTUBE_CONTEXT",
			Fatal:   true,
		}
	}

	if wp.ClientName == "" {
		wp.ClientName = textValue(gjson.Get(cfgText, "INNERTUBE_CONTEXT.client.clientName"))
	}
	if wp.ClientName == "" {
		wp.ClientName = "WEB"
	}
	if wp.ClientVersion == "" {
		wp.ClientVersion = gjson.Get(cfgText, "INNERTUBE_CLIENT_VERSION").String()
	}
	if wp.ClientVersion == "" {
		wp.ClientVersion = gjson.Get(cfgText, "INNERTUBE_CONTEXT.client.clientVersion").String()
	}

	wp.Continuation = chatContinuation(dataText)
	if wp.Continuation == "" {
		return watchPage{}, &Error{
			Code:    CodeChatDisabled,
			Message: "live chat is disabled for this video",
			Fatal:   true,
		}
	}
	wp.InitialContinuation = initialChatContinuation(dataText)
	return wp, nil
}

// initialChatContinuation reads the chat renderer's own continuation token.
// The web client sends this token to get_live_chat; the sub-menu tokens only
// seed the /live_chat iframe.
func initialChatContinuation(dataText string) string {
	continuations := gjson.Get(dataText,
		"contents.twoColumnWatchNextResults.conversationBar.liveChatRenderer.continuations")
	token, _ := nextChatContinuation(continuations)
	return token
}

// chatContinuation reads the live chat token from the watch page's chat header.
// It prefers the sub-menu item titled "Live chat", then index 1, then index 0.
func chatContinuation(dataText string) string {
	const menuPath = "contents.twoColumnWatchNextResults.conversationBar.liveChatRenderer." +
		"header.liveChatHeaderRenderer.viewSelector.sortFilterSubMenuRenderer.subMenuItems"

	items := gjson.Get(dataText, menuPath)
	if !items.IsArray() {
		return ""
	}
	arr := items.Array()

	for _, item := range arr {
		if strings.EqualFold(strings.TrimSpace(renderText(item.Get("title"))), "live chat") {
			if token := itemContinuation(item); token != "" {
				return token
			}
		}
	}
	if len(arr) > 1 {
		if token := itemContinuation(arr[1]); token != "" {
			return token
		}
	}
	if len(arr) > 0 {
		return itemContinuation(arr[0])
	}
	return ""
}

// itemContinuation reads the first continuation token a sub-menu item offers.
// Real pages use reloadContinuationData; the other shapes appear in tests and
// in some page variants.
func itemContinuation(item gjson.Result) string {
	for _, path := range []string{
		"continuation.reloadContinuationData.continuation",
		"continuation.invalidationContinuationData.continuation",
		"continuation.timedContinuationData.continuation",
		"continuation.continuation",
	} {
		if token := item.Get(path).String(); token != "" {
			return token
		}
	}
	return ""
}

// findAssignedJSON finds `name = {...}` or `window["name"] = {...}` and returns
// the balanced object. The scanner resumes after the name so unrelated later
// uses of the identifier are not mistaken for the assignment.
func findAssignedJSON(s, name string) (string, int, error) {
	for offset := 0; offset < len(s); {
		i := strings.Index(s[offset:], name)
		if i < 0 {
			return "", 0, errNoJSONObject
		}
		pos := offset + i + len(name)

		j := pos
		for j < len(s) && isAssignmentFiller(s[j]) {
			j++
		}
		if j < len(s) && s[j] == '=' {
			k := j + 1
			for k < len(s) && isSpace(s[k]) {
				k++
			}
			if k < len(s) && s[k] == '{' {
				return extractBalancedJSON(s, k)
			}
		}
		offset = pos
	}
	return "", 0, errNoJSONObject
}

// findCallObject finds `call({...})` and returns the balanced argument object.
func findCallObject(s, call string) (string, int, error) {
	for offset := 0; offset < len(s); {
		i := strings.Index(s[offset:], call)
		if i < 0 {
			return "", 0, errNoJSONObject
		}
		j := offset + i + len(call)
		for j < len(s) && isSpace(s[j]) {
			j++
		}
		if j < len(s) && s[j] == '(' {
			k := j + 1
			for k < len(s) && isSpace(s[k]) {
				k++
			}
			if k < len(s) && s[k] == '{' {
				return extractBalancedJSON(s, k)
			}
		}
		offset = j
	}
	return "", 0, errNoJSONObject
}

// isAssignmentFiller reports whether c can sit between an identifier and its
// assignment operator in the window["ytInitialData"] form.
func isAssignmentFiller(c byte) bool {
	switch c {
	case '"', '\'', ']', ' ', '\t', '\r', '\n':
		return true
	}
	return false
}

// isSpace reports whether c is JSON whitespace.
func isSpace(c byte) bool {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n'
}

// renderText reads a YouTube text object: a runs array, simpleText, or a plain
// string. Emoji runs contribute no text, matching the brief's message rule.
func renderText(r gjson.Result) string {
	if !r.Exists() {
		return ""
	}
	if r.Type == gjson.String {
		return r.String()
	}
	var b strings.Builder
	r.Get("runs").ForEach(func(_, run gjson.Result) bool {
		b.WriteString(run.Get("text").String())
		return true
	})
	if b.Len() > 0 {
		return b.String()
	}
	return r.Get("simpleText").String()
}

// textValue reads a scalar that may be encoded as a string or a number. ytcfg
// stores the client name as a number on real pages and as a string on the
// synthetic fixtures.
func textValue(r gjson.Result) string {
	if !r.Exists() {
		return ""
	}
	if r.Type == gjson.String {
		return r.String()
	}
	return r.Raw
}

// parseChatResponse maps one get_live_chat response. It returns the next
// continuation token and the suggested wait, or an Error with CodeEnded when
// YouTube supplies no continuation.
func parseChatResponse(body []byte) ([]protocol.ChatMessage, string, time.Duration, error) {
	if err := chatAPIError(body); err != nil {
		return nil, "", 0, err
	}

	chat := gjson.GetBytes(body, "continuationContents.liveChatContinuation")
	if !chat.Exists() {
		return nil, "", 0, endedError()
	}

	messages := make([]protocol.ChatMessage, 0, 8)
	chat.Get("actions").ForEach(func(_, action gjson.Result) bool {
		if msg, ok := mapInnerTubeAction(action); ok {
			messages = append(messages, msg)
		}
		return true
	})

	continuation, wait := nextChatContinuation(chat.Get("continuations"))
	if continuation == "" {
		return messages, "", 0, endedError()
	}
	return messages, continuation, wait, nil
}

// endedError is the normal end-of-chat signal. Fatal is false because the
// stream finished, not because it failed.
func endedError() *Error {
	return &Error{Code: CodeEnded, Message: "live chat ended", Fatal: false}
}

// chatAPIError maps a JSON error envelope to an Error. Server errors are
// retryable; client errors are fatal.
func chatAPIError(body []byte) error {
	envelope := gjson.GetBytes(body, "error")
	if !envelope.Exists() || !envelope.IsObject() {
		return nil
	}
	code := envelope.Get("code").Int()
	msg := strings.TrimSpace(envelope.Get("message").String())
	if msg == "" {
		msg = fmt.Sprintf("YouTube chat returned error %d", code)
	}
	if len(msg) > 300 {
		msg = strings.ToValidUTF8(msg[:300], "")
	}
	msg = redactSecrets(msg)

	if code == http.StatusTooManyRequests || code >= http.StatusInternalServerError {
		return &Error{Code: CodeNetwork, Message: msg, Fatal: false}
	}
	return &Error{Code: CodeParse, Message: msg, Fatal: true}
}

// mapInnerTubeAction converts one addChatItemAction. Viewer-engagement,
// mode-change, placeholder, and ticker actions are ignored.
func mapInnerTubeAction(action gjson.Result) (protocol.ChatMessage, bool) {
	item := action.Get("addChatItemAction.item")
	if !item.Exists() {
		return protocol.ChatMessage{}, false
	}

	switch {
	case item.Get("liveChatTextMessageRenderer").Exists():
		return mapInnerTubeMessage(item.Get("liveChatTextMessageRenderer"), "text"), true
	case item.Get("liveChatPaidMessageRenderer").Exists():
		return mapInnerTubeMessage(item.Get("liveChatPaidMessageRenderer"), "superchat"), true
	case item.Get("liveChatMembershipItemRenderer").Exists():
		return mapInnerTubeMessage(item.Get("liveChatMembershipItemRenderer"), "membership"), true
	case item.Get("liveChatPaidStickerRenderer").Exists():
		return mapInnerTubeMessage(item.Get("liveChatPaidStickerRenderer"), "superchat"), true
	}
	return protocol.ChatMessage{}, false
}

// mapInnerTubeMessage converts one chat renderer to the wire message shape.
func mapInnerTubeMessage(r gjson.Result, kind string) protocol.ChatMessage {
	msg := protocol.ChatMessage{
		ID:          r.Get("id").String(),
		Author:      renderText(r.Get("authorName")),
		AuthorID:    r.Get("authorChannelId.value").String(),
		Text:        renderText(r.Get("message")),
		PublishedAt: timestampUsec(r.Get("timestampUsec").String()),
		Kind:        kind,
	}
	if msg.AuthorID == "" {
		// Current text-message renderers omit authorChannelId and carry the
		// channel in authorExternalChannelId instead.
		msg.AuthorID = r.Get("authorExternalChannelId").String()
	}

	switch kind {
	case "superchat":
		msg.Amount = r.Get("purchaseAmountText.simpleText").String()
	case "membership":
		msg.IsSponsor = true
		// Membership renderers often carry their announcement in
		// headerSubtext rather than message.
		if msg.Text == "" {
			msg.Text = renderText(r.Get("headerSubtext"))
		}
	}

	r.Get("authorBadges").ForEach(func(_, badge gjson.Result) bool {
		switch strings.ToLower(badge.Get("liveChatAuthorBadgeRenderer.tooltip").String()) {
		case "moderator":
			msg.IsModerator = true
		case "owner":
			msg.IsOwner = true
		case "member":
			msg.IsSponsor = true
		}
		return true
	})

	return msg
}

// timestampUsec converts YouTube's microsecond timestamp to RFC 3339 UTC.
func timestampUsec(raw string) string {
	usec, err := strconv.ParseInt(strings.TrimSpace(raw), 10, 64)
	if err != nil || usec <= 0 {
		return ""
	}
	return time.UnixMicro(usec).UTC().Format(time.RFC3339Nano)
}

// nextChatContinuation returns the first chat continuation token and its
// suggested wait. Seek and reload continuations are not message sources but
// their tokens are still usable for the next live chat request.
func nextChatContinuation(continuations gjson.Result) (string, time.Duration) {
	if !continuations.IsArray() {
		return "", 0
	}

	var token string
	var wait time.Duration
	continuations.ForEach(func(_, entry gjson.Result) bool {
		for _, key := range []string{
			"invalidationContinuationData",
			"timedContinuationData",
			"reloadContinuationData",
			"liveChatReplayContinuationData",
		} {
			data := entry.Get(key)
			if !data.Exists() {
				continue
			}
			if next := data.Get("continuation").String(); next != "" {
				token = next
				if ms := data.Get("timeoutMs").Int(); ms > 0 {
					wait = time.Duration(ms) * time.Millisecond
				}
				return false
			}
		}
		return true
	})
	return token, wait
}

// stream polls the chat endpoint, retrying with backoff and refreshing the
// watch page after repeated failures. connected is emitted only after the first
// successful chat response.
func (p *InnerTubeProvider) stream(
	ctx context.Context,
	client *http.Client,
	cfg Config,
	videoID string,
	page watchPage,
	emit func(protocol.Event),
) error {
	continuation := page.InitialContinuation
	if continuation == "" {
		continuation = page.Continuation
	}
	failures := 0
	connected := false

	for {
		if ctx.Err() != nil {
			return nil
		}

		messages, next, wait, err := p.poll(ctx, client, page, videoID, continuation)
		if err != nil {
			if ctx.Err() != nil {
				return nil
			}

			var yerr *Error
			if errors.As(err, &yerr) && yerr.Code == CodeEnded {
				for _, msg := range messages {
					emit(protocol.Event{Type: "message", Message: &msg})
				}
				emit(protocol.Event{Type: "status", State: "ended"})
				return nil
			}
			if !retryable(yerr) {
				return err
			}

			delay := innerTubeBackoff[len(innerTubeBackoff)-1]
			if failures < len(innerTubeBackoff) {
				delay = innerTubeBackoff[failures]
			}
			failures++
			emit(protocol.Event{
				Type:   "status",
				State:  "reconnecting",
				Detail: fmt.Sprintf("innertube request failed; retrying in %s", delay),
			})
			if !sleep(ctx, delay) {
				return nil
			}

			if failures%refreshAfterFailures == 0 {
				refreshed, refreshErr := p.loadWatchPage(ctx, client, videoID, cfg.HL)
				if refreshErr != nil {
					if ctx.Err() != nil {
						return nil
					}
					var refreshYErr *Error
					if !errors.As(refreshErr, &refreshYErr) || !retryable(refreshYErr) {
						return refreshErr
					}
					continue
				}
				page = refreshed
				continuation = refreshed.InitialContinuation
				if continuation == "" {
					continuation = refreshed.Continuation
				}
				failures = 0
				emit(protocol.Event{
					Type:   "status",
					State:  "reconnecting",
					Detail: "innertube chat session refreshed",
				})
			}
			continue
		}

		failures = 0

		if !connected {
			connected = true
			emit(protocol.Event{Type: "status", State: "connected", Detail: "innertube (experimental)"})
		}
		for _, msg := range messages {
			emit(protocol.Event{Type: "message", Message: &msg})
		}

		if next == "" {
			emit(protocol.Event{Type: "status", State: "ended"})
			return nil
		}
		continuation = next

		if wait <= 0 {
			wait = defaultPollInterval
		}
		if !sleep(ctx, wait) {
			return nil
		}
	}
}

// poll performs one chat request and parses the response.
func (p *InnerTubeProvider) poll(
	ctx context.Context,
	client *http.Client,
	page watchPage,
	videoID, continuation string,
) ([]protocol.ChatMessage, string, time.Duration, error) {
	body, err := p.postChat(ctx, client, page, videoID, continuation)
	if err != nil {
		return nil, "", 0, err
	}
	return parseChatResponse(body)
}

// postChat sends one get_live_chat request. The request URL carries only the
// public INNERTUBE_API_KEY; it is never included in an error message.
func (p *InnerTubeProvider) postChat(
	ctx context.Context,
	client *http.Client,
	page watchPage,
	videoID, continuation string,
) ([]byte, error) {
	payload, err := json.Marshal(struct {
		Context      json.RawMessage `json:"context"`
		Continuation string          `json:"continuation"`
	}{
		Context:      page.Context,
		Continuation: continuation,
	})
	if err != nil {
		return nil, &Error{
			Code:    CodeParse,
			Message: "could not build the YouTube chat request",
			Fatal:   true,
		}
	}

	endpoint := p.baseURL() + "/youtubei/v1/live_chat/get_live_chat?key=" + url.QueryEscape(page.APIKey)
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, endpoint, bytes.NewReader(payload))
	if err != nil {
		return nil, &Error{
			Code:    CodeParse,
			Message: redactSecrets(fmt.Sprintf("could not build the YouTube chat request: %v", err)),
			Fatal:   true,
		}
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Accept", "application/json")
	req.Header.Set("User-Agent", desktopUserAgent)
	req.Header.Set("Origin", defaultInnerTubeBaseURL)
	req.Header.Set("Referer", defaultInnerTubeBaseURL+"/watch?v="+videoID)
	req.Header.Set("X-Youtube-Client-Name", page.ClientName)
	req.Header.Set("X-Youtube-Client-Version", page.ClientVersion)

	resp, err := client.Do(req)
	if err != nil {
		if ctx.Err() != nil {
			return nil, ctx.Err()
		}
		return nil, &Error{
			Code:    CodeNetwork,
			Message: redactSecrets(fmt.Sprintf("YouTube chat request failed: %v", err)),
			Fatal:   false,
		}
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(io.LimitReader(resp.Body, maxInnerTubeBytes))
	if err != nil {
		if ctx.Err() != nil {
			return nil, ctx.Err()
		}
		return nil, &Error{
			Code:    CodeNetwork,
			Message: redactSecrets(fmt.Sprintf("could not read the YouTube chat response: %v", err)),
			Fatal:   false,
		}
	}
	if resp.StatusCode != http.StatusOK {
		return nil, chatHTTPError(resp.StatusCode, body)
	}
	return body, nil
}

// chatHTTPError maps a non-200 chat response to an Error. Rate limits and
// server errors are retryable; an expired chat is ended; other client errors
// are fatal.
func chatHTTPError(status int, body []byte) error {
	if status == http.StatusTooManyRequests || status >= http.StatusInternalServerError {
		return &Error{
			Code:    CodeNetwork,
			Message: fmt.Sprintf("YouTube chat returned HTTP %d; retrying", status),
			Fatal:   false,
		}
	}
	switch status {
	case http.StatusUnauthorized, http.StatusForbidden:
		return &Error{
			Code:    CodeAuth,
			Message: fmt.Sprintf("YouTube chat returned HTTP %d", status),
			Fatal:   true,
		}
	case http.StatusNotFound, http.StatusGone:
		return endedError()
	}
	if err := chatAPIError(body); err != nil {
		return err
	}
	return &Error{
		Code:    CodeParse,
		Message: fmt.Sprintf("YouTube chat returned HTTP %d", status),
		Fatal:   true,
	}
}

// retryable reports whether an error is a temporary failure worth backing off
// and retrying.
func retryable(err *Error) bool {
	return err != nil && err.Code == CodeNetwork && !err.Fatal
}

// sleep waits for d or until ctx is cancelled. It reports whether the wait
// completed.
func sleep(ctx context.Context, d time.Duration) bool {
	timer := time.NewTimer(d)
	defer timer.Stop()
	select {
	case <-ctx.Done():
		return false
	case <-timer.C:
		return true
	}
}

// videoIDFromChannelPage extracts a live video ID from a channel /live page.
// The canonical watch link is authoritative; the page's own ytInitialData and
// og:url are the next best sources. A bare "videoId" match is used only when
// the page also advertises a live broadcast, so recommended videos cannot be
// mistaken for the channel's stream.
func videoIDFromChannelPage(body []byte) string {
	for _, tag := range linkTagRE.FindAll(body, -1) {
		attrs := htmlAttrs(tag)
		if !strings.EqualFold(attrs["rel"], "canonical") {
			continue
		}
		if id := watchURLVideoID(attrs["href"]); id != "" {
			return id
		}
	}
	for _, tag := range metaTagRE.FindAll(body, -1) {
		attrs := htmlAttrs(tag)
		if !strings.EqualFold(attrs["property"], "og:url") {
			continue
		}
		if id := watchURLVideoID(attrs["content"]); id != "" {
			return id
		}
	}

	if dataText, _, err := findAssignedJSON(string(body), "ytInitialData"); err == nil {
		for _, path := range []string{
			"currentVideoEndpoint.watchEndpoint.videoId",
			"contents.twoColumnWatchNextResults.results.results.contents.0.videoRenderer.videoId",
		} {
			if id := gjson.Get(dataText, path).String(); isVideoID(id) {
				return id
			}
		}
	}

	if bytes.Contains(body, []byte(`"isLive":true`)) || bytes.Contains(body, []byte(`"isLiveNow":true`)) {
		if m := rawVideoIDRE.FindSubmatch(body); len(m) == 2 && isVideoID(string(m[1])) {
			return string(m[1])
		}
	}
	return ""
}

// watchURLVideoID extracts a video ID from an absolute YouTube watch or /live
// URL.
func watchURLVideoID(raw string) string {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return ""
	}
	u, err := url.Parse(raw)
	if err != nil || u.Host == "" || !isYouTubeHost(u.Hostname()) {
		return ""
	}
	if v := u.Query().Get("v"); isVideoID(v) {
		return v
	}
	segs := pathSegments(u.Path)
	if len(segs) == 2 && segs[0] == "live" && isVideoID(segs[1]) {
		return segs[1]
	}
	return ""
}

var (
	linkTagRE    = regexp.MustCompile(`(?is)<link\b[^>]*>`)
	metaTagRE    = regexp.MustCompile(`(?is)<meta\b[^>]*>`)
	htmlAttrRE   = regexp.MustCompile(`(?is)([a-zA-Z_:][-a-zA-Z0-9_:.]*)\s*=\s*["']([^"']*)["']`)
	rawVideoIDRE = regexp.MustCompile(`"videoId"\s*:\s*"([A-Za-z0-9_-]{11})"`)
)

// htmlAttrs parses the attributes of one HTML start tag into a lowercase map.
func htmlAttrs(tag []byte) map[string]string {
	attrs := make(map[string]string)
	for _, m := range htmlAttrRE.FindAllSubmatch(tag, -1) {
		attrs[strings.ToLower(string(m[1]))] = stdhtml.UnescapeString(string(m[2]))
	}
	return attrs
}
