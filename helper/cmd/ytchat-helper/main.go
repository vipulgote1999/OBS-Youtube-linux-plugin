// Command ytchat-helper reads one JSON config object on stdin and streams
// YouTube live chat as JSON Lines events on stdout. stdout carries protocol
// events only; every log line goes to stderr. The API key arrives on stdin,
// never on the command line, and is never written to any log or event.
//
// Current state: the config is validated and a "resolving" status is
// emitted, then the helper stops with a fatal not_implemented error. The
// YouTube chat providers land in later tasks.
package main

import (
	"encoding/json"
	"fmt"
	"io"
	"log"
	"os"

	"obs-youtube-chat-tts/helper/internal/protocol"
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
// stream ends normally, 1 after a fatal error. Fatal problems are reported
// on stdout as protocol error events; stdout write failures are logged to
// stderr.
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

	// Resolve the video/channel input to a live chat stream. The providers
	// that do this land in later tasks; stop with a fatal error for now.
	if err := protocol.WriteEvent(stdout, protocol.Event{Type: "status", State: "resolving"}); err != nil {
		log.Printf("error: failed to write status event: %v", err)
		return 1
	}
	return fatal(stdout, "not_implemented", "providers not implemented")
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
