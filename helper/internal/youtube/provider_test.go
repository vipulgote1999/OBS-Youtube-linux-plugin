package youtube

import (
	"context"
	"errors"
	"strings"
	"testing"

	"obs-youtube-chat-tts/helper/internal/protocol"
)

// fakeProvider is a Provider whose behavior a test supplies.
type fakeProvider struct {
	calls int
	run   func(ctx context.Context, cfg Config, emit func(protocol.Event)) error
}

func (f *fakeProvider) Run(ctx context.Context, cfg Config, emit func(protocol.Event)) error {
	f.calls++
	return f.run(ctx, cfg, emit)
}

// newFake returns a fake provider that records its calls and runs fn.
func newFake(fn func(ctx context.Context, cfg Config, emit func(protocol.Event)) error) *fakeProvider {
	return &fakeProvider{run: fn}
}

// TestAutoFallbackOnAuthError is the required auto contract: with a key, auto
// tries the official provider first, and a pre-connection auth failure emits a
// clear warning and falls back to the experimental InnerTube provider.
func TestAutoFallbackOnAuthError(t *testing.T) {
	official := newFake(func(context.Context, Config, func(protocol.Event)) error {
		return &Error{Code: CodeAuth, Message: "API key not valid", Fatal: true}
	})
	inner := newFake(func(_ context.Context, _ Config, emit func(protocol.Event)) error {
		emit(protocol.Event{Type: "status", State: "connected", Detail: "innertube (experimental)"})
		return nil
	})

	var events []protocol.Event
	err := RunAuto(context.Background(), Config{
		Provider: "auto",
		APIKey:   "secret-key",
		Video:    "dQw4w9WgXcQ",
	}, official, inner, func(e protocol.Event) { events = append(events, e) })
	if err != nil {
		t.Fatalf("RunAuto() error = %v", err)
	}
	if official.calls != 1 {
		t.Errorf("official calls = %d, want 1", official.calls)
	}
	if inner.calls != 1 {
		t.Errorf("inner calls = %d, want 1", inner.calls)
	}

	var warned bool
	for _, e := range events {
		if e.Type == "status" && strings.Contains(e.Detail, "falling back") {
			warned = true
			if !strings.Contains(e.Detail, "experimental innertube") {
				t.Errorf("fallback detail = %q, want an experimental marker", e.Detail)
			}
			if strings.Contains(e.Detail, "secret-key") {
				t.Errorf("fallback detail leaked the API key: %q", e.Detail)
			}
		}
	}
	if !warned {
		t.Errorf("events = %#v, want a fallback status warning", events)
	}
}

// TestAutoFallbackReasons covers the three fallback-eligible pre-connection
// codes.
func TestAutoFallbackReasons(t *testing.T) {
	for _, code := range []string{CodeAuth, CodeQuota, CodeNetwork} {
		t.Run(code, func(t *testing.T) {
			official := newFake(func(context.Context, Config, func(protocol.Event)) error {
				return &Error{Code: code, Message: "official failed", Fatal: false}
			})
			inner := newFake(func(context.Context, Config, func(protocol.Event)) error { return nil })

			err := RunAuto(context.Background(), Config{APIKey: "k", Video: "v"}, official, inner,
				func(protocol.Event) {})
			if err != nil {
				t.Fatalf("RunAuto() error = %v", err)
			}
			if inner.calls != 1 {
				t.Errorf("inner calls = %d, want 1", inner.calls)
			}
		})
	}
}

// TestAutoNoFallbackOnNotLive checks that a definitive stream state is
// surfaced instead of being retried through a different provider.
func TestAutoNoFallbackOnNotLive(t *testing.T) {
	for _, code := range []string{CodeNotLive, CodeChatDisabled, CodeParse} {
		t.Run(code, func(t *testing.T) {
			wantErr := &Error{Code: code, Message: "definitive", Fatal: true}
			official := newFake(func(context.Context, Config, func(protocol.Event)) error {
				return wantErr
			})
			inner := newFake(func(context.Context, Config, func(protocol.Event)) error { return nil })

			err := RunAuto(context.Background(), Config{APIKey: "k", Video: "v"}, official, inner,
				func(protocol.Event) {})
			if !errors.Is(err, wantErr) {
				t.Fatalf("RunAuto() error = %v, want %v", err, wantErr)
			}
			if inner.calls != 0 {
				t.Errorf("inner calls = %d, want 0", inner.calls)
			}
		})
	}
}

// TestAutoNoFallbackAfterConnected checks that a stream that reached connected
// is never retried through InnerTube, even if it later fails with an eligible
// code.
func TestAutoNoFallbackAfterConnected(t *testing.T) {
	wantErr := &Error{Code: CodeAuth, Message: "mid-stream failure", Fatal: true}
	official := newFake(func(_ context.Context, _ Config, emit func(protocol.Event)) error {
		emit(protocol.Event{Type: "status", State: "connected", Detail: "official"})
		return wantErr
	})
	inner := newFake(func(context.Context, Config, func(protocol.Event)) error { return nil })

	err := RunAuto(context.Background(), Config{APIKey: "k", Video: "v"}, official, inner,
		func(protocol.Event) {})
	if !errors.Is(err, wantErr) {
		t.Fatalf("RunAuto() error = %v, want the official error", err)
	}
	if inner.calls != 0 {
		t.Errorf("inner calls = %d, want 0 after connected", inner.calls)
	}
}

// TestAutoWithoutKeyRunsInnerTube verifies that auto without a key never calls
// the official provider.
func TestAutoWithoutKeyRunsInnerTube(t *testing.T) {
	official := newFake(func(context.Context, Config, func(protocol.Event)) error {
		t.Error("official provider was called without an API key")
		return nil
	})
	inner := newFake(func(context.Context, Config, func(protocol.Event)) error { return nil })

	if err := RunAuto(context.Background(), Config{APIKey: "", Video: "v"}, official, inner,
		func(protocol.Event) {}); err != nil {
		t.Fatalf("RunAuto() error = %v", err)
	}
	if inner.calls != 1 {
		t.Errorf("inner calls = %d, want 1", inner.calls)
	}
}

// TestAutoOfficialSuccessStops checks that a successful official run is not
// followed by an InnerTube run.
func TestAutoOfficialSuccessStops(t *testing.T) {
	official := newFake(func(context.Context, Config, func(protocol.Event)) error { return nil })
	inner := newFake(func(context.Context, Config, func(protocol.Event)) error {
		t.Error("inner provider ran after a successful official stream")
		return nil
	})

	if err := RunAuto(context.Background(), Config{APIKey: "k", Video: "v"}, official, inner,
		func(protocol.Event) {}); err != nil {
		t.Fatalf("RunAuto() error = %v", err)
	}
	if inner.calls != 0 {
		t.Errorf("inner calls = %d, want 0", inner.calls)
	}
}

// TestAutoUnknownErrorNotSwallowed checks that a non-*Error failure is returned
// rather than triggering an unrelated fallback.
func TestAutoUnknownErrorNotSwallowed(t *testing.T) {
	wantErr := errors.New("boom")
	official := newFake(func(context.Context, Config, func(protocol.Event)) error { return wantErr })
	inner := newFake(func(context.Context, Config, func(protocol.Event)) error { return nil })

	err := RunAuto(context.Background(), Config{APIKey: "k", Video: "v"}, official, inner,
		func(protocol.Event) {})
	if !errors.Is(err, wantErr) {
		t.Fatalf("RunAuto() error = %v, want %v", err, wantErr)
	}
	if inner.calls != 0 {
		t.Errorf("inner calls = %d, want 0", inner.calls)
	}
}
