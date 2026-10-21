#include <QtTest/QtTest>

#include "chat-message.hpp"
#include "settings.hpp"
#include "text-normalizer.hpp"

class TestTextNormalizer : public QObject {
    Q_OBJECT

private slots:
    void TestNormalizeStripsUrlEmojiAndTruncates();
    void rendersAuthorAndTemplates();
};

void TestTextNormalizer::TestNormalizeStripsUrlEmojiAndTruncates()
{
    Settings settings;
    settings.stripUrls = true;
    settings.readAuthor = true;

    QCOMPARE(TextNormalizer::normalize(QStringLiteral("Hello https://example.com world"), settings),
             QStringLiteral("Hello world"));
    QCOMPARE(TextNormalizer::normalize(QStringLiteral("Hello 😀🎉"), settings),
             QStringLiteral("Hello"));
    QCOMPARE(TextNormalizer::normalize(QStringLiteral("@alice hello"), settings),
             QStringLiteral("hello"));
    QCOMPARE(TextNormalizer::normalize(QStringLiteral("a     b"), settings), QStringLiteral("a b"));
    QCOMPARE(TextNormalizer::normalize(QStringLiteral("!!!!!!!!"), settings), QStringLiteral("!"));

    /* Exactly 300 characters: 60 "word " groups. */
    const QString longText = QStringLiteral("word ").repeated(60);
    settings.maxChars = 200;
    const QString truncated = TextNormalizer::normalize(longText, settings);
    QVERIFY(truncated.size() <= 200);
    QVERIFY(!truncated.endsWith(QLatin1Char(' ')));
    QVERIFY(longText.startsWith(truncated));
    QVERIFY(truncated.endsWith(QStringLiteral("word")));

    QCOMPARE(TextNormalizer::normalize(QStringLiteral("😀😀😀"), settings), QString());

    settings.stripUrls = false;
    QCOMPARE(TextNormalizer::normalize(QStringLiteral("see https://example.com now"), settings),
             QStringLiteral("see https://example.com now"));
}

void TestTextNormalizer::rendersAuthorAndTemplates()
{
    Settings settings;
    ChatMessage message;
    message.author = QStringLiteral("Alice");
    message.text = QStringLiteral("hello   https://example.com there");

    QCOMPARE(TextNormalizer::render(message, settings),
             QStringLiteral("Alice says: hello there"));

    settings.readAuthor = false;
    QCOMPARE(TextNormalizer::render(message, settings), QStringLiteral("hello there"));

    settings.readAuthor = true;
    settings.authorTemplate = QStringLiteral("[{author}] {message}");
    QCOMPARE(TextNormalizer::render(message, settings),
             QStringLiteral("[Alice] hello there"));

    message.kind = QStringLiteral("superchat");
    message.amount = QStringLiteral("$5.00");
    QCOMPARE(TextNormalizer::render(message, settings),
             QStringLiteral("Alice sent $5.00: hello there"));

    message.kind = QStringLiteral("membership");
    message.text.clear();
    QCOMPARE(TextNormalizer::render(message, settings),
             QStringLiteral("Alice is now a member"));
}

QTEST_GUILESS_MAIN(TestTextNormalizer)

#include "test_text_normalizer.moc"
