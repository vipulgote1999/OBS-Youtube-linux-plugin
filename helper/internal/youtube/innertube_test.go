package youtube

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/tidwall/gjson"

	"obs-youtube-chat-tts/helper/internal/protocol"
)

// readFixture loads one testdata file.
func readFixture(t *testing.T, name string) []byte {
	t.Helper()
	body, err := os.ReadFile("testdata/" + name)
	if err != nil {
		t.Fatalf("read testdata/%s: %v", name, err)
	}
	return body
}

// TestParseWatchPage parses the synthetic watch page: the InnerTube key comes
// from ytcfg.set and the "Live chat" sub-menu item supplies the continuation
// rather than the "Top chat" item at index 0.
func TestParseWatchPage(t *testing.T) {
	page, err := parseWatchPage(readFixture(t, "watch_page.html"))
	if err != nil {
		t.Fatalf("parseWatchPage() error = %v", err)
	}
	if page.APIKey != "test-key" {
		t.Errorf("APIKey = %q, want test-key", page.APIKey)
	}
	if page.ClientName != "WEB" {
		t.Errorf("ClientName = %q, want WEB", page.ClientName)
	}
	if page.ClientVersion != "2.20261021.00.00" {
		t.Errorf("ClientVersion = %q, want 2.20261021.00.00", page.ClientVersion)
	}
	if page.Continuation != "LIVE_TOKEN" {
		t.Errorf("Continuation = %q, want LIVE_TOKEN", page.Continuation)
	}
	if page.InitialContinuation != "INITIAL_TOKEN" {
		t.Errorf("InitialContinuation = %q, want INITIAL_TOKEN", page.InitialContinuation)
	}
	if got := gjson.GetBytes(page.Context, "client.clientName").String(); got != "WEB" {
		t.Errorf("context client name = %q, want WEB", got)
	}
}

// TestWatchPageNoChat verifies that a page without a live chat renderer maps to
// CodeChatDisabled instead of a parse failure or an empty stream.
func TestWatchPageNoChat(t *testing.T) {
	_, err := parseWatchPage(readFixture(t, "watch_page_no_chat.html"))
	if err == nil {
		t.Fatal("parseWatchPage() error = nil, want a chat_disabled error")
	}
	var yerr *Error
	if !errors.As(err, &yerr) {
		t.Fatalf("error = %v (%T), want *Error", err, err)
	}
	if yerr.Code != CodeChatDisabled {
		t.Errorf("code = %q, want %q", yerr.Code, CodeChatDisabled)
	}
	if !yerr.Fatal {
		t.Error("Fatal = false, want true")
	}
}

// TestParseWatchPageWindowAssignment covers the window["ytInitialData"] form
// YouTube also emits.
func TestParseWatchPageWindowAssignment(t *testing.T) {
	html := `<html><script>
window["ytInitialData"] = {"contents":{"twoColumnWatchNextResults":{"conversationBar":{"liveChatRenderer":{"header":{"liveChatHeaderRenderer":{"viewSelector":{"sortFilterSubMenuRenderer":{"subMenuItems":[{"title":"Top chat"},{"title":"Live chat","continuation":{"reloadContinuationData":{"continuation":"WINDOW_TOKEN"}}}]}}}}}}}}};
</script><script>
ytcfg.set({"INNERTUBE_API_KEY":"window-key","INNERTUBE_CONTEXT":{"client":{"clientName":"WEB","clientVersion":"2.1"}}});
</script></html>`

	page, err := parseWatchPage([]byte(html))
	if err != nil {
		t.Fatalf("parseWatchPage() error = %v", err)
	}
	if page.APIKey != "window-key" {
		t.Errorf("APIKey = %q, want window-key", page.APIKey)
	}
	if page.Continuation != "WINDOW_TOKEN" {
		t.Errorf("Continuation = %q, want WINDOW_TOKEN", page.Continuation)
	}
}

