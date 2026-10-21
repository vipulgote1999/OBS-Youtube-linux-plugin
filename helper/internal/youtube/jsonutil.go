package youtube

import (
	"errors"
	"fmt"
)

// errNoJSONObject is returned when the input contains no opening brace at or
// after the requested offset.
var errNoJSONObject = errors.New("no JSON object found")

// extractBalancedJSON returns the JSON object embedded in s that begins at the
// first '{' at or after start.
//
// The scanner tracks brace depth, string state, and backslash escapes, so a
// '}' inside a quoted string never terminates the object. YouTube embeds its
// page data in JavaScript assignments such as
//
//	var ytInitialData = {...};
//
// where a regular expression over "}" would stop at the first brace inside a
// chat message or a URL.
//
// The returned end index is the offset immediately after the matching closing
// brace, ready to resume scanning. An error is returned when no object starts
// at or after start or when the object is unterminated.
func extractBalancedJSON(s string, start int) (string, int, error) {
	if start < 0 || start > len(s) {
		return "", 0, fmt.Errorf("JSON start index %d is out of range", start)
	}

	open := start
	for open < len(s) && s[open] != '{' {
		open++
	}
	if open == len(s) {
		return "", 0, errNoJSONObject
	}

	depth := 0
	inString := false
	escaped := false
	for i := open; i < len(s); i++ {
		c := s[i]

		if inString {
			switch {
			case escaped:
				escaped = false
			case c == '\\':
				escaped = true
			case c == '"':
				inString = false
			}
			continue
		}

		switch c {
		case '"':
			inString = true
		case '{':
			depth++
		case '}':
			depth--
			if depth == 0 {
				return s[open : i+1], i + 1, nil
			}
		}
	}

	return "", 0, errors.New("unterminated JSON object")
}
