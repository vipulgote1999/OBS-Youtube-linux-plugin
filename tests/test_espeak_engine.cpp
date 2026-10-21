#include <QtTest/QtTest>

#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include <atomic>
#include <thread>
#include <vector>

#include "espeak-engine.hpp"

namespace {
QByteArray makeWav(qint16 first, qint16 second)
{
    auto appendU16 = [](QByteArray *bytes, quint16 value) {
        bytes->append(char(value & 0xFF));
        bytes->append(char((value >> 8) & 0xFF));
    };
    auto appendU32 = [](QByteArray *bytes, quint32 value) {
        bytes->append(char(value & 0xFF));
        bytes->append(char((value >> 8) & 0xFF));
        bytes->append(char((value >> 16) & 0xFF));
        bytes->append(char((value >> 24) & 0xFF));
    };

    QByteArray wav;
    wav.append("RIFF");
    appendU32(&wav, 40);
    wav.append("WAVE");
    wav.append("fmt ");
    appendU32(&wav, 16);
    appendU16(&wav, 1);
    appendU16(&wav, 1);
    appendU32(&wav, 22050);
    appendU32(&wav, 44100);
    appendU16(&wav, 2);
    appendU16(&wav, 16);
    wav.append("data");
    appendU32(&wav, 4);
    appendU16(&wav, quint16(first));
    appendU16(&wav, quint16(second));
    return wav;
}

bool writeExecutableScript(const QString &path, const QByteArray &body)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    if (file.write(body) != body.size())
        return false;
    file.close();
    return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                           QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                           QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                           QFileDevice::ExeOther);
}

bool writeWav(const QString &path, qint16 first, qint16 second)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray bytes = makeWav(first, second);
    return file.write(bytes) == bytes.size();
}

void clearFakeEnvironment()
{
    qunsetenv("FAKE_ESPEAK_WAV");
    qunsetenv("FAKE_ESPEAK_MARKER");
}
} // namespace

class TestEspeakEngine : public QObject {
    Q_OBJECT

private slots:
    void synthesizesPcmViaFakeEspeak();
    void reportsNonZeroExit();
    void cancelStopsSynthesis();
    void cancelBeforeSynthesizeIsNotLost();
};

void TestEspeakEngine::synthesizesPcmViaFakeEspeak()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString wavPath = temp.filePath(QStringLiteral("speech.wav"));
    QVERIFY(writeWav(wavPath, 0, 16384));
    qputenv("FAKE_ESPEAK_WAV", wavPath.toUtf8());

    EspeakEngine engine(QStringLiteral(TEST_FIXTURES_DIR "/fake-espeak.sh"), QString(),
                        QStringLiteral("en-us"), 165, 50, 150);

    std::vector<float> pcm;
    int sampleRate = 0;
    QString error;
    const bool ok = engine.synthesize(QStringLiteral("hello"), &pcm, &sampleRate, &error);
    clearFakeEnvironment();

    QVERIFY2(ok, qPrintable(error));
    QVERIFY(!pcm.empty());
    QCOMPARE(sampleRate, 22050);
    QCOMPARE(pcm.size(), size_t(2));
    QVERIFY(qAbs(pcm.at(1) - 0.5f) < 0.0001f);
    QVERIFY(error.isEmpty());
}

void TestEspeakEngine::reportsNonZeroExit()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString scriptPath = temp.filePath(QStringLiteral("fail-espeak.sh"));
    QVERIFY(writeExecutableScript(scriptPath,
                                  "#!/bin/sh\n"
                                  "cat >/dev/null\n"
                                  "echo 'synthetic failure' >&2\n"
                                  "exit 2\n"));

    EspeakEngine engine(scriptPath, QString(), QStringLiteral("en-us"), 165, 50, 150);

    std::vector<float> pcm;
    int sampleRate = 0;
    QString error;
    QVERIFY(!engine.synthesize(QStringLiteral("hello"), &pcm, &sampleRate, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY2(error.contains(QStringLiteral("synthetic failure")), qPrintable(error));
    QCOMPARE(sampleRate, 0);
}

void TestEspeakEngine::cancelStopsSynthesis()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString wavPath = temp.filePath(QStringLiteral("speech.wav"));
    QVERIFY(writeWav(wavPath, 0, 16384));

    const QString markerPath = temp.filePath(QStringLiteral("started"));
    const QString scriptPath = temp.filePath(QStringLiteral("slow-espeak.sh"));
    QVERIFY(writeExecutableScript(scriptPath,
                                  "#!/bin/sh\n"
                                  "cat >/dev/null\n"
                                  "touch \"$FAKE_ESPEAK_MARKER\"\n"
                                  "sleep 5\n"
                                  "cat \"$FAKE_ESPEAK_WAV\"\n"));
    qputenv("FAKE_ESPEAK_WAV", wavPath.toUtf8());
    qputenv("FAKE_ESPEAK_MARKER", markerPath.toUtf8());

    EspeakEngine engine(scriptPath, QString(), QStringLiteral("en-us"), 165, 50, 150);

    std::vector<float> pcm;
    int sampleRate = 0;
    QString error;
    std::atomic<bool> ok{true};
    std::thread worker([&] {
        ok = engine.synthesize(QStringLiteral("hello"), &pcm, &sampleRate, &error);
    });

    QElapsedTimer startup;
    startup.start();
    bool started = QFile::exists(markerPath);
    while (!started && startup.elapsed() < 5000) {
        QTest::qWait(10);
        started = QFile::exists(markerPath);
    }

    QElapsedTimer cancelTimer;
    cancelTimer.start();
    engine.cancel();
    worker.join();
    clearFakeEnvironment();

    QVERIFY2(started, "fake espeak-ng never started");
    QVERIFY(!ok.load());
    QVERIFY2(error.contains(QStringLiteral("cancelled")), qPrintable(error));
    QVERIFY(cancelTimer.elapsed() < 3000);
}

void TestEspeakEngine::cancelBeforeSynthesizeIsNotLost()
{
    /* Regression for the stop/cancel race: synthesize() must not clear a
     * cancel mark that arrived between the worker's running check and the
     * engine call.  Only resetCancel() may clear it. */
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString wavPath = temp.filePath(QStringLiteral("speech.wav"));
    QVERIFY(writeWav(wavPath, 0, 16384));
    qputenv("FAKE_ESPEAK_WAV", wavPath.toUtf8());

    EspeakEngine engine(QStringLiteral(TEST_FIXTURES_DIR "/fake-espeak.sh"), QString(),
                        QStringLiteral("en-us"), 165, 50, 150);

    engine.cancel();

    std::vector<float> pcm;
    int sampleRate = 0;
    QString error;
    const bool ok = engine.synthesize(QStringLiteral("hello"), &pcm, &sampleRate, &error);
    clearFakeEnvironment();

    QVERIFY(!ok);
    QVERIFY2(error.contains(QStringLiteral("cancelled")), qPrintable(error));
    QVERIFY(pcm.empty());
    QCOMPARE(sampleRate, 0);
}

QTEST_GUILESS_MAIN(TestEspeakEngine)

#include "test_espeak_engine.moc"
