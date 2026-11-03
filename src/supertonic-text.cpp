#include "supertonic-text.hpp"

#include <QRegularExpression>

namespace {
const QStringList &supportedLanguages()
{
    static const QStringList languages = {
        QStringLiteral("en"), QStringLiteral("ko"), QStringLiteral("ja"),
        QStringLiteral("ar"), QStringLiteral("bg"), QStringLiteral("cs"),
        QStringLiteral("da"), QStringLiteral("de"), QStringLiteral("el"),
        QStringLiteral("es"), QStringLiteral("et"), QStringLiteral("fi"),
        QStringLiteral("fr"), QStringLiteral("hi"), QStringLiteral("hr"),
        QStringLiteral("hu"), QStringLiteral("id"), QStringLiteral("it"),
        QStringLiteral("lt"), QStringLiteral("lv"), QStringLiteral("nl"),
        QStringLiteral("pl"), QStringLiteral("pt"), QStringLiteral("ro"),
        QStringLiteral("ru"), QStringLiteral("sk"), QStringLiteral("sl"),
        QStringLiteral("sv"), QStringLiteral("tr"), QStringLiteral("uk"),
        QStringLiteral("vi"), QStringLiteral("na"),
    };
    return languages;
}

QString normalizedLanguage(const QString &lang)
{
    const QString candidate = lang.trimmed().toLower();
    return supportedLanguages().contains(candidate) ? candidate : QStringLiteral("en");
}

bool endsWithTerminalPunctuation(const QString &text)
{
    static const QString ascii = QStringLiteral(".!?;:,'\")]}>");
    static const QStringList wide = {
        QStringLiteral("…"), QStringLiteral("。"), QStringLiteral("」"),
        QStringLiteral("』"), QStringLiteral("】"), QStringLiteral("〉"),
        QStringLiteral("》"), QStringLiteral("›"), QStringLiteral("»"),
        QStringLiteral("“"), QStringLiteral("”"), QStringLiteral("‘"),
        QStringLiteral("’"),
    };

    if (text.isEmpty())
        return false;
    if (ascii.contains(text.back()))
        return true;
    for (const QString &mark : wide) {
        if (text.endsWith(mark))
            return true;
    }
    return false;
}

void appendHardSplit(const QString &sentence, int maxLen, QStringList *chunks)
{
    int offset = 0;
    while (offset < sentence.size()) {
        int take = qMin(maxLen, sentence.size() - offset);
        if (offset + take < sentence.size()) {
            /* Prefer a word boundary so a chunk does not begin mid-word. */
            const int space = sentence.lastIndexOf(QLatin1Char(' '), offset + take - 1);
            if (space > offset)
                take = space - offset;
        }

        const QString piece = sentence.mid(offset, take).trimmed();
        if (!piece.isEmpty())
            chunks->append(piece);
        offset += take;
        while (offset < sentence.size() && sentence.at(offset).isSpace())
            ++offset;
    }
}
} // namespace

