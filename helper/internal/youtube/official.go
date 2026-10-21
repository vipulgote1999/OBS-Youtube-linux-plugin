package youtube

import (
	"context"
	"crypto/tls"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/credentials"
	"google.golang.org/grpc/metadata"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"

	"obs-youtube-chat-tts/helper/internal/protocol"
	streamlist "obs-youtube-chat-tts/helper/proto"
)

const (
	// defaultGRPCEndpoint is the YouTube live-chat streaming service.
	defaultGRPCEndpoint = "youtube.googleapis.com:443"

	// userAgent identifies this helper to both REST and gRPC endpoints.
	userAgent = "ytchat-helper/0.1"

	// maxRecvMsgSize bounds one streamed response. A burst of Super Chats can
	// be large, so 8 MiB rather than gRPC's 4 MiB default.
	maxRecvMsgSize = 8 * 1024 * 1024
)

// streamBackoff is the reconnect delay sequence: 1, 2, 4, 8, 16, then 30
// seconds, holding at 30.
var streamBackoff = []time.Duration{
	1 * time.Second,
	2 * time.Second,
	4 * time.Second,
	8 * time.Second,
	16 * time.Second,
	30 * time.Second,
}

// maxPreConnectAttempts bounds the connection attempts made before the first
// successful gRPC response: the initial attempt plus two retries. Running out
// of attempts returns a non-fatal network error instead of looping forever, so
// the caller can surface it and auto can fall back to InnerTube.
const maxPreConnectAttempts = 3

// OfficialProvider streams chat through YouTube's official Data API and the
// youtube.api.v3.V3DataLiveChatMessageService gRPC service. Empty fields use
// production defaults; tests override APIBaseURL with an httptest server.
type OfficialProvider struct {
	HTTP         *http.Client
	APIBaseURL   string
	GRPCEndpoint string
}

// Run resolves cfg.Video to a live chat and streams messages until the chat
// ends or ctx is cancelled. The connected status is emitted by stream only
// after the gRPC connection is confirmed.
func (p *OfficialProvider) Run(ctx context.Context, cfg Config, emit func(protocol.Event)) error {
	_, liveChatID, err := p.resolveLiveChatID(ctx, cfg)
	if err != nil {
		return err
	}

	return p.stream(ctx, cfg, liveChatID, emit)
}

// endpoint returns the configured gRPC endpoint or the production default.
func (p *OfficialProvider) endpoint() string {
	if p.GRPCEndpoint != "" {
		return p.GRPCEndpoint
	}
	return defaultGRPCEndpoint
}

// stream dials the streaming service and receives chat messages. It emits
// connected exactly once, after the first successful gRPC response headers.
// Before that point it gives up after maxPreConnectAttempts; afterwards it
// reconnects with backoff indefinitely.
func (p *OfficialProvider) stream(ctx context.Context, cfg Config, liveChatID string, emit func(protocol.Event)) error {
	conn, err := grpc.NewClient(
		p.endpoint(),
		grpc.WithTransportCredentials(credentials.NewTLS(&tls.Config{MinVersion: tls.VersionTLS12})),
		grpc.WithUserAgent(userAgent),
		grpc.WithDefaultCallOptions(grpc.MaxCallRecvMsgSize(maxRecvMsgSize)),
	)
	if err != nil {
		return &Error{
			Code:    CodeNetwork,
			Message: redactSecrets(fmt.Sprintf("could not create the YouTube chat connection: %v", err)),
			Fatal:   true,
		}
	}
	defer conn.Close()

	client := streamlist.NewV3DataLiveChatMessageServiceClient(conn)
	pageToken := ""
	reconnectAttempt := 0
	preConnectFailures := 0
	connected := false

	for {
		if ctx.Err() != nil {
			return nil
		}

		req := &streamlist.LiveChatMessageListRequest{
			Part:       []string{"snippet", "authorDetails"},
			LiveChatId: proto.String(liveChatID),
		}
		if cfg.HL != "" {
			req.Hl = proto.String(cfg.HL)
		}
		if pageToken != "" {
			req.PageToken = proto.String(pageToken)
		}

		// The key travels as gRPC metadata, never in a logged URL.
		callCtx := metadata.AppendToOutgoingContext(ctx, "x-goog-api-key", cfg.APIKey)
		stream, err := client.StreamList(callCtx, req)
		if err == nil {
			// Header blocks until the server responds, which is the first
			// proof that the transport connection actually works. NewClient
			// itself is lazy.
			_, err = stream.Header()
		}
		if err != nil {
			if ctx.Err() != nil {
				return nil
			}
			if fatal := classifyGRPCError(err); fatal != nil {
				return fatal
			}
			if !connected {
				preConnectFailures++
				if preConnectFailures >= maxPreConnectAttempts {
					return &Error{
						Code: CodeNetwork,
						Message: redactSecrets(fmt.Sprintf(
							"could not connect to the YouTube chat stream after %d attempts: %v",
							preConnectFailures, err)),
						Fatal: false,
					}
				}
				reconnectAttempt = p.waitBeforeRetry(ctx, preConnectFailures-1, emit)
				continue
			}
			reconnectAttempt = p.waitBeforeRetry(ctx, reconnectAttempt, emit)
			continue
		}

		if !connected {
			connected = true
			emit(protocol.Event{Type: "status", State: "connected", Detail: "official"})
		}

		received, ended, err := p.receive(stream, emit, &pageToken)
		if ended {
			emit(protocol.Event{Type: "status", State: "ended"})
			return nil
		}
		if err != nil {
			if ctx.Err() != nil {
				return nil
			}
			if fatal := classifyGRPCError(err); fatal != nil {
				return fatal
			}
		}
		if received {
			reconnectAttempt = 0
		}
		if ctx.Err() != nil {
			return nil
		}
		reconnectAttempt = p.waitBeforeRetry(ctx, reconnectAttempt, emit)
	}
}

