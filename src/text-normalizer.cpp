#include "text-normalizer.hpp"

#include <QRegularExpression>

namespace {
QString collapseRepeatedPunctuation(const QString &text)
{
    static const QRegularExpression repeated(QStringLiteral("([!?.,])\\1+"));
    QString result = text;
    result.replace(repeated, QStringLiteral("\\1"));
    return result;
}
} // namespace

QString TextNormalizer::normalize(const QString &text, const Settings &settings)
{
    QString result = text;

    if (settings.stripUrls) {
        static const QRegularExpression url(
            QStringLiteral("(?:https?://|www\\.)\\S+"),
            QRegularExpression::CaseInsensitiveOption);
        result.remove(url);
    }

    /* Emoji, variation selectors, keycaps, and zero-width joiners/spaces. */
    static const QRegularExpression emoji(QStringLiteral(
        "[\\x{1F000}-\\x{1FAFF}\\x{2600}-\\x{27BF}\\x{FE00}-\\x{FE0F}\\x{20E3}]"));
    result.remove(emoji);

    static const QRegularExpression zeroWidth(QStringLiteral("[\\x{200B}-\\x{200D}\\x{FEFF}]"));
    result.remove(zeroWidth);

    result = result.simplified();

    /* A leading @mention is addressing, not something to speak. */
    static const QRegularExpression leadingMention(QStringLiteral("^@\\S+\\s+"));
    result.remove(leadingMention);

    result = collapseRepeatedPunctuation(result);
    result = result.simplified();

    const int maxChars = qBound(1, settings.maxChars, 500);
    if (result.size() > maxChars) {
        QString cut = result.left(maxChars);
        const int lastSpace = cut.lastIndexOf(QLatin1Char(' '));
        if (lastSpace > 0)
            cut.truncate(lastSpace);
        result = cut.trimmed();
    }

    return result;
}

QString TextNormalizer::render(const ChatMessage &msg, const Settings &settings)
{
    const QString text = normalize(msg.text, settings);

    if (msg.kind == QLatin1String("superchat")) {
        return QStringLiteral("%1 sent %2: %3")
            .arg(msg.author, msg.amount, text);
    }

    if (msg.kind == QLatin1String("membership"))
        return QStringLiteral("%1 is now a member").arg(msg.author);

    if (!settings.readAuthor)
        return text;

    if (settings.authorTemplate.isEmpty())
        return text;

    QString rendered = settings.authorTemplate;
    rendered.replace(QStringLiteral("{author}"), msg.author);
    rendered.replace(QStringLiteral("{message}"), text);
    return rendered.trimmed();
}
