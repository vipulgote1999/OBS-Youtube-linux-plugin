package youtube

import (
	"context"
	"regexp"

	"obs-youtube-chat-tts/helper/internal/protocol"
)

// Wire error codes carried in protocol.Event.Code. They are stable strings
// consumed by the OBS plugin's helper parser.
const (
	// CodeAuth means the API key is missing, invalid, or not allowed to read
	// the chat.
	CodeAuth = "auth"
	// CodeQuota means the YouTube Data API quota is exhausted.
	CodeQuota = "quota"
	// CodeNotLive means the input resolved but is not an active live stream.
	CodeNotLive = "not_live"
	// CodeChatDisabled means the broadcast is live but its chat is disabled.
	CodeChatDisabled = "chat_disabled"
	// CodeNetwork means a transport, DNS, TLS, or gRPC availability failure.
	CodeNetwork = "network"
	// CodeParse means the input or a YouTube response could not be parsed.
	CodeParse = "parse"
	// CodeEnded means the live chat ended.
	CodeEnded = "ended"
)

// Provider streams live chat messages for one configured broadcast.
//
// Run blocks until the stream ends, the context is cancelled, or an error
// stops the provider. Status and message events are delivered through emit;
// a returned error is reported by the caller on the protocol stream. Run
// returns nil after a normal end (the chat ended) and after context
// cancellation.
type Provider interface {
	Run(ctx context.Context, cfg Config, emit func(protocol.Event)) error
}

// Config is the JSON object the OBS plugin writes on the helper's stdin.
type Config struct {
	Provider string `json:"provider"`
	APIKey   string `json:"api_key"`
	Video    string `json:"video"`
	HL       string `json:"hl"`
}

// Error is the provider's internal error type. The caller maps Code to the
// wire code, Message to the human-readable detail, and Fatal to the event's
// fatal flag. Messages never contain the API key.
type Error struct {
	Code    string
	Message string
	Fatal   bool
}

// Error implements error.
func (e *Error) Error() string { return e.Message }

// redactSecrets removes API keys from text that may be logged or written to
// the protocol. net/http quotes full request URLs in transport errors, and
// those URLs carry the key query parameter.
var secretPatterns = []*regexp.Regexp{
	regexp.MustCompile(`(?i)([?&]key=)[^&\s"']+`),
	regexp.MustCompile(`(?i)(x-goog-api-key[=:]\s*)\S+`),
}

// redactSecrets replaces key material in s with REDACTED.
func redactSecrets(s string) string {
	for _, re := range secretPatterns {
		s = re.ReplaceAllString(s, "${1}REDACTED")
	}
	return s
}
