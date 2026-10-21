package protocol

import (
	"bytes"
	"encoding/json"
	"reflect"
	"strings"
	"sync"
	"testing"
)

func TestWriteEvent(t *testing.T) {
	tests := []struct {
		name     string
		event    Event
		wantLine string
	}{
		{
			name:     "status event omits unset fields",
			event:    Event{Type: "status", State: "resolving"},
			wantLine: `{"type":"status","state":"resolving"}`,
		},
		{
			name:     "status event carries detail",
			event:    Event{Type: "status", State: "connected", Detail: "official"},
			wantLine: `{"type":"status","state":"connected","detail":"official"}`,
		},
		{
			name:     "fatal error carries code detail and fatal",
			event:    Event{Type: "error", Code: "quota", Detail: "quota exceeded", Fatal: true},
			wantLine: `{"type":"error","code":"quota","detail":"quota exceeded","fatal":true}`,
		},
		{
			name: "message event carries the full chat message",
			event: Event{
				Type: "message",
				Message: &ChatMessage{
					ID:          "msg-1",
					Author:      "Alice <mods>",
					AuthorID:    "UCalice",
					Text:        "hello <b>chat</b>",
					PublishedAt: "2026-10-21T12:00:05Z",
					Kind:        "text",
					IsModerator: true,
					IsOwner:     false,
					IsSponsor:   true,
				},
			},
			wantLine: `{"type":"message","message":{"id":"msg-1","author":"Alice <mods>","author_id":"UCalice","text":"hello <b>chat</b>","published_at":"2026-10-21T12:00:05Z","kind":"text","is_moderator":true,"is_owner":false,"is_sponsor":true}}`,
		},
		{
			name: "superchat message omits empty optional fields",
			event: Event{
				Type: "message",
				Message: &ChatMessage{
					ID:     "msg-2",
					Author: "Bob",
					Text:   "enjoy the stream ><>",
					Kind:   "superchat",
					Amount: "$5.00",
				},
			},
			wantLine: `{"type":"message","message":{"id":"msg-2","author":"Bob","text":"enjoy the stream ><>","kind":"superchat","amount":"$5.00","is_moderator":false,"is_owner":false,"is_sponsor":false}}`,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			var buf bytes.Buffer
			if err := WriteEvent(&buf, tt.event); err != nil {
				t.Fatalf("WriteEvent() error = %v", err)
			}
			out := buf.String()

			// One encoded event must be exactly one line ending in a single
			// newline.
			if !strings.HasSuffix(out, "\n") {
				t.Errorf("encoded event must end in \\n, got %q", out)
			}
			if n := strings.Count(out, "\n"); n != 1 {
				t.Errorf("encoded event must be exactly one line, got %d newlines: %q", n, out)
			}
			line := strings.TrimSuffix(out, "\n")

			// Angle brackets must survive unescaped (SetEscapeHTML(false)).
			if strings.Contains(line, `\u003c`) || strings.Contains(line, `\u003e`) {
				t.Errorf("encoded event must not HTML-escape angle brackets: %q", line)
			}
			for _, raw := range []string{"<", ">"} {
				if strings.Contains(tt.wantLine, raw) && !strings.Contains(line, raw) {
					t.Errorf("encoded event lost raw %q: %q", raw, line)
				}
			}

			// The encoded line must include exactly the expected fields with
			// the expected values, independent of key order.
			var want, got any
			if err := json.Unmarshal([]byte(tt.wantLine), &want); err != nil {
				t.Fatalf("test bug: wantLine is not valid JSON: %v", err)
			}
			if err := json.Unmarshal([]byte(line), &got); err != nil {
				t.Fatalf("encoded event is not valid JSON: %v", err)
			}
			if !reflect.DeepEqual(got, want) {
				t.Errorf("WriteEvent() =\n%s\nwant\n%s", line, tt.wantLine)
			}
		})
	}
}

// TestWriteEventConcurrentLines verifies the write lock: concurrent writers
// sharing one stream must never interleave partial lines.
func TestWriteEventConcurrentLines(t *testing.T) {
	const (
		goroutines   = 8
		perGoroutine = 50
	)
	var buf bytes.Buffer
	var wg sync.WaitGroup
	for i := 0; i < goroutines; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for j := 0; j < perGoroutine; j++ {
				e := Event{
					Type: "message",
					Message: &ChatMessage{
						ID:     "m",
						Author: "a",
						Text:   "<x>",
						Kind:   "text",
					},
				}
				if err := WriteEvent(&buf, e); err != nil {
					t.Errorf("WriteEvent() error = %v", err)
				}
			}
		}()
	}
	wg.Wait()

	lines := strings.Split(strings.TrimSuffix(buf.String(), "\n"), "\n")
	if len(lines) != goroutines*perGoroutine {
		t.Fatalf("got %d complete lines, want %d", len(lines), goroutines*perGoroutine)
	}
	for _, line := range lines {
		var got map[string]any
		if err := json.Unmarshal([]byte(line), &got); err != nil {
			t.Fatalf("interleaved or partial line: %q (%v)", line, err)
		}
	}
}
