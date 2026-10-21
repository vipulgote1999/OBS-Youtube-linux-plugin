package youtube

import (
	"context"
	"errors"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

// newTestProvider returns an OfficialProvider pointed at an httptest server.
func newTestProvider(t *testing.T, handler http.HandlerFunc) (*OfficialProvider, *httptest.Server) {
	t.Helper()
	srv := httptest.NewServer(handler)
	t.Cleanup(srv.Close)
	return &OfficialProvider{APIBaseURL: srv.URL}, srv
}

// writeJSON writes a JSON response body with the YouTube API content type.
func writeJSON(w http.ResponseWriter, status int, body string) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	fmt.Fprint(w, body)
}

// TestResolveLiveChatIDDirectVideo checks that a direct video ID skips
// channel resolution, calls videos.list, and returns the active live chat ID.
func TestResolveLiveChatIDDirectVideo(t *testing.T) {
	var paths []string
	p, _ := newTestProvider(t, func(w http.ResponseWriter, r *http.Request) {
		paths = append(paths, r.URL.Path)
		if r.URL.Path != "/videos" {
			t.Errorf("path = %q, want /videos", r.URL.Path)
			writeJSON(w, http.StatusNotFound, `{"error":{"code":404}}`)
			return
		}
		q := r.URL.Query()
		if got := q.Get("part"); got != "liveStreamingDetails" {
			t.Errorf("part = %q, want liveStreamingDetails", got)
		}
		if got := q.Get("id"); got != "dQw4w9WgXcQ" {
			t.Errorf("id = %q, want dQw4w9WgXcQ", got)
		}
		if got := q.Get("key"); got != "test-key" {
			t.Errorf("key = %q, want the configured key", got)
		}
		if got := r.Header.Get("User-Agent"); got != userAgent {
			t.Errorf("User-Agent = %q, want %q", got, userAgent)
		}
		writeJSON(w, http.StatusOK, `{
			"items": [{
				"id": "dQw4w9WgXcQ",
				"liveStreamingDetails": {"activeLiveChatId": "chat-abc"}
			}]
		}`)
	})

	videoID, chatID, err := p.resolveLiveChatID(context.Background(), Config{
		APIKey: "test-key",
		Video:  "dQw4w9WgXcQ",
	})
	if err != nil {
		t.Fatalf("resolveLiveChatID: %v", err)
	}
	if videoID != "dQw4w9WgXcQ" {
		t.Errorf("videoID = %q, want dQw4w9WgXcQ", videoID)
	}
	if chatID != "chat-abc" {
		t.Errorf("liveChatID = %q, want chat-abc", chatID)
	}
	if len(paths) != 1 {
		t.Errorf("requests = %v, want exactly one videos.list request", paths)
	}
}

// TestResolveChannelHandle checks the channel path: channels.list resolves the
// handle, search.list finds a live video, and videos.list returns its chat.
func TestResolveChannelHandle(t *testing.T) {
	p, _ := newTestProvider(t, func(w http.ResponseWriter, r *http.Request) {
		q := r.URL.Query()
		switch r.URL.Path {
		case "/channels":
			if got := q.Get("forHandle"); got != "@SomeChannel" {
				t.Errorf("forHandle = %q, want @SomeChannel", got)
			}
			writeJSON(w, http.StatusOK, `{"items":[{"id":"UC-channel"}]}`)
		case "/search":
			if got := q.Get("channelId"); got != "UC-channel" {
				t.Errorf("channelId = %q, want UC-channel", got)
			}
			if got := q.Get("eventType"); got != "live" {
				t.Errorf("eventType = %q, want live", got)
			}
			if got := q.Get("type"); got != "video" {
				t.Errorf("type = %q, want video", got)
			}
			writeJSON(w, http.StatusOK, `{"items":[{"id":{"videoId":"liveVideo01"}}]}`)
		case "/videos":
			if got := q.Get("id"); got != "liveVideo01" {
				t.Errorf("id = %q, want liveVideo01", got)
			}
			writeJSON(w, http.StatusOK, `{"items":[{"liveStreamingDetails":{"activeLiveChatId":"chat-xyz"}}]}`)
		default:
			t.Errorf("unexpected path %q", r.URL.Path)
			writeJSON(w, http.StatusNotFound, `{"error":{"code":404}}`)
		}
	})

	videoID, chatID, err := p.resolveLiveChatID(context.Background(), Config{
		APIKey: "test-key",
		Video:  "https://www.youtube.com/@SomeChannel/live",
	})
	if err != nil {
		t.Fatalf("resolveLiveChatID: %v", err)
	}
	if videoID != "liveVideo01" {
		t.Errorf("videoID = %q, want liveVideo01", videoID)
	}
	if chatID != "chat-xyz" {
		t.Errorf("liveChatID = %q, want chat-xyz", chatID)
	}
}

