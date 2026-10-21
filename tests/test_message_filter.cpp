#include <QtTest/QtTest>

#include "chat-message.hpp"
#include "message-filter.hpp"
#include "settings.hpp"

namespace {
ChatMessage makeMessage(const QString &id, const QString &author, const QString &text,
                        const QString &kind = QStringLiteral("text"))
{
    ChatMessage message;
    message.id = id;
    message.author = author;
    message.authorId = author;
    message.text = text;
    message.kind = kind;
    return message;
}

Settings noCooldowns()
{
    Settings settings;
    settings.userCooldownSec = 0;
    settings.globalCooldownSec = 0;
    return settings;
}
} // namespace

class TestMessageFilter : public QObject {
    Q_OBJECT

private slots:
    void rejectsCommands();
    void TestFilterRejectsBlocklisted();
    void filtersDisabledMessageKinds();
    void filtersDisabledModerators();
    void enforcesUserCooldown();
    void enforcesGlobalCooldown();
    void rejectsDuplicateIds();
    void resetClearsState();
};

void TestMessageFilter::rejectsCommands()
{
    MessageFilter filter;
    Settings settings = noCooldowns();
    settings.skipCommands = true;
    settings.commandPrefix = QStringLiteral("!");

    const FilterDecision decision = filter.evaluate(makeMessage(QStringLiteral("m1"), QStringLiteral("Alice"),
                                                                QStringLiteral("!hello")),
                                                   settings, 0);
    QVERIFY(!decision.accept);
    QVERIFY(!decision.reason.isEmpty());
}

void TestMessageFilter::TestFilterRejectsBlocklisted()
{
    MessageFilter filter;
    Settings settings = noCooldowns();
    settings.skipBlocked = true;
    settings.blocklist = QStringLiteral("spoiler");

    const FilterDecision blocked = filter.evaluate(
        makeMessage(QStringLiteral("m1"), QStringLiteral("Alice"),
                    QStringLiteral("This is a SPOILER about the ending")),
        settings, 0);
    QVERIFY(!blocked.accept);
    QVERIFY(!blocked.reason.isEmpty());

    const FilterDecision allowed = filter.evaluate(
        makeMessage(QStringLiteral("m2"), QStringLiteral("Alice"), QStringLiteral("Hello everyone")),
        settings, 0);
    QVERIFY(allowed.accept);

    settings.skipBlocked = false;
    MessageFilter unfiltered;
    QVERIFY(unfiltered
                .evaluate(makeMessage(QStringLiteral("m3"), QStringLiteral("Bob"),
                                      QStringLiteral("This is a SPOILER")),
                          settings, 0)
                .accept);
}

void TestMessageFilter::filtersDisabledMessageKinds()
{
    Settings superchat = noCooldowns();
    superchat.speakSuperchats = false;

    MessageFilter filter;
    ChatMessage message =
        makeMessage(QStringLiteral("m1"), QStringLiteral("Alice"), QStringLiteral("great stream"),
                    QStringLiteral("superchat"));
    message.amount = QStringLiteral("$5.00");

    QVERIFY(!filter.evaluate(message, superchat, 0).accept);

    superchat.speakSuperchats = true;
    const FilterDecision accepted = filter.evaluate(message, superchat, 0);
    QVERIFY(accepted.accept);
    QCOMPARE(accepted.priority, 10);

    Settings membership = noCooldowns();
    membership.speakMemberships = false;
    filter.reset();
    QVERIFY(!filter
                 .evaluate(makeMessage(QStringLiteral("m2"), QStringLiteral("Bob"),
                                       QStringLiteral("joined"), QStringLiteral("membership")),
                           membership, 0)
                 .accept);

    membership.speakMemberships = true;
    filter.reset();
    const FilterDecision memberAccepted = filter.evaluate(
        makeMessage(QStringLiteral("m3"), QStringLiteral("Bob"), QStringLiteral("joined"),
                    QStringLiteral("membership")),
        membership, 0);
    QVERIFY(memberAccepted.accept);
    QCOMPARE(memberAccepted.priority, 5);
}

void TestMessageFilter::filtersDisabledModerators()
{
    MessageFilter filter;
    Settings settings = noCooldowns();
    settings.readModerators = false;

    ChatMessage message =
        makeMessage(QStringLiteral("m1"), QStringLiteral("Alice"), QStringLiteral("hello"));
    message.isModerator = true;

    QVERIFY(!filter.evaluate(message, settings, 0).accept);

    settings.readModerators = true;
    filter.reset();
    QVERIFY(filter.evaluate(message, settings, 0).accept);
}

void TestMessageFilter::enforcesUserCooldown()
{
    MessageFilter filter;
    Settings settings = noCooldowns();
    settings.userCooldownSec = 30;

    const ChatMessage first =
        makeMessage(QStringLiteral("m1"), QStringLiteral("Alice"), QStringLiteral("one"));
    QVERIFY(filter.evaluate(first, settings, 0).accept);

    const ChatMessage again =
        makeMessage(QStringLiteral("m2"), QStringLiteral("Alice"), QStringLiteral("two"));
    QVERIFY(!filter.evaluate(again, settings, 0).accept);

    const ChatMessage third =
        makeMessage(QStringLiteral("m3"), QStringLiteral("Alice"), QStringLiteral("three"));
    QVERIFY(filter.evaluate(third, settings, 31000).accept);
}

void TestMessageFilter::enforcesGlobalCooldown()
{
    MessageFilter filter;
    Settings settings = noCooldowns();
    settings.globalCooldownSec = 2;

    QVERIFY(filter
                .evaluate(makeMessage(QStringLiteral("m1"), QStringLiteral("Alice"),
                                      QStringLiteral("one")),
                          settings, 0)
                .accept);

    QVERIFY(!filter
                 .evaluate(makeMessage(QStringLiteral("m2"), QStringLiteral("Bob"),
                                       QStringLiteral("two")),
                           settings, 1000)
                 .accept);

    QVERIFY(filter
                .evaluate(makeMessage(QStringLiteral("m3"), QStringLiteral("Bob"),
                                      QStringLiteral("three")),
                          settings, 2001)
                .accept);
}

void TestMessageFilter::rejectsDuplicateIds()
{
    MessageFilter filter;
    const Settings settings = noCooldowns();

    QVERIFY(filter
                .evaluate(makeMessage(QStringLiteral("same-id"), QStringLiteral("Alice"),
                                      QStringLiteral("one")),
                          settings, 0)
                .accept);

    const FilterDecision duplicate = filter.evaluate(
        makeMessage(QStringLiteral("same-id"), QStringLiteral("Bob"), QStringLiteral("two")),
        settings, 10000);
    QVERIFY(!duplicate.accept);
    QVERIFY(!duplicate.reason.isEmpty());
}

void TestMessageFilter::resetClearsState()
{
    MessageFilter filter;
    const Settings settings = noCooldowns();
    const ChatMessage message =
        makeMessage(QStringLiteral("m1"), QStringLiteral("Alice"), QStringLiteral("hello"));

    QVERIFY(filter.evaluate(message, settings, 0).accept);
    QVERIFY(!filter.evaluate(message, settings, 0).accept);

    filter.reset();
    QVERIFY(filter.evaluate(message, settings, 0).accept);
}

QTEST_GUILESS_MAIN(TestMessageFilter)

#include "test_message_filter.moc"