QString SupertonicText::preprocess(const QString &text, const QString &lang)
{
    if (text.trimmed().isEmpty())
        return QString();

    QString result = text;

    static const QList<QPair<QString, QString>> replacements = {
        {QString::fromUtf8("–"), QStringLiteral("-")},
        {QString::fromUtf8("‑"), QStringLiteral("-")},
        {QString::fromUtf8("—"), QStringLiteral("-")},
        {QStringLiteral("_"), QStringLiteral(" ")},
        {QString::fromUtf8("“"), QStringLiteral("\"")},
        {QString::fromUtf8("”"), QStringLiteral("\"")},
        {QString::fromUtf8("‘"), QStringLiteral("'")},
        {QString::fromUtf8("’"), QStringLiteral("'")},
        {QString::fromUtf8("´"), QStringLiteral("'")},
        {QStringLiteral("`"), QStringLiteral("'")},
        {QStringLiteral("["), QStringLiteral(" ")},
        {QStringLiteral("]"), QStringLiteral(" ")},
        {QStringLiteral("|"), QStringLiteral(" ")},
        {QStringLiteral("/"), QStringLiteral(" ")},
        {QStringLiteral("#"), QStringLiteral(" ")},
        {QString::fromUtf8("→"), QStringLiteral(" ")},
        {QString::fromUtf8("←"), QStringLiteral(" ")},
    };
    for (const auto &replacement : replacements)
        result.replace(replacement.first, replacement.second);

    /* Remove emoji before the final whitespace collapse.  The ranges cover
     * the pictographic planes and the symbol blocks the model has no
     * pronunciation for. */
    static const QRegularExpression emoji(
        QStringLiteral("[\\x{1F000}-\\x{1FAFF}\\x{2600}-\\x{27BF}\\x{2B00}-\\x{2BFF}"
                       "\\x{FE00}-\\x{FE0F}\\x{200D}]"));
    result.remove(emoji);

    static const QStringList specialSymbols = {
        QString::fromUtf8("♥"), QString::fromUtf8("☆"), QString::fromUtf8("♡"),
        QString::fromUtf8("©"), QStringLiteral("\\"),
    };
    for (const QString &symbol : specialSymbols)
        result.remove(symbol);

    static const QList<QPair<QString, QString>> expressions = {
        {QStringLiteral("@"), QStringLiteral(" at ")},
        {QStringLiteral("e.g.,"), QStringLiteral("for example, ")},
        {QStringLiteral("i.e.,"), QStringLiteral("that is, ")},
    };
    for (const auto &expression : expressions)
        result.replace(expression.first, expression.second);

    /* Remove spaces before punctuation (the model sees punctuation as its
     * own token) and collapse repeated quotes. */
    static const QRegularExpression spaceBeforePunctuation(
        QStringLiteral("\\s+([,.!?;:])"));
    result.replace(spaceBeforePunctuation, QStringLiteral("\\1"));

    static const QRegularExpression duplicateQuotes(QStringLiteral("\"{2,}"));
    static const QRegularExpression duplicateApostrophes(QStringLiteral("'{2,}"));
    static const QRegularExpression duplicateBackticks(QStringLiteral("`{2,}"));
    result.replace(duplicateQuotes, QStringLiteral("\""));
    result.replace(duplicateApostrophes, QStringLiteral("'"));
    result.replace(duplicateBackticks, QStringLiteral("`"));

    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    result.replace(whitespace, QStringLiteral(" "));
    result = result.trimmed();

    if (!endsWithTerminalPunctuation(result))
        result += QLatin1Char('.');

    const QString tag = normalizedLanguage(lang);
    return QStringLiteral("<%1>%2</%1>").arg(tag, result);
}

QStringList SupertonicText::chunk(const QString &text, int maxLen)
{
    QStringList chunks;
    if (text.trimmed().isEmpty())
        return chunks;

    const int limit = qMax(1, maxLen);

    static const QRegularExpression paragraphSeparator(QStringLiteral("\\n\\s*\\n+"));
    static const QRegularExpression sentenceSeparator(QStringLiteral("(?<=[.!?])\\s+"));

    const QStringList paragraphs = text.split(paragraphSeparator);
    for (const QString &paragraph : paragraphs) {
        const QString trimmed = paragraph.trimmed();
        if (trimmed.isEmpty())
            continue;

        QString current;
        const QStringList sentences = trimmed.split(sentenceSeparator);
        for (const QString &raw : sentences) {
            const QString sentence = raw.trimmed();
            if (sentence.isEmpty())
                continue;

            if (sentence.size() > limit) {
                if (!current.isEmpty()) {
                    chunks.append(current);
                    current.clear();
                }
                appendHardSplit(sentence, limit, &chunks);
                continue;
            }

            if (current.isEmpty()) {
                current = sentence;
            } else if (current.size() + 1 + sentence.size() <= limit) {
                current += QLatin1Char(' ') + sentence;
            } else {
                chunks.append(current);
                current = sentence;
            }
        }

        if (!current.isEmpty()) {
            chunks.append(current);
            current.clear();
        }
    }

    /* A pathological input could still produce an overlong chunk; never hand
     * one to the model. */
    QStringList bounded;
    for (const QString &chunk : chunks) {
        if (chunk.size() <= limit)
            bounded.append(chunk);
        else
            appendHardSplit(chunk, limit, &bounded);
    }

    return bounded;
}

std::vector<uint16_t> SupertonicText::unicodeValues(const QString &text)
{
    const QString normalized = text.normalized(QString::NormalizationForm_KD);

    std::vector<uint16_t> values;
    values.reserve(size_t(normalized.size()));

    for (int i = 0; i < normalized.size(); ++i) {
        const QChar unit = normalized.at(i);
        if (unit.isHighSurrogate() && i + 1 < normalized.size() &&
            normalized.at(i + 1).isLowSurrogate()) {
            /* The indexer is indexed by UTF-16 unit; astral characters have
             * no pronunciation and are dropped rather than mapped to half a
             * surrogate pair. */
            ++i;
            continue;
        }
        values.push_back(uint16_t(unit.unicode()));
    }

    return values;
}