// TestResolveNoLive checks that a video without an activeLiveChatId is
// reported as not live rather than as a transport or parse error.
func TestResolveNoLive(t *testing.T) {
	p, _ := newTestProvider(t, func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, http.StatusOK, `{"items":[{"id":"dQw4w9WgXcQ","liveStreamingDetails":{}}]}`)
	})

	_, _, err := p.resolveLiveChatID(context.Background(), Config{
		APIKey: "test-key",
		Video:  "dQw4w9WgXcQ",
	})
	assertCode(t, err, CodeNotLive)
}

// TestAPIErrorQuota checks that a quotaExceeded response maps to CodeQuota.
func TestAPIErrorQuota(t *testing.T) {
	p, _ := newTestProvider(t, func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, http.StatusForbidden, `{
			"error": {
				"code": 403,
				"message": "The request cannot be completed because you have exceeded your quota.",
				"errors": [{
					"message": "Quota exceeded.",
					"domain": "youtube.quota",
					"reason": "quotaExceeded"
				}],
				"status": "RESOURCE_EXHAUSTED"
			}
		}`)
	})

	_, _, err := p.resolveLiveChatID(context.Background(), Config{
		APIKey: "test-key",
		Video:  "dQw4w9WgXcQ",
	})
	assertCode(t, err, CodeQuota)
}

// TestAPIErrorInvalidKey checks that keyInvalid maps to CodeAuth.
func TestAPIErrorInvalidKey(t *testing.T) {
	p, _ := newTestProvider(t, func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, http.StatusBadRequest, `{
			"error": {
				"code": 400,
				"message": "API key not valid. Please pass a valid API key.",
				"errors": [{
					"message": "API key not valid.",
					"domain": "global",
					"reason": "keyInvalid"
				}],
				"status": "INVALID_ARGUMENT"
			}
		}`)
	})

	_, _, err := p.resolveLiveChatID(context.Background(), Config{
		APIKey: "test-key",
		Video:  "dQw4w9WgXcQ",
	})
	assertCode(t, err, CodeAuth)
}

// TestAPIErrorBadRequest checks the second documented authentication reason.
func TestAPIErrorBadRequest(t *testing.T) {
	p, _ := newTestProvider(t, func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, http.StatusBadRequest, `{"error":{"code":400,"errors":[{"reason":"badRequest"}]}}`)
	})

	_, _, err := p.resolveLiveChatID(context.Background(), Config{
		APIKey: "test-key",
		Video:  "dQw4w9WgXcQ",
	})
	assertCode(t, err, CodeAuth)
}

// TestErrorRedactsKey makes sure transport errors that quote a request URL
// never leak the key parameter.
func TestErrorRedactsKey(t *testing.T) {
	got := redactSecrets(`Get "https://www.googleapis.com/youtube/v3/videos?part=id&key=SECRET123&id=x": dial tcp: refused`)
	if strings.Contains(got, "SECRET123") {
		t.Fatalf("redacted error still contains the API key: %q", got)
	}
	if !strings.Contains(got, "key=REDACTED") {
		t.Errorf("redacted error = %q, want key=REDACTED marker", got)
	}
}

// assertCode fails the test unless err is an *Error with want as its code.
func assertCode(t *testing.T, err error, want string) {
	t.Helper()
	if err == nil {
		t.Fatalf("error = nil, want code %s", want)
	}
	var yerr *Error
	if !errors.As(err, &yerr) {
		t.Fatalf("error = %T %v, want *Error", err, err)
	}
	if yerr.Code != want {
		t.Fatalf("error code = %q (%v), want %q", yerr.Code, err, want)
	}
}