// TestParseChatResponse maps text, Super Chat, and membership actions, ignores
// viewer-engagement actions, and reads the next invalidation continuation.
func TestParseChatResponse(t *testing.T) {
	messages, next, wait, err := parseChatResponse(readFixture(t, "chat_response.json"))
	if err != nil {
		t.Fatalf("parseChatResponse() error = %v", err)
	}
	if len(messages) != 3 {
		t.Fatalf("messages = %d, want 3: %#v", len(messages), messages)
	}
	if next != "NEXT" {
		t.Errorf("continuation = %q, want NEXT", next)
	}
	if wait != 10*time.Millisecond {
		t.Errorf("wait = %s, want 10ms", wait)
	}

	text := messages[0]
	if text.ID != "msg-text-1" || text.Author != "Alice" || text.AuthorID != "UC-alice" {
		t.Errorf("text identity = %#v, want msg-text-1/Alice/UC-alice", text)
	}
	if text.Text != "hello chat" {
		t.Errorf("text = %q, want %q", text.Text, "hello chat")
	}
	if text.Kind != "text" {
		t.Errorf("kind = %q, want text", text.Kind)
	}
	if text.IsModerator || text.IsOwner || text.IsSponsor {
		t.Errorf("text flags = moderator:%v owner:%v sponsor:%v, want all false",
			text.IsModerator, text.IsOwner, text.IsSponsor)
	}
	if text.PublishedAt != "2024-10-21T08:40:00Z" {
		t.Errorf("published_at = %q, want 2024-10-21T08:40:00Z", text.PublishedAt)
	}

	super := messages[1]
	if super.Author != "Bob" || super.Text != "great stream" {
		t.Errorf("super chat = %#v, want Bob/great stream", super)
	}
	if super.Kind != "superchat" {
		t.Errorf("super chat kind = %q, want superchat", super.Kind)
	}
	if super.Amount != "$5.00" {
		t.Errorf("super chat amount = %q, want $5.00", super.Amount)
	}
	if !super.IsSponsor {
		t.Error("super chat IsSponsor = false, want true from the Member badge")
	}

	member := messages[2]
	if member.Author != "Carol" || member.Text != "joined the channel" {
		t.Errorf("membership = %#v, want Carol/joined the channel", member)
	}
	if member.Kind != "membership" {
		t.Errorf("membership kind = %q, want membership", member.Kind)
	}
	if !member.IsSponsor {
		t.Error("membership IsSponsor = false, want true")
	}
	if !member.IsModerator {
		t.Error("membership IsModerator = false, want true from the Moderator badge")
	}
}

// TestParseChatResponseEnded verifies that a response without a continuation
// reports CodeEnded so the poller can emit the ended status.
func TestParseChatResponseEnded(t *testing.T) {
	_, _, _, err := parseChatResponse(readFixture(t, "chat_response_ended.json"))
	if err == nil {
		t.Fatal("parseChatResponse() error = nil, want ended")
	}
	var yerr *Error
	if !errors.As(err, &yerr) {
		t.Fatalf("error = %v (%T), want *Error", err, err)
	}
	if yerr.Code != CodeEnded {
		t.Errorf("code = %q, want %q", yerr.Code, CodeEnded)
	}
}

// TestParseChatResponsePaidSticker treats a Super Sticker as a Super Chat and
// reads its rendered purchase amount.
func TestParseChatResponsePaidSticker(t *testing.T) {
	body := []byte(`{"continuationContents":{"liveChatContinuation":{
		"actions":[{"addChatItemAction":{"item":{"liveChatPaidStickerRenderer":{
			"id":"sticker-1",
			"timestampUsec":"1729500000000000",
			"authorName":{"simpleText":"Dana"},
			"authorChannelId":{"value":"UC-dana"},
			"purchaseAmountText":{"simpleText":"$2.00"}
		}}}}],
		"continuations":[{"timedContinuationData":{"continuation":"STICKER_NEXT","timeoutMs":1500}}]
	}}}`)

	messages, next, wait, err := parseChatResponse(body)
	if err != nil {
		t.Fatalf("parseChatResponse() error = %v", err)
	}
	if len(messages) != 1 {
		t.Fatalf("messages = %d, want 1", len(messages))
	}
	if messages[0].Kind != "superchat" || messages[0].Amount != "$2.00" {
		t.Errorf("sticker = %#v, want superchat with $2.00", messages[0])
	}
	if next != "STICKER_NEXT" {
		t.Errorf("continuation = %q, want STICKER_NEXT", next)
	}
	if wait != 1500*time.Millisecond {
		t.Errorf("wait = %s, want 1.5s", wait)
	}
}

