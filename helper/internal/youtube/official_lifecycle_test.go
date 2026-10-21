package youtube

import (
	"context"
	"errors"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"obs-youtube-chat-tts/helper/internal/protocol"
)

// TestRunPreConnectFailure verifies the connection lifecycle: connected is
// not emitted until the gRPC stream is actually up, and an unreachable
// endpoint produces a bounded, non-fatal network error after three attempts
// instead of retrying forever.
func TestRunPreConnectFailure(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		writeJSON(w, http.StatusOK, `{
			"items": [{
				"id": "dQw4w9WgXcQ",
				"liveStreamingDetails": {"activeLiveChatId": "chat-abc"}
			}]
		}`)
	}))
	defer srv.Close()

	p := &OfficialProvider{
		APIBaseURL:   srv.URL,
		GRPCEndpoint: "127.0.0.1:1", // closed port: connection refused
	}

	ctx, cancel := context.WithTimeout(context.Background(), 12*time.Second)
	defer cancel()

	var events []protocol.Event
	start := time.Now()
	err := p.Run(ctx, Config{
		Provider: "official",
		APIKey:   "test-key",
		Video:    "dQw4w9WgXcQ",
		HL:       "en",
	}, func(e protocol.Event) { events = append(events, e) })
	elapsed := time.Since(start)

	var yerr *Error
	if !errors.As(err, &yerr) {
		t.Fatalf("error = %v (%T), want *Error with code %s", err, err, CodeNetwork)
	}
	if yerr.Code != CodeNetwork {
		t.Errorf("error code = %q, want %q", yerr.Code, CodeNetwork)
	}
	if yerr.Fatal {
		t.Error("error is fatal, want non-fatal so auto can fall back")
	}
	if yerr.Message == "" {
		t.Error("error message is empty")
	}
	if strings.Contains(yerr.Message, "test-key") {
		t.Errorf("error message leaked the API key: %q", yerr.Message)
	}

	reconnecting := 0
	for _, e := range events {
		switch {
		case e.Type == "status" && e.State == "connected":
			t.Error("connected was emitted without a working gRPC stream")
		case e.Type == "status" && e.State == "reconnecting":
			reconnecting++
		case e.Type == "error":
			t.Errorf("provider emitted an error event; the caller owns that: %#v", e)
		}
	}
	if reconnecting != 2 {
		t.Errorf("reconnecting statuses = %d, want 2 for three total attempts", reconnecting)
	}

	// Three attempts with 1 s + 2 s backoff finish in about three seconds.
	// A generous ceiling still proves the loop is bounded.
	if elapsed > 8*time.Second {
		t.Errorf("Run took %s, want a bounded retry loop", elapsed)
	}
}
