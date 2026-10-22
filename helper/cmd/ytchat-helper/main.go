// Command ytchat-helper reads one JSON config object on stdin and streams
// YouTube live chat as JSON Lines events on stdout. stdout carries protocol
// events only; every log line goes to stderr. The API key arrives on stdin,
// never on the command line, and is never written to any log or event.
//
// The official provider resolves the input through the YouTube Data API and
// streams messages from V3DataLiveChatMessageService over gRPC. The
// experimental InnerTube provider needs no API key and reads YouTube's public
// watch page and live chat endpoint. The auto provider prefers the official
// API when a key is present and falls back to InnerTube on a pre-connection
// auth, quota, or network failure.
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
const helperVersion = "0.2.0"

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
	case "innertube":
		return runInnerTube(ctx, stdout, providerCfg)
	default: // auto
		return runAuto(ctx, stdout, providerCfg)
	}
}

// runProvider emits the connecting status for label, runs provider, and maps
// its error to the protocol stream.
func runProvider(
	ctx context.Context,
	stdout io.Writer,
	cfg youtube.Config,
	provider youtube.Provider,
	label string,
) int {
	if err := protocol.WriteEvent(stdout, protocol.Event{
		Type:   "status",
		State:  "connecting",
		Detail: label,
	}); err != nil {
		log.Printf("error: failed to write status event: %v", err)
		return 1
	}

	emit := func(e protocol.Event) {
		if err := protocol.WriteEvent(stdout, e); err != nil {
			log.Printf("error: failed to write protocol event: %v", err)
		}
	}

	err := provider.Run(ctx, cfg, emit)
	if err == nil {
		return 0
	}
	if ctx.Err() != nil || errors.Is(err, context.Canceled) {
		return 0
	}
	return providerError(stdout, err)
}

// runOfficial streams chat through the official provider.
func runOfficial(ctx context.Context, stdout io.Writer, cfg youtube.Config) int {
	return runProvider(ctx, stdout, cfg, &youtube.OfficialProvider{}, "official")
}

// runInnerTube streams chat through the experimental no-key provider.
func runInnerTube(ctx context.Context, stdout io.Writer, cfg youtube.Config) int {
	return runProvider(ctx, stdout, cfg, &youtube.InnerTubeProvider{}, "innertube (experimental)")
}

// runAuto tries the official provider when a key is configured and falls back
// to the experimental InnerTube provider on a pre-connection failure.
func runAuto(ctx context.Context, stdout io.Writer, cfg youtube.Config) int {
	return runProvider(ctx, stdout, cfg, autoProvider{
		official: &youtube.OfficialProvider{},
		inner:    &youtube.InnerTubeProvider{},
	}, "auto")
}

// autoProvider adapts RunAuto to the Provider interface so main can share one
// event and exit-code path.
type autoProvider struct {
	official youtube.Provider
	inner    youtube.Provider
}

// Run implements youtube.Provider.
func (a autoProvider) Run(ctx context.Context, cfg youtube.Config, emit func(protocol.Event)) error {
	return youtube.RunAuto(ctx, cfg, a.official, a.inner, emit)
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
