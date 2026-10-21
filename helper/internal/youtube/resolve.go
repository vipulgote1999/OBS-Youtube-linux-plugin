// Package youtube resolves user input — video URLs, bare video IDs, channel
// URLs — into the video or channel reference the chat providers consume. It
// performs no network access and does not decide whether a video is live,
// past, or upcoming; that is the provider's job.
package youtube

import (
	"errors"
	"fmt"
	"net/url"
	"strings"
)

// videoIDLen is the length of a YouTube video ID.
const videoIDLen = 11

// ParseInput resolves a raw user input to a video ID or a channel reference.
//
// It returns (videoID, "", nil) for direct video URLs and bare 11-character
// video IDs, or ("", channel, nil) for channel references: "@handle" for
// handle URLs and bare handles, the channel ID for /channel/UC... URLs, and
// "c/name" / "user/name" for the legacy custom-name forms. An error is
// returned for empty input and for anything that is not a YouTube URL,
// video ID, or channel reference.
func ParseInput(input string) (videoID, channel string, err error) {
	s := strings.TrimSpace(input)
	if s == "" {
		return "", "", errors.New("empty input")
	}

	u, parseErr := url.Parse(s)
	if parseErr != nil {
		return "", "", fmt.Errorf("invalid YouTube input: %w", parseErr)
	}

	// Without a host the input is a bare video ID, a bare @handle, or a URL
	// missing its https:// scheme.
	if u.Host == "" {
		if isVideoID(s) {
			return s, "", nil
		}
		if isBareHandle(s) {
			return "", s, nil
		}
		if schemelessHost(s) != "" {
			u, parseErr = url.Parse("https://" + s)
			if parseErr != nil {
				return "", "", fmt.Errorf("invalid YouTube input: %w", parseErr)
			}
		}
	}
	if u.Host == "" {
		return "", "", fmt.Errorf("unrecognized YouTube input %q", s)
	}

	host := u.Hostname()
	if !isYouTubeHost(host) {
		return "", "", fmt.Errorf("not a YouTube URL: host %q", host)
	}
	if isShortLinkHost(host) {
		return parseShortLinkURL(u)
	}
	return parseYouTubeURL(u)
}

// parseShortLinkURL extracts the video ID from a youtu.be/<id> URL.
func parseShortLinkURL(u *url.URL) (videoID, channel string, err error) {
	segs := pathSegments(u.Path)
	if len(segs) == 0 {
		return "", "", errors.New("no video ID in youtu.be URL")
	}
	if strings.HasPrefix(segs[0], "@") {
		return "", segs[0], nil
	}
	if isVideoID(segs[0]) {
		return segs[0], "", nil
	}
	return "", "", fmt.Errorf("invalid video ID in youtu.be URL: %q", segs[0])
}

// parseYouTubeURL extracts the video ID or channel reference from a
// youtube.com URL: the v query parameter, /live/<id>, /shorts/<id>,
// /embed/<id>, @handle, /channel/<id>, /c/<name>, or /user/<name>.
func parseYouTubeURL(u *url.URL) (videoID, channel string, err error) {
	segs := pathSegments(u.Path)
	if len(segs) == 0 || segs[0] == "watch" {
		if v := u.Query().Get("v"); isVideoID(v) {
			return v, "", nil
		}
		if len(segs) == 0 {
			return "", "", errors.New("no video or channel in YouTube URL")
		}
		return "", "", errors.New("no video ID in YouTube watch URL")
	}

	switch segs[0] {
	case "live", "shorts", "embed":
		if len(segs) < 2 || !isVideoID(segs[1]) {
			return "", "", fmt.Errorf("invalid video ID in YouTube /%s URL", segs[0])
		}
		return segs[1], "", nil
	case "channel":
		if len(segs) < 2 || segs[1] == "" {
			return "", "", errors.New("no channel ID in YouTube URL")
		}
		return "", segs[1], nil
	case "c", "user":
		if len(segs) < 2 || segs[1] == "" {
			return "", "", fmt.Errorf("no channel name in YouTube /%s URL", segs[0])
		}
		return "", segs[0] + "/" + segs[1], nil
	}

	if strings.HasPrefix(segs[0], "@") {
		return "", segs[0], nil
	}
	return "", "", fmt.Errorf("unrecognized YouTube URL path %q", u.Path)
}

// isYouTubeHost reports whether host serves YouTube pages, accepting the
// youtube.com, youtu.be, and youtube-nocookie.com domains and their
// subdomains.
func isYouTubeHost(host string) bool {
	h := strings.ToLower(host)
	switch h {
	case "youtube.com", "youtube-nocookie.com", "youtu.be":
		return true
	}
	return strings.HasSuffix(h, ".youtube.com") ||
		strings.HasSuffix(h, ".youtube-nocookie.com") ||
		strings.HasSuffix(h, ".youtu.be")
}

// isShortLinkHost reports whether host is a youtu.be short-link domain.
func isShortLinkHost(host string) bool {
	h := strings.ToLower(host)
	return h == "youtu.be" || strings.HasSuffix(h, ".youtu.be")
}

// isVideoID reports whether s looks like a YouTube video ID: exactly 11
// characters from the ID alphabet.
func isVideoID(s string) bool {
	if len(s) != videoIDLen {
		return false
	}
	for _, r := range s {
		switch {
		case r >= '0' && r <= '9':
		case r >= 'a' && r <= 'z':
		case r >= 'A' && r <= 'Z':
		case r == '_':
		case r == '-':
		default:
			return false
		}
	}
	return true
}

// isBareHandle reports whether s is a bare @handle channel reference such as
// "@SomeChannel", with no URL structure at all.
func isBareHandle(s string) bool {
	return strings.HasPrefix(s, "@") && len(s) > 1 && !strings.ContainsAny(s, "/?# \t")
}

// schemelessHost returns the leading host-like token of a scheme-less input
// ("youtu.be/<id>" → "youtu.be"), or "" when the input does not start with
// something that looks like a host name.
func schemelessHost(s string) string {
	candidate := s
	if end := strings.IndexAny(s, "/?#"); end >= 0 {
		candidate = s[:end]
	}
	if candidate == "" || !strings.Contains(candidate, ".") || strings.ContainsAny(candidate, " \t") {
		return ""
	}
	return candidate
}

// pathSegments splits a URL path into its non-empty segments.
func pathSegments(p string) []string {
	if p == "" || p == "/" {
		return nil
	}
	return strings.Split(strings.Trim(p, "/"), "/")
}
