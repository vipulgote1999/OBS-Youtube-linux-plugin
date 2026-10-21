package main

import (
	"bytes"
	"context"
	"encoding/json"
	"strings"
	"testing"

	"obs-youtube-chat-tts/helper/internal/protocol"
	"obs-youtube-chat-tts/helper/internal/youtube"
)

// decodeEvents parses every JSON line in out.
func decodeEvents(t *testing.T, out string) []protocol.Event {
	t.Helper()
	var events []protocol.Event
	for _, line := range strings.Split(strings.TrimSpace(out), "\n") {
		if line == "" {
			continue
		}
		var e protocol.Event
		if err := json.Unmarshal([]byte(line), &e); err != nil {
			t.Fatalf("invalid event line %q: %v", line, err)
		}
		events = append(events, e)
	}
	return events
}

// runConfig runs the helper with one config object and returns its exit code
// and stdout.
func runConfig(t *testing.T, cfg string) (int, string) {
	t.Helper()
	var stdout bytes.Buffer
	code := run(strings.NewReader(cfg), &stdout)
	return code, stdout.String()
}

// TestRunInnertubeNotImplemented documents the Task 5 boundary: the InnerTube
// provider is selected but not implemented yet.
func TestRunInnertubeNotImplemented(t *testing.T) {
	code, out := runConfig(t, `{"provider":"innertube","video":"dQw4w9WgXcQ"}`)
	if code != 1 {
		t.Errorf("exit = %d, want 1", code)
	}
	events := decodeEvents(t, out)
	if len(events) != 1 {
		t.Fatalf("events = %#v, want one error event", events)
	}
	if events[0].Type != "error" || events[0].Code != "not_implemented" || !events[0].Fatal {
		t.Errorf("event = %#v, want fatal not_implemented error", events[0])
	}
}

// TestRunAutoWithoutKey checks that auto without a key reports the same
// not-implemented boundary instead of attempting an unauthenticated request.
func TestRunAutoWithoutKey(t *testing.T) {
	code, out := runConfig(t, `{"provider":"auto","video":"dQw4w9WgXcQ"}`)
	if code != 1 {
		t.Errorf("exit = %d, want 1", code)
	}
	events := decodeEvents(t, out)
	if len(events) != 1 || events[0].Code != "not_implemented" {
		t.Fatalf("events = %#v, want one not_implemented error", events)
	}
}

// TestRunOfficialRequiresKey checks provider selection and the auth error
// path without network access.
func TestRunOfficialRequiresKey(t *testing.T) {
	code, out := runConfig(t, `{"provider":"official","video":"dQw4w9WgXcQ"}`)
	if code != 1 {
		t.Errorf("exit = %d, want 1", code)
	}
	events := decodeEvents(t, out)
	if len(events) != 2 {
		t.Fatalf("events = %#v, want connecting then error", events)
	}
	if events[0].Type != "status" || events[0].State != "connecting" {
		t.Errorf("first event = %#v, want connecting status", events[0])
	}
	if events[1].Type != "error" || events[1].Code != "auth" || !events[1].Fatal {
		t.Errorf("second event = %#v, want fatal auth error", events[1])
	}
	if strings.Contains(out, "api_key") {
		t.Error("stdout mentions api_key")
	}
}

// TestRunOfficialCancelledContext checks that cancellation exits 0 without an
// error event.
func TestRunOfficialCancelledContext(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	cancel()

	var stdout bytes.Buffer
	code := runOfficial(ctx, &stdout, youtube.Config{
		Provider: "official",
		APIKey:   "key",
		Video:    "dQw4w9WgXcQ",
	})
	if code != 0 {
		t.Errorf("exit = %d, want 0", code)
	}
	for _, e := range decodeEvents(t, stdout.String()) {
		if e.Type == "error" {
			t.Errorf("cancellation emitted an error event: %#v", e)
		}
	}
}
