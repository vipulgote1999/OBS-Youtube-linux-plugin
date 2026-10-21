package youtube

import (
	"testing"

	"google.golang.org/protobuf/proto"
	streamlist "obs-youtube-chat-tts/helper/proto"
)

// TestMapOfficialMessageText maps an ordinary text message: the text comes
// from snippet.textMessageDetails and the author from authorDetails.
func TestMapOfficialMessageText(t *testing.T) {
	m := &streamlist.LiveChatMessage{
		Id: proto.String("msg-1"),
		Snippet: &streamlist.LiveChatMessageSnippet{
			PublishedAt: proto.String("2026-10-21T12:00:00.000Z"),
			DisplayedContent: &streamlist.LiveChatMessageSnippet_TextMessageDetails{
				TextMessageDetails: &streamlist.LiveChatTextMessageDetails{
					MessageText: proto.String("hello chat"),
				},
			},
		},
		AuthorDetails: &streamlist.LiveChatMessageAuthorDetails{
			ChannelId:   proto.String("UC-author"),
			DisplayName: proto.String("Alice"),
		},
	}

	got := mapOfficialMessage(m)
	if got.Kind != "text" {
		t.Errorf("Kind = %q, want text", got.Kind)
	}
	if got.Text != "hello chat" {
		t.Errorf("Text = %q, want %q", got.Text, "hello chat")
	}
	if got.Author != "Alice" {
		t.Errorf("Author = %q, want Alice", got.Author)
	}
	if got.ID != "msg-1" {
		t.Errorf("ID = %q, want msg-1", got.ID)
	}
	if got.AuthorID != "UC-author" {
		t.Errorf("AuthorID = %q, want UC-author", got.AuthorID)
	}
	if got.PublishedAt != "2026-10-21T12:00:00.000Z" {
		t.Errorf("PublishedAt = %q, want the snippet timestamp", got.PublishedAt)
	}
	if got.Amount != "" {
		t.Errorf("Amount = %q, want empty", got.Amount)
	}
	if got.IsModerator || got.IsOwner || got.IsSponsor {
		t.Errorf("flags = moderator:%v owner:%v sponsor:%v, want all false",
			got.IsModerator, got.IsOwner, got.IsSponsor)
	}
}

// TestMapOfficialMessageSuperChat maps a Super Chat: the kind changes, the
// amount comes from amountDisplayString, and the text is userComment.
func TestMapOfficialMessageSuperChat(t *testing.T) {
	m := &streamlist.LiveChatMessage{
		Id: proto.String("msg-2"),
		Snippet: &streamlist.LiveChatMessageSnippet{
			DisplayedContent: &streamlist.LiveChatMessageSnippet_SuperChatDetails{
				SuperChatDetails: &streamlist.LiveChatSuperChatDetails{
					AmountMicros:        proto.Uint64(5000000),
					Currency:            proto.String("USD"),
					AmountDisplayString: proto.String("$5.00"),
					UserComment:         proto.String("great stream"),
				},
			},
		},
		AuthorDetails: &streamlist.LiveChatMessageAuthorDetails{
			DisplayName: proto.String("Bob"),
		},
	}

	got := mapOfficialMessage(m)
	if got.Kind != "superchat" {
		t.Errorf("Kind = %q, want superchat", got.Kind)
	}
	if got.Amount != "$5.00" {
		t.Errorf("Amount = %q, want $5.00", got.Amount)
	}
	if got.Text != "great stream" {
		t.Errorf("Text = %q, want %q", got.Text, "great stream")
	}
	if got.Author != "Bob" {
		t.Errorf("Author = %q, want Bob", got.Author)
	}
}

// TestMapOfficialMessageNewMember maps a new-member event: the kind is
// membership, the author is flagged as a sponsor, and the text comes from the
// member-level display header when YouTube supplies one.
func TestMapOfficialMessageNewMember(t *testing.T) {
	m := &streamlist.LiveChatMessage{
		Id: proto.String("msg-3"),
		Snippet: &streamlist.LiveChatMessageSnippet{
			DisplayMessage: proto.String("Carol became a member"),
			DisplayedContent: &streamlist.LiveChatMessageSnippet_NewSponsorDetails{
				NewSponsorDetails: &streamlist.LiveChatNewSponsorDetails{
					MemberLevelName: proto.String("Gold"),
					IsUpgrade:       proto.Bool(false),
				},
			},
		},
		AuthorDetails: &streamlist.LiveChatMessageAuthorDetails{
			DisplayName: proto.String("Carol"),
		},
	}

	got := mapOfficialMessage(m)
	if got.Kind != "membership" {
		t.Errorf("Kind = %q, want membership", got.Kind)
	}
	if !got.IsSponsor {
		t.Error("IsSponsor = false, want true")
	}
	if got.Text != "Carol became a member" {
		t.Errorf("Text = %q, want the member-level header", got.Text)
	}
	if got.Author != "Carol" {
		t.Errorf("Author = %q, want Carol", got.Author)
	}
}

// TestMapOfficialMessageModeratorAndOwner checks the badge flags copied from
// authorDetails.
func TestMapOfficialMessageModeratorAndOwner(t *testing.T) {
	m := &streamlist.LiveChatMessage{
		Id: proto.String("msg-4"),
		Snippet: &streamlist.LiveChatMessageSnippet{
			DisplayedContent: &streamlist.LiveChatMessageSnippet_TextMessageDetails{
				TextMessageDetails: &streamlist.LiveChatTextMessageDetails{
					MessageText: proto.String("mods and owners"),
				},
			},
		},
		AuthorDetails: &streamlist.LiveChatMessageAuthorDetails{
			DisplayName:     proto.String("Dave"),
			IsChatModerator: proto.Bool(true),
			IsChatOwner:     proto.Bool(true),
			IsChatSponsor:   proto.Bool(true),
		},
	}

	got := mapOfficialMessage(m)
	if !got.IsModerator {
		t.Error("IsModerator = false, want true")
	}
	if !got.IsOwner {
		t.Error("IsOwner = false, want true")
	}
	if !got.IsSponsor {
		t.Error("IsSponsor = false, want true")
	}
	if got.Kind != "text" {
		t.Errorf("Kind = %q, want text", got.Kind)
	}
}
