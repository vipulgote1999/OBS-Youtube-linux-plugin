// Command ytchat-helper reads one JSON config object on stdin and streams
// YouTube live chat as JSON Lines events on stdout. stdout carries protocol
// events only; every log line goes to stderr. The API key arrives on stdin,
// never on the command line, and is never written to any log or event.
//
// The official provider resolves the input through the YouTube Data API and
// streams messages from V3DataLiveChatMessageService over gRPC. The InnerTube
// provider and the auto fallback land in a later task.
package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"os/signal"
	"syscall"

	"obs-youtube-chat-tts/helper/internal/protocol"
	"obs-youtube-chat-tts/helper/internal/youtube"
)

// helperVersion is what --version prints.
const helperVersion = "0.1.0"

// config is the single JSON object the OBS plugin writes on the helper's
// stdin before closing it: {"provider","api_key","video","hl"}.
type config struct {
	Provider string `json:"provider"`
	APIKey   string `json:"api_key"`
	Video    string `json:"video"`
	HL       string `json:"hl"`
}

// validProvider reports whether the configured chat provider is known.
func validProvider(p string) bool {
	switch p {
	case "auto", "official", "innertube":
		return true
	}
	return false
}

func main() {
	log.SetFlags(0)
	log.SetPrefix("ytchat-helper: ")

	// --version prints and exits before anything is read from stdin.
	for _, arg := range os.Args[1:] {
		if arg == "--version" {
			fmt.Printf("ytchat-helper %s\n", helperVersion)
			return
		}
	}

	os.Exit(run(os.Stdin, os.Stdout))
}

// run drives the helper and returns the process exit code: 0 when the chat
// stream ends normally or the process is cancelled, 1 after a fatal error.
// Fatal problems are reported on stdout as protocol error events; stdout
// write failures are logged to stderr.
func run(stdin io.Reader, stdout io.Writer) int {
	raw, err := io.ReadAll(stdin)
	if err != nil {
		return fatal(stdout, "config", fmt.Sprintf("failed to read config from stdin: %v", err))
	}

	var cfg config
	if err := json.Unmarshal(raw, &cfg); err != nil {
		// encoding/json errors describe the syntax problem, never the
		// config values themselves.
		return fatal(stdout, "config", "invalid config JSON: "+err.Error())
	}
	if !validProvider(cfg.Provider) {
		return fatal(stdout, "config",
			fmt.Sprintf("invalid provider %q (want auto, official, or innertube)", cfg.Provider))
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	providerCfg := youtube.Config{
		Provider: cfg.Provider,
		APIKey:   cfg.APIKey,
		Video:    cfg.Video,
		HL:       cfg.HL,
	}

	switch cfg.Provider {
	case "official":
		return runOfficial(ctx, stdout, providerCfg)
	case "auto":
		if cfg.APIKey != "" {
			return runOfficial(ctx, stdout, providerCfg)
		}
		return fatal(stdout, "not_implemented",
			"the auto provider needs an API key until the InnerTube fallback lands")
	default: // innertube
		return fatal(stdout, "not_implemented",
			"the innertube provider is not implemented yet")
	}
}

// runOfficial streams chat through the official provider, reporting connecting
// before the first request and mapping provider errors to protocol events.
func runOfficial(ctx context.Context, stdout io.Writer, cfg youtube.Config) int {
	if err := protocol.WriteEvent(stdout, protocol.Event{
		Type:   "status",
		State:  "connecting",
		Detail: "official",
	}); err != nil {
		log.Printf("error: failed to write status event: %v", err)
		return 1
	}

	emit := func(e protocol.Event) {
		if err := protocol.WriteEvent(stdout, e); err != nil {
			log.Printf("error: failed to write protocol event: %v", err)
		}
	}

	provider := &youtube.OfficialProvider{}
	err := provider.Run(ctx, cfg, emit)
	if err == nil {
		return 0
	}
	if ctx.Err() != nil || errors.Is(err, context.Canceled) {
		return 0
	}
	return providerError(stdout, err)
}

// providerError writes one error event for a provider failure and returns the
// process exit code: 1 for fatal failures, 0 for retryable ones.
func providerError(w io.Writer, err error) int {
	code := youtube.CodeNetwork
	detail := "YouTube chat provider failed"
	fatalErr := true

	var yerr *youtube.Error
	if errors.As(err, &yerr) {
		code = yerr.Code
		detail = yerr.Message
		fatalErr = yerr.Fatal
	}

	if writeErr := protocol.WriteEvent(w, protocol.Event{
		Type:   "error",
		Code:   code,
		Detail: detail,
		Fatal:  fatalErr,
	}); writeErr != nil {
		log.Printf("error: failed to write error event: %v", writeErr)
	}
	if fatalErr {
		return 1
	}
	return 0
}

// fatal writes a fatal error event on w and returns exit code 1.
func fatal(w io.Writer, code, detail string) int {
	if err := protocol.WriteEvent(w, protocol.Event{
		Type:   "error",
		Code:   code,
		Detail: detail,
		Fatal:  true,
	}); err != nil {
		log.Printf("error: failed to write error event: %v", err)
	}
	return 1
}
