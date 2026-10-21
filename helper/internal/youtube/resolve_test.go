package youtube

import "testing"

// TestParseInput is the exact input table from the task brief.
func TestParseInput(t *testing.T) {
	tests := []struct {
		name    string
		input   string
		videoID string
		channel string
		wantErr bool
	}{
		{
			name:    "watch url",
			input:   "https://www.youtube.com/watch?v=dQw4w9WgXcQ",
			videoID: "dQw4w9WgXcQ",
		},
		{
			name:    "short link",
			input:   "https://youtu.be/dQw4w9WgXcQ?t=10",
			videoID: "dQw4w9WgXcQ",
		},
		{
			name:    "live url",
			input:   "https://www.youtube.com/live/dQw4w9WgXcQ",
			videoID: "dQw4w9WgXcQ",
		},
		{
			name:    "shorts url",
			input:   "https://www.youtube.com/shorts/dQw4w9WgXcQ",
			videoID: "dQw4w9WgXcQ",
		},
		{
			name:    "bare video id",
			input:   "dQw4w9WgXcQ",
			videoID: "dQw4w9WgXcQ",
		},
		{
			name:    "handle live url",
			input:   "https://www.youtube.com/@SomeChannel/live",
			channel: "@SomeChannel",
		},
		{
			name:    "channel id url",
			input:   "https://www.youtube.com/channel/UCabc123",
			channel: "UCabc123",
		},
		{
			name:    "non-youtube host",
			input:   "https://example.com/watch?v=x",
			wantErr: true,
		},
		{
			name:    "empty input",
			input:   "",
			wantErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			videoID, channel, err := ParseInput(tt.input)
			if tt.wantErr {
				if err == nil {
					t.Fatalf("ParseInput(%q) = (%q, %q), want error", tt.input, videoID, channel)
				}
				if videoID != "" || channel != "" {
					t.Errorf("ParseInput(%q) on error = (%q, %q), want empty video and channel", tt.input, videoID, channel)
				}
				return
			}
			if err != nil {
				t.Fatalf("ParseInput(%q) error = %v", tt.input, err)
			}
			if videoID != tt.videoID || channel != tt.channel {
				t.Errorf("ParseInput(%q) = (%q, %q), want (%q, %q)",
					tt.input, videoID, channel, tt.videoID, tt.channel)
			}
		})
	}
}

// TestParseInputForms covers the additional accepted forms: /embed URLs,
// mobile hosts, scheme-less URLs, extra query parameters, plain handle URLs,
// bare @handle input, and the legacy /c/name and /user/name channel forms.
func TestParseInputForms(t *testing.T) {
	tests := []struct {
		name    string
		input   string
		videoID string
		channel string
	}{
		{
			name:    "embed url",
			input:   "https://www.youtube.com/embed/dQw4w9WgXcQ",
			videoID: "dQw4w9WgXcQ",
		},
		{
			name:    "mobile watch url",
			input:   "https://m.youtube.com/watch?v=dQw4w9WgXcQ",
			videoID: "dQw4w9WgXcQ",
		},
		{
			name:    "watch url with extra params",
			input:   "https://www.youtube.com/watch?v=dQw4w9WgXcQ&list=PLabc&t=30",
			videoID: "dQw4w9WgXcQ",
		},
		{
			name:    "scheme-less watch url",
			input:   "www.youtube.com/watch?v=dQw4w9WgXcQ",
			videoID: "dQw4w9WgXcQ",
		},
		{
			name:    "scheme-less short link",
			input:   "youtu.be/dQw4w9WgXcQ",
			videoID: "dQw4w9WgXcQ",
		},
		{
			name:    "handle url",
			input:   "https://www.youtube.com/@SomeChannel",
			channel: "@SomeChannel",
		},
		{
			name:    "bare handle",
			input:   "@SomeChannel",
			channel: "@SomeChannel",
		},
		{
			name:    "custom name url",
			input:   "https://www.youtube.com/c/SomeName",
			channel: "c/SomeName",
		},
		{
			name:    "legacy user url",
			input:   "https://www.youtube.com/user/SomeName",
			channel: "user/SomeName",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			videoID, channel, err := ParseInput(tt.input)
			if err != nil {
				t.Fatalf("ParseInput(%q) error = %v", tt.input, err)
			}
			if videoID != tt.videoID || channel != tt.channel {
				t.Errorf("ParseInput(%q) = (%q, %q), want (%q, %q)",
					tt.input, videoID, channel, tt.videoID, tt.channel)
			}
		})
	}
}

// TestParseInputInvalid rejects inputs that are neither YouTube video inputs
// nor channel references.
func TestParseInputInvalid(t *testing.T) {
	tests := []struct {
		name  string
		input string
	}{
		{name: "whitespace only", input: "   "},
		{name: "plain text", input: "not a youtube input"},
		{name: "eleven characters of text", input: "hello world"},
		{name: "other video host", input: "https://vimeo.com/12345"},
		{name: "scheme-less other host", input: "example.com/watch?v=x"},
		{name: "youtube root", input: "https://www.youtube.com/"},
		{name: "watch without v", input: "https://www.youtube.com/watch"},
		{name: "watch with short v", input: "https://www.youtube.com/watch?v=short"},
		{name: "live without id", input: "https://www.youtube.com/live"},
		{name: "channel without id", input: "https://www.youtube.com/channel/"},
		{name: "unknown youtube path", input: "https://www.youtube.com/results?search_query=x"},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			videoID, channel, err := ParseInput(tt.input)
			if err == nil {
				t.Fatalf("ParseInput(%q) = (%q, %q), want error", tt.input, videoID, channel)
			}
			if videoID != "" || channel != "" {
				t.Errorf("ParseInput(%q) on error = (%q, %q), want empty video and channel", tt.input, videoID, channel)
			}
		})
	}
}
