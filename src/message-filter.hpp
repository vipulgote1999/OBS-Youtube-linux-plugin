#pragma once

#include <QHash>
#include <QQueue>
#include <QSet>
#include <QString>

#include "chat-message.hpp"
#include "settings.hpp"

struct FilterDecision {
    bool accept = false;
    int priority = 0;
    QString reason;
};

/**
 * Stateful per-stream filter: duplicate IDs, command prefixes, disabled
 * message kinds/moderators, the blocklist, empty normalized text, and the
 * per-user/global cooldowns.
 *
 * seenIds_ is capped at 10,000 IDs (FIFO) and lastUserMs_ is pruned after
 * ten minutes once it exceeds 2,000 users, so long streams do not grow
 * without bound.
 */
class MessageFilter {
public:
    FilterDecision evaluate(const ChatMessage &msg, const Settings &settings, qint64 nowMs);
    void reset();

private:
    QHash<QString, qint64> lastUserMs_;
    QSet<QString> seenIds_;
    QQueue<QString> seenOrder_;
    bool hasGlobal_ = false;
    qint64 lastGlobalMs_ = 0;
};
