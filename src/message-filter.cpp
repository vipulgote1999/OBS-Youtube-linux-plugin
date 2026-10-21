#include "message-filter.hpp"

#include "text-normalizer.hpp"

#include <QRegularExpression>
#include <QStringList>

namespace {
constexpr int kMaxSeenIds = 10000;
constexpr int kMaxTrackedUsers = 2000;
constexpr qint64 kUserPruneMs = 10 * 60 * 1000;

bool matchesBlocklist(const QString &text, const QString &blocklist)
{
    const QStringList entries =
        blocklist.split(QRegularExpression(QStringLiteral("[\\r\\n,]")), Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
        const QString phrase = entry.trimmed();
        if (!phrase.isEmpty() && text.contains(phrase, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

FilterDecision reject(const QString &reason)
{
    FilterDecision decision;
    decision.accept = false;
    decision.reason = reason;
    return decision;
}
} // namespace

FilterDecision MessageFilter::evaluate(const ChatMessage &msg, const Settings &settings, qint64 nowMs)
{
    if (!msg.id.isEmpty() && seenIds_.contains(msg.id))
        return reject(QStringLiteral("duplicate-id"));

    const QString trimmedText = msg.text.trimmed();
    if (settings.skipCommands && !settings.commandPrefix.isEmpty() &&
        trimmedText.startsWith(settings.commandPrefix)) {
        return reject(QStringLiteral("command"));
    }

    if (msg.kind == QLatin1String("superchat") && !settings.speakSuperchats)
        return reject(QStringLiteral("superchat-disabled"));
    if (msg.kind == QLatin1String("membership") && !settings.speakMemberships)
        return reject(QStringLiteral("membership-disabled"));

    if (msg.isModerator && !settings.readModerators)
        return reject(QStringLiteral("moderator-disabled"));

    if (settings.skipBlocked && !settings.blocklist.trimmed().isEmpty() &&
        matchesBlocklist(msg.text, settings.blocklist)) {
        return reject(QStringLiteral("blocklisted"));
    }

    const QString normalized = TextNormalizer::normalize(msg.text, settings);
    /* A membership announcement has no chat text by design; its utterance is
     * synthesized from the author name. */
    if (normalized.isEmpty() && msg.kind != QLatin1String("membership"))
        return reject(QStringLiteral("empty-message"));

    const QString userKey = msg.authorId.isEmpty() ? msg.author : msg.authorId;
    if (settings.userCooldownSec > 0 && !userKey.isEmpty()) {
        const auto it = lastUserMs_.constFind(userKey);
        if (it != lastUserMs_.constEnd()) {
            const qint64 elapsed = nowMs - it.value();
            if (elapsed >= 0 && elapsed < qint64(settings.userCooldownSec) * 1000)
                return reject(QStringLiteral("user-cooldown"));
        }
    }

    if (settings.globalCooldownSec > 0 && hasGlobal_) {
        const qint64 elapsed = nowMs - lastGlobalMs_;
        if (elapsed >= 0 && elapsed < qint64(settings.globalCooldownSec) * 1000)
            return reject(QStringLiteral("global-cooldown"));
    }

    if (!userKey.isEmpty())
        lastUserMs_.insert(userKey, nowMs);

    if (!msg.id.isEmpty()) {
        seenIds_.insert(msg.id);
        seenOrder_.enqueue(msg.id);
        while (seenOrder_.size() > kMaxSeenIds) {
            const QString evicted = seenOrder_.dequeue();
            seenIds_.remove(evicted);
        }
    }

    hasGlobal_ = true;
    lastGlobalMs_ = nowMs;

    if (lastUserMs_.size() > kMaxTrackedUsers) {
        for (auto it = lastUserMs_.begin(); it != lastUserMs_.end();) {
            if (nowMs - it.value() > kUserPruneMs)
                it = lastUserMs_.erase(it);
            else
                ++it;
        }
    }

    FilterDecision decision;
    decision.accept = true;
    if (msg.kind == QLatin1String("superchat"))
        decision.priority = 10;
    else if (msg.kind == QLatin1String("membership"))
        decision.priority = 5;
    return decision;
}

void MessageFilter::reset()
{
    lastUserMs_.clear();
    seenIds_.clear();
    seenOrder_.clear();
    hasGlobal_ = false;
    lastGlobalMs_ = 0;
}
