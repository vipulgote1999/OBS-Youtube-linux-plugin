#include <QtTest/QtTest>

#include <chrono>
#include <vector>

#include "tts-queue.hpp"

using namespace std::chrono_literals;

namespace {
ChatMessage makeMessage(const QString &id, const QString &kind = QStringLiteral("text"))
{
    ChatMessage msg;
    msg.id = id;
    msg.author = QStringLiteral("author-") + id;
    msg.text = QStringLiteral("message-") + id;
    msg.kind = kind;
    return msg;
}
} // namespace

class TestTtsQueue : public QObject {
    Q_OBJECT

private slots:
    void TestTtsQueueOrder();
    void TestTtsQueueLimit();
    void ignoresDuplicateIds();
    void snapshotReturnsPopOrder();
    void clearEmptiesQueue();
    void countsEvictions();
};

void TestTtsQueue::TestTtsQueueOrder()
{
    TtsQueue queue;
    queue.push(makeMessage(QStringLiteral("A")), 0);
    queue.push(makeMessage(QStringLiteral("B")), 0);
    queue.push(makeMessage(QStringLiteral("C"), QStringLiteral("superchat")), 10);

    ChatMessage out;
    QVERIFY(queue.pop(&out, 100ms));
    QCOMPARE(out.id, QStringLiteral("C"));
    QVERIFY(queue.pop(&out, 100ms));
    QCOMPARE(out.id, QStringLiteral("A"));
    QVERIFY(queue.pop(&out, 100ms));
    QCOMPARE(out.id, QStringLiteral("B"));
    QVERIFY(!queue.pop(&out, 10ms));
}

void TestTtsQueue::TestTtsQueueLimit()
{
    TtsQueue queue;
    queue.setLimit(2);
    queue.push(makeMessage(QStringLiteral("A")), 0);
    queue.push(makeMessage(QStringLiteral("B")), 0);
    queue.push(makeMessage(QStringLiteral("C")), 0);

    QCOMPARE(queue.size(), size_t(2));

    const std::vector<ChatMessage> items = queue.snapshot(10);
    QCOMPARE(items.size(), size_t(2));
    QCOMPARE(items.at(0).id, QStringLiteral("B"));
    QCOMPARE(items.at(1).id, QStringLiteral("C"));

    /* A low-priority newcomer still evicts the lowest-priority existing
     * item, which is the normal A even though B is newer. */
    TtsQueue priorityQueue;
    priorityQueue.setLimit(2);
    priorityQueue.push(makeMessage(QStringLiteral("A")), 0);
    priorityQueue.push(makeMessage(QStringLiteral("B"), QStringLiteral("superchat")), 10);
    priorityQueue.push(makeMessage(QStringLiteral("C")), 0);
    const std::vector<ChatMessage> priorityItems = priorityQueue.snapshot(10);
    QCOMPARE(priorityItems.size(), size_t(2));
    QCOMPARE(priorityItems.at(0).id, QStringLiteral("B"));
    QCOMPARE(priorityItems.at(1).id, QStringLiteral("C"));

    /* Reducing the limit trims by the same lowest-priority-oldest rule. */
    TtsQueue shrinkQueue;
    shrinkQueue.push(makeMessage(QStringLiteral("A")), 0);
    shrinkQueue.push(makeMessage(QStringLiteral("B"), QStringLiteral("superchat")), 10);
    shrinkQueue.push(makeMessage(QStringLiteral("C")), 0);
    shrinkQueue.setLimit(1);
    const std::vector<ChatMessage> shrinkItems = shrinkQueue.snapshot(10);
    QCOMPARE(shrinkItems.size(), size_t(1));
    QCOMPARE(shrinkItems.at(0).id, QStringLiteral("B"));
}

void TestTtsQueue::ignoresDuplicateIds()
{
    TtsQueue queue;
    queue.push(makeMessage(QStringLiteral("A")), 0);
    queue.push(makeMessage(QStringLiteral("A")), 10);

    QCOMPARE(queue.size(), size_t(1));

    ChatMessage out;
    QVERIFY(queue.pop(&out, 100ms));
    QCOMPARE(out.id, QStringLiteral("A"));
    QCOMPARE(queue.size(), size_t(0));
}

void TestTtsQueue::snapshotReturnsPopOrder()
{
    TtsQueue queue;
    queue.push(makeMessage(QStringLiteral("A")), 0);
    queue.push(makeMessage(QStringLiteral("B")), 10);
    queue.push(makeMessage(QStringLiteral("C")), 0);

    const std::vector<ChatMessage> items = queue.snapshot(2);
    QCOMPARE(items.size(), size_t(2));
    QCOMPARE(items.at(0).id, QStringLiteral("B"));
    QCOMPARE(items.at(1).id, QStringLiteral("A"));
    QCOMPARE(queue.size(), size_t(3));
}

void TestTtsQueue::clearEmptiesQueue()
{
    TtsQueue queue;
    queue.push(makeMessage(QStringLiteral("A")), 0);

    queue.clear();
    QCOMPARE(queue.size(), size_t(0));

    ChatMessage out;
    QVERIFY(!queue.pop(&out, 10ms));
}

void TestTtsQueue::countsEvictions()
{
    TtsQueue queue;
    QCOMPARE(queue.evictionCount(), uint64_t(0));

    queue.setLimit(2);
    queue.push(makeMessage(QStringLiteral("A")), 0);
    queue.push(makeMessage(QStringLiteral("B")), 0);
    queue.push(makeMessage(QStringLiteral("C")), 0);
    QCOMPARE(queue.evictionCount(), uint64_t(1));

    /* Shrinking the limit evicts by the same rule and is counted too. */
    queue.setLimit(1);
    QCOMPARE(queue.evictionCount(), uint64_t(2));

    /* clear() is a user action, not an eviction. */
    queue.clear();
    QCOMPARE(queue.evictionCount(), uint64_t(2));
}

QTEST_GUILESS_MAIN(TestTtsQueue)

#include "test_tts_queue.moc"