// TestParseChatResponseNoChatContinuation verifies that a body without
// continuationContents is treated as the end of chat.
func TestParseChatResponseNoChatContinuation(t *testing.T) {
	_, _, _, err := parseChatResponse([]byte(`{"responseContext":{}}`))
	var yerr *Error
	if !errors.As(err, &yerr) || yerr.Code != CodeEnded {
		t.Fatalf("error = %v, want code %q", err, CodeEnded)
	}
}

// TestRunInnerTube drives the poller against an httptest YouTube: the watch
// page supplies the key and continuation, the first chat response supplies
// three messages and NEXT, and the second ends the chat.
func TestRunInnerTube(t *testing.T) {
	var mu sync.Mutex
	var chatCalls []string

	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/watch":
			if got := r.URL.Query().Get("v"); got != "dQw4w9WgXcQ" {
				t.Errorf("watch v = %q, want dQw4w9WgXcQ", got)
			}
			if got := r.URL.Query().Get("hl"); got != "en" {
				t.Errorf("watch hl = %q, want en", got)
			}
			if ua := r.Header.Get("User-Agent"); !strings.Contains(ua, "Chrome") {
				t.Errorf("User-Agent = %q, want a desktop Chrome agent", ua)
			}
			if al := r.Header.Get("Accept-Language"); al != "en-US,en;q=0.9" {
				t.Errorf("Accept-Language = %q, want en-US,en;q=0.9", al)
			}
			w.Header().Set("Content-Type", "text/html; charset=utf-8")
			w.Write(readFixture(t, "watch_page.html"))

		case "/youtubei/v1/live_chat/get_live_chat":
			if r.Method != http.MethodPost {
				t.Errorf("chat method = %s, want POST", r.Method)
			}
			if got := r.URL.Query().Get("key"); got != "test-key" {
				t.Errorf("chat key = %q, want test-key", got)
			}
			if got := r.Header.Get("Content-Type"); got != "application/json" {
				t.Errorf("Content-Type = %q, want application/json", got)
			}
			if got := r.Header.Get("Origin"); got != "https://www.youtube.com" {
				t.Errorf("Origin = %q, want https://www.youtube.com", got)
			}
			if got := r.Header.Get("Referer"); got != "https://www.youtube.com/watch?v=dQw4w9WgXcQ" {
				t.Errorf("Referer = %q, want the watch page", got)
			}
			if got := r.Header.Get("X-Youtube-Client-Name"); got != "WEB" {
				t.Errorf("X-Youtube-Client-Name = %q, want WEB", got)
			}
			if got := r.Header.Get("X-Youtube-Client-Version"); got != "2.20261021.00.00" {
				t.Errorf("X-Youtube-Client-Version = %q, want 2.20261021.00.00", got)
			}

			var req struct {
				Context      json.RawMessage `json:"context"`
				Continuation string          `json:"continuation"`
			}
			body, _ := io.ReadAll(r.Body)
			if err := json.Unmarshal(body, &req); err != nil {
				t.Errorf("request body is not JSON: %v", err)
			}
			if got := gjson.GetBytes(req.Context, "client.clientName").String(); got != "WEB" {
				t.Errorf("request context client = %q, want WEB", got)
			}

			mu.Lock()
			chatCalls = append(chatCalls, req.Continuation)
			call := len(chatCalls)
			mu.Unlock()

			w.Header().Set("Content-Type", "application/json")
			switch call {
			case 1:
				w.Write(readFixture(t, "chat_response.json"))
			default:
				w.Write(readFixture(t, "chat_response_ended.json"))
			}

		default:
			t.Errorf("unexpected request path %q", r.URL.Path)
			http.NotFound(w, r)
		}
	}))
	defer srv.Close()

	p := &InnerTubeProvider{HTTP: srv.Client(), BaseURL: srv.URL}

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	var events []protocol.Event
	err := p.Run(ctx, Config{Provider: "innertube", Video: "dQw4w9WgXcQ", HL: "en"},
		func(e protocol.Event) { events = append(events, e) })
	if err != nil {
		t.Fatalf("Run() error = %v", err)
	}

	mu.Lock()
	if len(chatCalls) != 2 {
		t.Errorf("chat calls = %d, want 2", len(chatCalls))
	}
	if len(chatCalls) == 2 && (chatCalls[0] != "INITIAL_TOKEN" || chatCalls[1] != "NEXT") {
		t.Errorf("chat continuations = %q, want [INITIAL_TOKEN NEXT]", chatCalls)
	}
	mu.Unlock()

	var connected, ended, messages int
	for _, e := range events {
		switch {
		case e.Type == "status" && e.State == "connected":
			connected++
			if !strings.Contains(e.Detail, "experimental") {
				t.Errorf("connected detail = %q, want an experimental marker", e.Detail)
			}
		case e.Type == "status" && e.State == "ended":
			ended++
		case e.Type == "message":
			messages++
		case e.Type == "error":
			t.Errorf("unexpected error event: %#v", e)
		}
	}
	if connected != 1 {
		t.Errorf("connected events = %d, want 1", connected)
	}
	if ended != 1 {
		t.Errorf("ended events = %d, want 1", ended)
	}
	if messages != 3 {
		t.Errorf("message events = %d, want 3", messages)
	}
}

