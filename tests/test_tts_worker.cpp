#include <QtTest/QSignalSpy>
#include <QtTest/QtTest>

#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>

#include <memory>
#include <vector>

#include "audio-buffer.hpp"
#include "tts-engine.hpp"
#include "tts-queue.hpp"
#include "tts-worker.hpp"

namespace {
constexpr int kSampleRate = 22050;
constexpr size_t kSamplesPerUtterance = 1000;

ChatMessage makeMessage(const QString &id)
{
    ChatMessage msg;
    msg.id = id;
    msg.author = QStringLiteral("author-") + id;
    msg.text = QStringLiteral("message-") + id;
    msg.kind = QStringLiteral("text");
    return msg;
}

/* Shared recording for the engines: synthesize() runs on the worker thread,
 * the assertions run on the test thread. */
class SynthRecorder {
public:
    void record(const QString &text)
    {
        QMutexLocker locker(&mutex_);
        texts_.append(text);
    }

    QStringList texts() const
    {
        QMutexLocker locker(&mutex_);
        return texts_;
    }

    qsizetype count() const
    {
        QMutexLocker locker(&mutex_);
        return texts_.size();
    }

private:
    mutable QMutex mutex_;
    QStringList texts_;
};

class FakeTtsEngine : public TtsEngine {
public:
    explicit FakeTtsEngine(SynthRecorder *recorder) : recorder_(recorder) {}

    bool synthesize(const QString &text, std::vector<float> *pcm, int *sampleRate,
                    QString *error) override
    {
        recorder_->record(text);
        if (error)
            error->clear();
        pcm->assign(kSamplesPerUtterance, 0.5f);
        *sampleRate = kSampleRate;
        return true;
    }

    void cancel() override {}

private:
    SynthRecorder *recorder_;
};

class FailingTtsEngine : public TtsEngine {
public:
    explicit FailingTtsEngine(SynthRecorder *recorder) : recorder_(recorder) {}

    bool synthesize(const QString &text, std::vector<float> *pcm, int *sampleRate,
                    QString *error) override
    {
        recorder_->record(text);
        pcm->clear();
        *sampleRate = 0;
        if (error)
            *error = QStringLiteral("boom");
        return false;
    }

    void cancel() override {}

private:
    SynthRecorder *recorder_;
};
} // namespace

class TestTtsWorker : public QObject {
    Q_OBJECT

private slots:
    void TestTtsWorkerStopJoins();
    void failureContinuesToNextMessage();
    void rendersWithProvidedSettings();
};

void TestTtsWorker::TestTtsWorkerStopJoins()
{
    TtsQueue queue;
    AudioBuffer audio;
    SynthRecorder recorder;

    queue.push(makeMessage(QStringLiteral("1")), 0);
    queue.push(makeMessage(QStringLiteral("2")), 0);
    QCOMPARE(queue.size(), size_t(2));

    TtsWorker worker(queue, audio,
                     std::make_unique<FakeTtsEngine>(&recorder));

    QSignalSpy errorSpy(&worker, &TtsWorker::errorOccurred);
    worker.start();

    /* Both utterances are synthesized in FIFO order, and both PCM blocks are
     * appended to the shared buffer.  Waiting for 2,000 samples (rather than
     * the brief's minimum of 1,000) guarantees the worker finished the second
     * push before skip() clears the buffer, so the assertion below is not
     * racy. */
    QTRY_VERIFY_WITH_TIMEOUT(recorder.count() == 2 && audio.available() >= 2000, 5000);

    const QStringList texts = recorder.texts();
    QCOMPARE(texts.size(), 2);
    QVERIFY2(texts.at(0).contains(QStringLiteral("message-1")), qPrintable(texts.at(0)));
    QVERIFY2(texts.at(1).contains(QStringLiteral("message-2")), qPrintable(texts.at(1)));

    worker.skip();
    QCOMPARE(queue.size(), size_t(0));
    QCOMPARE(audio.available(), size_t(0));

    QElapsedTimer stopTimer;
    stopTimer.start();
    worker.stop();
    QVERIFY2(stopTimer.elapsed() < 3000, "stop() did not return promptly");

    /* stop() must be safe to call again (the dock may disconnect twice). */
    worker.stop();
    QCOMPARE(errorSpy.count(), 0);
}

void TestTtsWorker::failureContinuesToNextMessage()
{    TtsQueue queue;
    AudioBuffer audio;
    SynthRecorder recorder;

    queue.push(makeMessage(QStringLiteral("3")), 0);
    queue.push(makeMessage(QStringLiteral("4")), 0);

    TtsWorker worker(queue, audio,
                     std::make_unique<FailingTtsEngine>(&recorder));

    QSignalSpy errorSpy(&worker, &TtsWorker::errorOccurred);
    worker.start();

    /* A failed synthesis emits the engine error and the worker moves on to the
     * second message instead of exiting. */
    QTRY_COMPARE_WITH_TIMEOUT(errorSpy.count(), 2, 5000);
    for (const QList<QVariant> &arguments : errorSpy) {
        const QString message = arguments.at(0).toString();
        QVERIFY2(message.contains(QStringLiteral("boom")), qPrintable(message));
    }

    QCOMPARE(recorder.count(), 2);
    QCOMPARE(audio.available(), size_t(0));
    QTRY_COMPARE_WITH_TIMEOUT(queue.size(), size_t(0), 2000);

    worker.stop();
}

void TestTtsWorker::rendersWithProvidedSettings()
{
    TtsQueue queue;
    AudioBuffer audio;
    SynthRecorder recorder;

    ChatMessage msg = makeMessage(QStringLiteral("5"));
    msg.text = QStringLiteral("hello world this sentence is too long");
    queue.push(msg, 0);

    TtsWorker worker(queue, audio, std::make_unique<FakeTtsEngine>(&recorder));

    Settings settings;
    settings.readAuthor = false;
    settings.maxChars = 5;
    worker.setSettings(settings);

    worker.start();

    QTRY_COMPARE_WITH_TIMEOUT(recorder.count(), 1, 5000);
    QCOMPARE(recorder.texts().at(0), QStringLiteral("hello"));

    worker.stop();
}

QTEST_GUILESS_MAIN(TestTtsWorker)

#include "test_tts_worker.moc"