// receive drains one StreamList response stream. It reports whether any
// response arrived, whether the chat ended, and the error that broke the
// stream.
func (p *OfficialProvider) receive(
	stream streamlist.V3DataLiveChatMessageService_StreamListClient,
	emit func(protocol.Event),
	pageToken *string,
) (received, ended bool, err error) {
	for {
		resp, err := stream.Recv()
		if err == io.EOF {
			return received, false, nil
		}
		if err != nil {
			return received, false, err
		}
		received = true

		if resp.GetOfflineAt() != "" {
			return received, true, nil
		}
		for _, item := range resp.GetItems() {
			if item.GetSnippet().GetType() == streamlist.LiveChatMessageSnippet_TypeWrapper_CHAT_ENDED_EVENT {
				return received, true, nil
			}
			if isSilentMessage(item) {
				continue
			}
			msg := mapOfficialMessage(item)
			if msg.Kind == "text" && msg.Text == "" {
				continue
			}
			emit(protocol.Event{Type: "message", Message: &msg})
		}
		if token := resp.GetNextPageToken(); token != "" {
			*pageToken = token
		}
	}
}

// waitBeforeRetry emits a reconnecting status, sleeps for the backoff delay,
// and returns the next attempt index. It returns as soon as ctx ends.
func (p *OfficialProvider) waitBeforeRetry(ctx context.Context, attempt int, emit func(protocol.Event)) int {
	delay := streamBackoff[len(streamBackoff)-1]
	if attempt < len(streamBackoff) {
		delay = streamBackoff[attempt]
	}
	emit(protocol.Event{
		Type:   "status",
		State:  "reconnecting",
		Detail: fmt.Sprintf("chat stream interrupted; retrying in %s", delay),
	})

	timer := time.NewTimer(delay)
	defer timer.Stop()
	select {
	case <-ctx.Done():
	case <-timer.C:
	}
	return attempt + 1
}

// classifyGRPCError converts a permanent gRPC failure into an Error. It
// returns nil for failures worth retrying.
func classifyGRPCError(err error) *Error {
	st, ok := status.FromError(err)
	if !ok {
		return nil
	}
	msg := redactSecrets(st.Message())
	if msg == "" {
		msg = "YouTube chat stream failed"
	}
	switch st.Code() {
	case codes.Unauthenticated:
		return &Error{Code: CodeAuth, Message: msg, Fatal: true}
	case codes.PermissionDenied:
		if strings.Contains(strings.ToLower(st.Message()), "quota") {
			return &Error{Code: CodeQuota, Message: msg, Fatal: true}
		}
		return &Error{Code: CodeAuth, Message: msg, Fatal: true}
	case codes.ResourceExhausted:
		return &Error{Code: CodeQuota, Message: msg, Fatal: true}
	case codes.NotFound, codes.FailedPrecondition:
		return &Error{Code: CodeChatDisabled, Message: msg, Fatal: true}
	case codes.InvalidArgument:
		return &Error{Code: CodeParse, Message: msg, Fatal: true}
	}
	return nil
}

