package youtube

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strings"
	"time"
)

const (
	// defaultAPIBaseURL is the YouTube Data API v3 root. Tests replace it with
	// an httptest server URL.
	defaultAPIBaseURL = "https://www.googleapis.com/youtube/v3"

	// requestTimeout bounds every REST call independently of the stream
	// context, so a stuck connection cannot stall resolution forever.
	requestTimeout = 15 * time.Second

	// maxResponseBytes caps a REST response body; YouTube's JSON for these
	// endpoints is far smaller.
	maxResponseBytes = 1 << 20
)

// restClient performs the Data API calls used to resolve a live video to its
// live chat ID.
type restClient struct {
	http    *http.Client
	baseURL string
	apiKey  string
}

// rest returns a REST client for cfg, defaulting the HTTP client and base URL
// on OfficialProvider.
func (p *OfficialProvider) rest(cfg Config) *restClient {
	client := p.HTTP
	if client == nil {
		client = &http.Client{Timeout: requestTimeout}
	}
	baseURL := strings.TrimRight(p.APIBaseURL, "/")
	if baseURL == "" {
		baseURL = defaultAPIBaseURL
	}
	return &restClient{
		http:    client,
		baseURL: baseURL,
		apiKey:  strings.TrimSpace(cfg.APIKey),
	}
}

// channelListResponse is the subset of channels.list the resolver uses.
type channelListResponse struct {
	Items []struct {
		ID string `json:"id"`
	} `json:"items"`
}

// searchListResponse is the subset of search.list the resolver uses. Both
// video and channel searches place their identifier in id.
type searchListResponse struct {
	Items []struct {
		ID struct {
			VideoID   string `json:"videoId"`
			ChannelID string `json:"channelId"`
		} `json:"id"`
	} `json:"items"`
}

// videoListResponse is the subset of videos.list the resolver uses.
type videoListResponse struct {
	Items []struct {
		ID                   string `json:"id"`
		LiveStreamingDetails struct {
			ActiveLiveChatID string `json:"activeLiveChatId"`
		} `json:"liveStreamingDetails"`
	} `json:"items"`
}

// apiErrorResponse is YouTube's standard error envelope.
type apiErrorResponse struct {
	Error struct {
		Code    int    `json:"code"`
		Message string `json:"message"`
		Status  string `json:"status"`
		Errors  []struct {
			Reason  string `json:"reason"`
			Message string `json:"message"`
		} `json:"errors"`
	} `json:"error"`
}

// resolveLiveChatID turns cfg.Video into a video ID and active live chat ID.
//
// A direct video URL/ID goes straight to videos.list. A channel reference is
// resolved with channels.list (or search.list for legacy custom names), then
// search.list finds the channel's active live video, and videos.list returns
// that video's activeLiveChatId.
func (p *OfficialProvider) resolveLiveChatID(ctx context.Context, cfg Config) (videoID, liveChatID string, err error) {
	if strings.TrimSpace(cfg.APIKey) == "" {
		return "", "", &Error{
			Code:    CodeAuth,
			Message: "an API key is required for the official provider",
			Fatal:   true,
		}
	}

	videoID, channel, err := ParseInput(cfg.Video)
	if err != nil {
		return "", "", &Error{
			Code:    CodeParse,
			Message: redactSecrets(err.Error()),
			Fatal:   true,
		}
	}

	api := p.rest(cfg)
	if videoID == "" {
		channelID, err := api.channelID(ctx, channel)
		if err != nil {
			return "", "", err
		}
		videoID, err = api.liveVideoID(ctx, channelID)
		if err != nil {
			return "", "", err
		}
	}

	liveChatID, err = api.activeLiveChatID(ctx, videoID)
	if err != nil {
		return "", "", err
	}
	return videoID, liveChatID, nil
}

// channelID resolves a channel reference from ParseInput to a channel ID.
// Handles use channels.list?forHandle, legacy user names use forUsername,
// legacy custom names use a channel search, and bare channel IDs pass
// through.
func (a *restClient) channelID(ctx context.Context, ref string) (string, error) {
	var params url.Values
	switch {
	case strings.HasPrefix(ref, "@"):
		params = url.Values{
			"part":      {"id"},
			"forHandle": {ref},
		}
	case strings.HasPrefix(ref, "user/"):
		params = url.Values{
			"part":        {"id"},
			"forUsername": {strings.TrimPrefix(ref, "user/")},
		}
	case strings.HasPrefix(ref, "c/"):
		return a.channelIDByCustomName(ctx, strings.TrimPrefix(ref, "c/"))
	default:
		return ref, nil
	}

	var resp channelListResponse
	if err := a.getJSON(ctx, "/channels", params, &resp); err != nil {
		return "", err
	}
	if len(resp.Items) == 0 || resp.Items[0].ID == "" {
		return "", &Error{
			Code:    CodeParse,
			Message: fmt.Sprintf("no channel found for %q", ref),
			Fatal:   true,
		}
	}
	return resp.Items[0].ID, nil
}

// channelIDByCustomName resolves a legacy /c/<name> reference with a channel
// search, since channels.list has no custom-name parameter.
func (a *restClient) channelIDByCustomName(ctx context.Context, name string) (string, error) {
	params := url.Values{
		"part":       {"snippet"},
		"q":          {name},
		"type":       {"channel"},
		"maxResults": {"1"},
	}
	var resp searchListResponse
	if err := a.getJSON(ctx, "/search", params, &resp); err != nil {
		return "", err
	}
	if len(resp.Items) == 0 || resp.Items[0].ID.ChannelID == "" {
		return "", &Error{
			Code:    CodeParse,
			Message: fmt.Sprintf("no channel found for %q", "c/"+name),
			Fatal:   true,
		}
	}
	return resp.Items[0].ID.ChannelID, nil
}

