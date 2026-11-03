#include <QtTest/QtTest>

#include "audio-consumer.hpp"

class TestAudioConsumer : public QObject {
    Q_OBJECT

private slots:
    void firstActiveOwnerBecomesConsumer();
    void secondOwnerWaitsUntilFirstReleases();
    void releaseByNonConsumerKeepsTheConsumer();
    void isConsumerOnlyForTheOwner();
};

void TestAudioConsumer::firstActiveOwnerBecomesConsumer()
{
    AudioConsumerArbiter arbiter;
    int first = 1;

    QVERIFY(arbiter.tryAcquire(&first));
    QVERIFY(arbiter.isConsumer(&first));
}

void TestAudioConsumer::secondOwnerWaitsUntilFirstReleases()
{
    AudioConsumerArbiter arbiter;
    int first = 1;
    int second = 2;

    QVERIFY(arbiter.tryAcquire(&first));
    QVERIFY(!arbiter.tryAcquire(&second));
    QVERIFY(!arbiter.isConsumer(&second));

    arbiter.release(&first);
    QVERIFY(!arbiter.isConsumer(&first));
    QVERIFY(arbiter.tryAcquire(&second));
    QVERIFY(arbiter.isConsumer(&second));
}

void TestAudioConsumer::releaseByNonConsumerKeepsTheConsumer()
{
    AudioConsumerArbiter arbiter;
    int first = 1;
    int second = 2;

    QVERIFY(arbiter.tryAcquire(&first));
    arbiter.release(&second);
    QVERIFY(arbiter.isConsumer(&first));
}

void TestAudioConsumer::isConsumerOnlyForTheOwner()
{
    AudioConsumerArbiter arbiter;
    int first = 1;
    int other = 2;

    QVERIFY(!arbiter.isConsumer(&first));
    QVERIFY(arbiter.tryAcquire(&first));
    QVERIFY(arbiter.isConsumer(&first));
    QVERIFY(!arbiter.isConsumer(&other));
    QVERIFY(!arbiter.isConsumer(nullptr));
}

QTEST_GUILESS_MAIN(TestAudioConsumer)

#include "test_audio_consumer.moc"
