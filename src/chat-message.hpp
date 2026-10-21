#pragma once

#include <QMetaType>
#include <QString>

/* One live chat message as emitted by the helper's JSON Lines protocol. */
struct ChatMessage {
    QString id;
    QString author;
    QString authorId;
    QString text;
    QString publishedAt;
    QString kind;
    QString amount;
    bool isModerator = false;
    bool isOwner = false;
    bool isSponsor = false;
};

/* One decoded helper event: status, chat message, or error. */
struct ChatEvent {
    enum class Type { Status, Message, Error };

    Type type = Type::Status;
    QString state;
    QString detail;
    QString code;
    bool fatal = false;
    ChatMessage message;
};

Q_DECLARE_METATYPE(ChatEvent)

/* Configuration written as one compact JSON object to the helper's stdin. */
struct ChatConfig {
    QString helperPath;
    QString provider = QStringLiteral("auto");
    QString apiKey;
    QString video;
    QString hl = QStringLiteral("en");
};