// TestRunInnerTubeChatDisabled verifies that a watch page without chat fails
// before any chat request is made.
func TestRunInnerTubeChatDisabled(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/watch" {
			t.Errorf("unexpected path %q; chat must not be requested", r.URL.Path)
		}
		w.Write(readFixture(t, "watch_page_no_chat.html"))
	}))
	defer srv.Close()

	p := &InnerTubeProvider{HTTP: srv.Client(), BaseURL: srv.URL}
	err := p.Run(context.Background(), Config{Video: "dQw4w9WgXcQ"}, func(protocol.Event) {})
	var yerr *Error
	if !errors.As(err, &yerr) || yerr.Code != CodeChatDisabled {
		t.Fatalf("Run() error = %v, want code %q", err, CodeChatDisabled)
	}
}

// TestResolveChannelLive follows a channel's /live page and reads the canonical
// watch link.
func TestResolveChannelLive(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/@SomeChannel/live" {
			t.Errorf("path = %q, want /@SomeChannel/live", r.URL.Path)
		}
		io.WriteString(w, `<html><head><link rel="canonical" href="https://www.youtube.com/watch?v=dQw4w9WgXcQ"></head></html>`)
	}))
	defer srv.Close()

	p := &InnerTubeProvider{HTTP: srv.Client(), BaseURL: srv.URL}
	client := p.client()
	videoID, err := p.resolveVideoID(context.Background(), client, Config{Video: "@SomeChannel"})
	if err != nil {
		t.Fatalf("resolveVideoID() error = %v", err)
	}
	if videoID != "dQw4w9WgXcQ" {
		t.Errorf("videoID = %q, want dQw4w9WgXcQ", videoID)
	}
}

// TestResolveChannelNotLive verifies that a channel /live page without a video
// maps to CodeNotLive.
func TestResolveChannelNotLive(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		io.WriteString(w, `<html><head><link rel="canonical" href="https://www.youtube.com/@SomeChannel"></head></html>`)
	}))
	defer srv.Close()

	p := &InnerTubeProvider{HTTP: srv.Client(), BaseURL: srv.URL}
	_, err := p.resolveVideoID(context.Background(), p.client(), Config{Video: "@SomeChannel"})
	var yerr *Error
	if !errors.As(err, &yerr) || yerr.Code != CodeNotLive {
		t.Fatalf("resolveVideoID() error = %v, want code %q", err, CodeNotLive)
	}
}

// TestInnerTubeBaseURLDefault checks the production default.
func TestInnerTubeBaseURLDefault(t *testing.T) {
	p := &InnerTubeProvider{}
	if got := p.baseURL(); got != defaultInnerTubeBaseURL {
		t.Errorf("baseURL() = %q, want %q", got, defaultInnerTubeBaseURL)
	}
	if _, err := url.Parse(p.baseURL()); err != nil {
		t.Errorf("baseURL() is not a valid URL: %v", err)
	}
}
