#include <QtTest/QtTest>

#include <vector>

#include "supertonic-text.hpp"

class TestSupertonicText : public QObject {
    Q_OBJECT

private slots:
    void wrapsTextWithLanguageTagsAndAddsPeriod();
    void keepsExistingTerminalPunctuation();
    void replacesDashesQuotesAndSymbols();
    void removesEmojiAndSpecialSymbols();
    void expandsKnownExpressions();
    void fallsBackToEnglishForUnknownLanguage();
    void decomposesAccentedText();
    void preservesCjkCharacters();
    void chunksShortSentencesTogether();
    void splitsLongTextUnderLimit();
    void hardSplitsOverlongSentence();
    void handlesEmptyInput();
};

void TestSupertonicText::wrapsTextWithLanguageTagsAndAddsPeriod()
{
    QCOMPARE(SupertonicText::preprocess(QStringLiteral("Hello world"), QStringLiteral("en")),
             QStringLiteral("<en>Hello world.</en>"));
}

void TestSupertonicText::keepsExistingTerminalPunctuation()
{
    QCOMPARE(SupertonicText::preprocess(QStringLiteral("Hello!"), QStringLiteral("en")),
             QStringLiteral("<en>Hello!</en>"));
    QCOMPARE(SupertonicText::preprocess(QStringLiteral("He said \"hi\""), QStringLiteral("en")),
             QStringLiteral("<en>He said \"hi\"</en>"));
    QCOMPARE(SupertonicText::preprocess(QString::fromUtf8("你好。"), QStringLiteral("ja")),
             QString::fromUtf8("<ja>你好。</ja>"));
}

void TestSupertonicText::replacesDashesQuotesAndSymbols()
{
    const QString input = QString::fromUtf8("a–b — c_d “x” ‘y’ [z] | / #");
    QCOMPARE(SupertonicText::preprocess(input, QStringLiteral("en")),
             QStringLiteral("<en>a-b - c d \"x\" 'y' z.</en>"));
}

void TestSupertonicText::removesEmojiAndSpecialSymbols()
{
    const QString input = QString::fromUtf8("Hi 😀 ♥ there");
    QCOMPARE(SupertonicText::preprocess(input, QStringLiteral("en")),
             QStringLiteral("<en>Hi there.</en>"));
}

void TestSupertonicText::expandsKnownExpressions()
{
    QCOMPARE(SupertonicText::preprocess(QStringLiteral("meet @ 5 e.g., now"),
                                        QStringLiteral("en")),
             QStringLiteral("<en>meet at 5 for example, now.</en>"));
}

void TestSupertonicText::fallsBackToEnglishForUnknownLanguage()
{
    QCOMPARE(SupertonicText::preprocess(QStringLiteral("Hello"), QStringLiteral("xx")),
             QStringLiteral("<en>Hello.</en>"));
}

void TestSupertonicText::decomposesAccentedText()
{
    const std::vector<uint16_t> values = SupertonicText::unicodeValues(QString::fromUtf8("café"));
    const std::vector<uint16_t> expected = {0x0063, 0x0061, 0x0066, 0x0065, 0x0301};
    QCOMPARE(values, expected);
}

void TestSupertonicText::preservesCjkCharacters()
{
    const std::vector<uint16_t> values =
        SupertonicText::unicodeValues(QString::fromUtf8("你好"));
    const std::vector<uint16_t> expected = {0x4F60, 0x597D};
    QCOMPARE(values, expected);
}

void TestSupertonicText::chunksShortSentencesTogether()
{
    const QStringList chunks =
        SupertonicText::chunk(QStringLiteral("One. Two. Three."), 300);
    QCOMPARE(chunks.size(), 1);
    QCOMPARE(chunks.first(), QStringLiteral("One. Two. Three."));
}

void TestSupertonicText::splitsLongTextUnderLimit()
{
    const QString sentence = QStringLiteral("This is a sentence with a useful number of words in it.");
    QString text;
    for (int i = 0; i < 8; ++i)
        text += sentence + QLatin1Char(' ');

    const QStringList chunks = SupertonicText::chunk(text.trimmed(), 300);
    QVERIFY(chunks.size() > 1);
    for (const QString &chunk : chunks)
        QVERIFY2(chunk.size() <= 300, qPrintable(chunk));
}

void TestSupertonicText::hardSplitsOverlongSentence()
{
    const QString text(700, QLatin1Char('a'));
    const QStringList chunks = SupertonicText::chunk(text, 300);
    QCOMPARE(chunks.size(), 3);
    for (const QString &chunk : chunks) {
        QVERIFY(!chunk.isEmpty());
        QVERIFY2(chunk.size() <= 300, qPrintable(chunk));
    }
}

void TestSupertonicText::handlesEmptyInput()
{
    QVERIFY(SupertonicText::preprocess(QString(), QStringLiteral("en")).isEmpty());
    QVERIFY(SupertonicText::chunk(QString(), 300).isEmpty());
    QVERIFY(SupertonicText::unicodeValues(QString()).empty());
}

QTEST_GUILESS_MAIN(TestSupertonicText)

#include "test_supertonic_text.moc"
