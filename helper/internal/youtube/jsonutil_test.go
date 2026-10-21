package youtube

import (
	"strings"
	"testing"
)

// TestExtractBalancedJSON checks the scanner against objects whose strings
// contain braces, semicolons, and escaped quotes. The end index is the offset
// immediately after the matching closing brace.
func TestExtractBalancedJSON(t *testing.T) {
	tests := []struct {
		name    string
		input   string
		start   int
		want    string
		wantEnd int
	}{
		{
			name:    "brace and semicolon inside a string",
			input:   `prefix {"a":"};not-the-end","b":1} suffix`,
			start:   0,
			want:    `{"a":"};not-the-end","b":1}`,
			wantEnd: 34,
		},
		{
			name:    "nested objects with an escaped quote",
			input:   `x={"outer":{"inner":[1,{"deep":"a\"}b"}]},"ok":true}`,
			start:   2,
			want:    `{"outer":{"inner":[1,{"deep":"a\"}b"}]},"ok":true}`,
			wantEnd: 52,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got, end, err := extractBalancedJSON(tt.input, tt.start)
			if err != nil {
				t.Fatalf("extractBalancedJSON() error = %v", err)
			}
			if got != tt.want {
				t.Errorf("extractBalancedJSON() = %q, want %q", got, tt.want)
			}
			if end != tt.wantEnd {
				t.Errorf("end = %d, want %d", end, tt.wantEnd)
			}
		})
	}
}

// TestExtractBalancedJSONSkipsPrefix checks that the scan starts at the first
// opening brace at or after start rather than requiring start to point at it.
func TestExtractBalancedJSONSkipsPrefix(t *testing.T) {
	input := `window["x"] = {"a":1}; tail`
	got, end, err := extractBalancedJSON(input, 0)
	if err != nil {
		t.Fatalf("extractBalancedJSON() error = %v", err)
	}
	if got != `{"a":1}` {
		t.Errorf("extractBalancedJSON() = %q, want %q", got, `{"a":1}`)
	}
	if want := strings.Index(input, "{"); end != want+len(`{"a":1}`) {
		t.Errorf("end = %d, want %d", end, want+len(`{"a":1}`))
	}
}

// TestExtractBalancedJSONUnterminated verifies that truncated objects and
// unterminated strings are reported as errors instead of returning a partial
// value.
func TestExtractBalancedJSONUnterminated(t *testing.T) {
	for _, input := range []string{
		`{"a":1`,
		`{"a":{"b":1}`,
		`{"a":"no closing quote`,
		`{"a":"escaped \" quote}`,
	} {
		t.Run(input, func(t *testing.T) {
			got, _, err := extractBalancedJSON(input, 0)
			if err == nil {
				t.Fatalf("extractBalancedJSON(%q) = %q, want an error", input, got)
			}
		})
	}
}

// TestExtractBalancedJSONNoObject checks the error when no brace is present.
func TestExtractBalancedJSONNoObject(t *testing.T) {
	if _, _, err := extractBalancedJSON("no object here", 0); err == nil {
		t.Fatal("extractBalancedJSON() error = nil, want an error")
	}
}

// TestExtractBalancedJSONBadStart checks the bounds check on start.
func TestExtractBalancedJSONBadStart(t *testing.T) {
	if _, _, err := extractBalancedJSON(`{"a":1}`, -1); err == nil {
		t.Fatal("extractBalancedJSON() with negative start error = nil, want an error")
	}
	if _, _, err := extractBalancedJSON(`{"a":1}`, 100); err == nil {
		t.Fatal("extractBalancedJSON() with out-of-range start error = nil, want an error")
	}
}
