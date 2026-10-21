#pragma once

#include <QString>

#include "chat-message.hpp"
#include "settings.hpp"

/** Turns raw chat text into speakable text and renders a full utterance. */
class TextNormalizer {
public:
    /* Removes URLs (when enabled), emoji/zero-width characters and a leading
     * @mention, collapses whitespace and repeated punctuation, then truncates
     * to settings.maxChars at a word boundary where possible. */
    static QString normalize(const QString &text, const Settings &settings);

    /* Super Chats always render as "<author> sent <amount>: <message>",
     * memberships as "<author> is now a member".  Other messages use
     * settings.authorTemplate when readAuthor is enabled. */
    static QString render(const ChatMessage &msg, const Settings &settings);
};