// liveVideoID returns the first active live video for a channel.
func (a *restClient) liveVideoID(ctx context.Context, channelID string) (string, error) {
	params := url.Values{
		"part":       {"snippet"},
		"channelId":  {channelID},
		"eventType":  {"live"},
		"type":       {"video"},
		"maxResults": {"5"},
	}
	var resp searchListResponse
	if err := a.getJSON(ctx, "/search", params, &resp); err != nil {
		return "", err
	}
	for _, item := range resp.Items {
		if item.ID.VideoID != "" {
			return item.ID.VideoID, nil
		}
	}
	return "", &Error{
		Code:    CodeNotLive,
		Message: "channel has no active live stream",
		Fatal:   true,
	}
}

// activeLiveChatID returns the activeLiveChatId of a video.
func (a *restClient) activeLiveChatID(ctx context.Context, videoID string) (string, error) {
	params := url.Values{
		"part": {"liveStreamingDetails"},
		"id":   {videoID},
	}
	var resp videoListResponse
	if err := a.getJSON(ctx, "/videos", params, &resp); err != nil {
		return "", err
	}
	for _, item := range resp.Items {
		if item.LiveStreamingDetails.ActiveLiveChatID != "" {
			return item.LiveStreamingDetails.ActiveLiveChatID, nil
		}
	}
	return "", &Error{
		Code:    CodeNotLive,
		Message: fmt.Sprintf("video %s is not live or has no active live chat", videoID),
		Fatal:   true,
	}
}

// getJSON performs one GET with the API key appended and decodes the JSON
// body into out. Errors never contain the key.
func (a *restClient) getJSON(ctx context.Context, path string, params url.Values, out any) error {
	body, err := a.get(ctx, path, params)
	if err != nil {
		return err
	}
	if err := json.Unmarshal(body, out); err != nil {
		return &Error{
			Code:    CodeParse,
			Message: "could not parse the YouTube API response",
			Fatal:   true,
		}
	}
	return nil
}

// get performs one GET request and returns the response body. The request
// carries a User-Agent and a 15 second timeout; the API key travels only in
// the query string and is redacted from every error.
func (a *restClient) get(ctx context.Context, path string, params url.Values) ([]byte, error) {
	reqCtx, cancel := context.WithTimeout(ctx, requestTimeout)
	defer cancel()

	query := make(url.Values, len(params)+1)
	for k, v := range params {
		query[k] = v
	}
	query.Set("key", a.apiKey)
	endpoint := a.baseURL + path + "?" + query.Encode()

	req, err := http.NewRequestWithContext(reqCtx, http.MethodGet, endpoint, nil)
	if err != nil {
		return nil, &Error{
			Code:    CodeParse,
			Message: redactSecrets(fmt.Sprintf("could not build the YouTube API request: %v", err)),
			Fatal:   true,
		}
	}
	req.Header.Set("User-Agent", userAgent)
	req.Header.Set("Accept", "application/json")

	resp, err := a.http.Do(req)
	if err != nil {
		if ctx.Err() != nil {
			return nil, ctx.Err()
		}
		return nil, &Error{
			Code:    CodeNetwork,
			Message: redactSecrets(fmt.Sprintf("YouTube API request failed: %v", err)),
			Fatal:   true,
		}
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(io.LimitReader(resp.Body, maxResponseBytes))
	if err != nil {
		return nil, &Error{
			Code:    CodeNetwork,
			Message: redactSecrets(fmt.Sprintf("could not read the YouTube API response: %v", err)),
			Fatal:   true,
		}
	}
	if resp.StatusCode != http.StatusOK {
		return nil, apiError(resp.StatusCode, body)
	}
	return body, nil
}

// apiError maps a non-200 YouTube Data API response to an Error.
func apiError(status int, body []byte) error {
	var envelope apiErrorResponse
	_ = json.Unmarshal(body, &envelope)

	code := ""
	for _, item := range envelope.Error.Errors {
		switch item.Reason {
		case "keyInvalid", "badRequest", "keyExpired", "accessNotConfigured":
			// Authentication problems win over any other reason: retrying
			// with the same key cannot help.
			code = CodeAuth
		case "quotaExceeded", "dailyLimitExceeded", "rateLimitExceeded":
			if code != CodeAuth {
				code = CodeQuota
			}
		case "liveChatDisabled", "liveChatNotFound", "liveChatEnded":
			if code == "" {
				code = CodeChatDisabled
			}
		}
	}

	if code == "" {
		lower := strings.ToLower(envelope.Error.Message)
		switch {
		case status == http.StatusTooManyRequests,
			envelope.Error.Status == "RESOURCE_EXHAUSTED",
			strings.Contains(lower, "quota"):
			code = CodeQuota
		case status == http.StatusBadRequest,
			status == http.StatusUnauthorized,
			status == http.StatusForbidden:
			code = CodeAuth
		default:
			code = CodeNetwork
		}
	}

	return &Error{
		Code:    code,
		Message: apiErrorMessage(envelope, status),
		Fatal:   true,
	}
}

// apiErrorMessage prefers YouTube's own message and falls back to a generic
// description of the HTTP status. The result is truncated and redacted.
func apiErrorMessage(envelope apiErrorResponse, status int) string {
	msg := strings.TrimSpace(envelope.Error.Message)
	if msg == "" {
		for _, item := range envelope.Error.Errors {
			if m := strings.TrimSpace(item.Message); m != "" {
				msg = m
				break
			}
		}
	}
	if msg == "" {
		msg = fmt.Sprintf("YouTube API returned HTTP %d", status)
	}
	if len(msg) > 300 {
		msg = strings.ToValidUTF8(msg[:300], "")
	}
	return redactSecrets(msg)
}