// isSilentMessage reports whether a message carries no spoken content: chat
// tombstones, moderation actions, sponsor-only mode toggles, and polls.
func isSilentMessage(m *streamlist.LiveChatMessage) bool {
	switch m.GetSnippet().GetType() {
	case streamlist.LiveChatMessageSnippet_TypeWrapper_TOMBSTONE,
		streamlist.LiveChatMessageSnippet_TypeWrapper_USER_BANNED_EVENT,
		streamlist.LiveChatMessageSnippet_TypeWrapper_SPONSOR_ONLY_MODE_STARTED_EVENT,
		streamlist.LiveChatMessageSnippet_TypeWrapper_SPONSOR_ONLY_MODE_ENDED_EVENT,
		streamlist.LiveChatMessageSnippet_TypeWrapper_POLL_EVENT:
		return true
	}
	return false
}

// mapOfficialMessage converts one YouTube live chat message to the wire
// message shape. Text messages carry their body, Super Chats add the rendered
// amount, and membership events are flagged as sponsors.
func mapOfficialMessage(m *streamlist.LiveChatMessage) protocol.ChatMessage {
	snippet := m.GetSnippet()
	author := m.GetAuthorDetails()
	s := &protocol.ChatMessage{
		ID:          m.GetId(),
		Author:      author.GetDisplayName(),
		AuthorID:    author.GetChannelId(),
		PublishedAt: snippet.GetPublishedAt(),
		Kind:        "text",
		IsModerator: author.GetIsChatModerator(),
		IsOwner:     author.GetIsChatOwner(),
		IsSponsor:   author.GetIsChatSponsor(),
	}
	if s.AuthorID == "" {
		s.AuthorID = snippet.GetAuthorChannelId()
	}

	switch details := snippet.GetDisplayedContent().(type) {
	case *streamlist.LiveChatMessageSnippet_TextMessageDetails:
		s.Text = details.TextMessageDetails.GetMessageText()

	case *streamlist.LiveChatMessageSnippet_SuperChatDetails:
		s.Kind = "superchat"
		s.Amount = details.SuperChatDetails.GetAmountDisplayString()
		s.Text = details.SuperChatDetails.GetUserComment()

	case *streamlist.LiveChatMessageSnippet_SuperStickerDetails:
		s.Kind = "superchat"
		s.Amount = details.SuperStickerDetails.GetAmountDisplayString()
		s.Text = snippet.GetDisplayMessage()

	case *streamlist.LiveChatMessageSnippet_NewSponsorDetails:
		s.Kind = "membership"
		s.IsSponsor = true
		s.Text = membershipText(snippet.GetDisplayMessage(), s.Author,
			"became a member", details.NewSponsorDetails.GetMemberLevelName())

	case *streamlist.LiveChatMessageSnippet_MemberMilestoneChatDetails:
		s.Kind = "membership"
		s.IsSponsor = true
		s.Text = details.MemberMilestoneChatDetails.GetUserComment()
		if s.Text == "" {
			s.Text = membershipText(snippet.GetDisplayMessage(), s.Author,
				"reached a membership milestone", details.MemberMilestoneChatDetails.GetMemberLevelName())
		}

	case *streamlist.LiveChatMessageSnippet_MembershipGiftingDetails:
		s.Kind = "membership"
		s.IsSponsor = true
		s.Text = snippet.GetDisplayMessage()
		if s.Text == "" {
			s.Text = fmt.Sprintf("%s gifted %d memberships", s.Author,
				details.MembershipGiftingDetails.GetGiftMembershipsCount())
		}

	case *streamlist.LiveChatMessageSnippet_GiftMembershipReceivedDetails:
		s.Kind = "membership"
		s.IsSponsor = true
		s.Text = membershipText(snippet.GetDisplayMessage(), s.Author,
			"received a gift membership", details.GiftMembershipReceivedDetails.GetMemberLevelName())

	default:
		s.Text = snippet.GetDisplayMessage()
	}

	return *s
}

// membershipText prefers YouTube's localized display header. When it is
// absent it builds a short fallback from the author, event, and level.
func membershipText(header, author, event, level string) string {
	if header != "" {
		return header
	}
	if author == "" {
		author = "A viewer"
	}
	if level != "" {
		return fmt.Sprintf("%s %s (%s)", author, event, level)
	}
	return fmt.Sprintf("%s %s", author, event)
}
