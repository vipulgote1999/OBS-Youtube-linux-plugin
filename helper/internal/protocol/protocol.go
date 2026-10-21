// Package protocol defines the JSON Lines event stream the ytchat-helper
// writes on stdout: one JSON object per line. Human-readable status and error
// text is carried in the detail field; the message field is used exclusively
// for chat messages and never for error text.
package protocol

import (
	"encoding/json"
	"io"
	"sync"
)

// Event is one line of the helper's stdout stream.
type Event struct {
	Type    string       `json:"type"`              // status | message | error
	State   string       `json:"state,omitempty"`   // resolving|connecting|connected|reconnecting|ended
	Detail  string       `json:"detail,omitempty"`  // status/error text; never a secret
	Message *ChatMessage `json:"message,omitempty"` // only for type=message
	Code    string       `json:"code,omitempty"`    // only for type=error
	Fatal   bool         `json:"fatal,omitempty"`   // only for type=error
}

// ChatMessage is a single live chat message.
type ChatMessage struct {
	ID          string `json:"id"`
	Author      string `json:"author"`
	AuthorID    string `json:"author_id,omitempty"`
	Text        string `json:"text"`
	PublishedAt string `json:"published_at,omitempty"`
	Kind        string `json:"kind"` // text | superchat | membership
	Amount      string `json:"amount,omitempty"`
	IsModerator bool   `json:"is_moderator"`
	IsOwner     bool   `json:"is_owner"`
	IsSponsor   bool   `json:"is_sponsor"`
}

// writeMu serializes event encoding so concurrent writers can never
// interleave partial lines on a shared stream.
var writeMu sync.Mutex

// WriteEvent encodes e as a single JSON line on w, terminated by "\n".
// HTML escaping is disabled so chat text containing <, >, or & survives
// verbatim.
func WriteEvent(w io.Writer, e Event) error {
	writeMu.Lock()
	defer writeMu.Unlock()
	enc := json.NewEncoder(w)
	enc.SetEscapeHTML(false)
	return enc.Encode(e)
}
