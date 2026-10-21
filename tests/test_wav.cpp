#include <QtTest/QtTest>

#include <QByteArray>
#include <QFile>
#include <QVector>

#include <cstdint>

#include "wav.hpp"

namespace {
constexpr quint32 kBogusDataSize = 0xFFFFFFFFu;

void appendU16(QByteArray *bytes, quint16 value)
{
    bytes->append(char(value & 0xFF));
    bytes->append(char((value >> 8) & 0xFF));
}

void appendU32(QByteArray *bytes, quint32 value)
{
    bytes->append(char(value & 0xFF));
    bytes->append(char((value >> 8) & 0xFF));
    bytes->append(char((value >> 16) & 0xFF));
    bytes->append(char((value >> 24) & 0xFF));
}

/* Builds a canonical 44-byte-header WAV.  dataSize < 0 means "the actual
 * payload length"; any other value is written into the data chunk header so
 * tests can exercise espeak-ng's bogus size fields. */
QByteArray makeWav(const QVector<qint16> &samples, quint16 channels = 1,
                   quint32 sampleRate = 22050, quint16 bitsPerSample = 16,
                   quint16 audioFormat = 1, qint64 dataSize = -1)
{
    QByteArray payload;
    for (qint16 sample : samples) {
        appendU16(&payload, quint16(sample));
    }

    const quint16 blockAlign = quint16(channels * bitsPerSample / 8);
    const quint32 byteRate = sampleRate * blockAlign;

    QByteArray wav;
    wav.append("RIFF");
    appendU32(&wav, quint32(36 + payload.size()));
    wav.append("WAVE");

    wav.append("fmt ");
    appendU32(&wav, 16);
    appendU16(&wav, audioFormat);
    appendU16(&wav, channels);
    appendU32(&wav, sampleRate);
    appendU32(&wav, byteRate);
    appendU16(&wav, blockAlign);
    appendU16(&wav, bitsPerSample);

    wav.append("data");
    appendU32(&wav, dataSize < 0 ? quint32(payload.size()) : quint32(dataSize));
    wav.append(payload);
    return wav;
}

bool parse(const QByteArray &bytes, std::vector<float> *pcm, int *sampleRate, QString *error)
{
    return parseWav(bytes, pcm, sampleRate, error);
}
} // namespace

class TestWav : public QObject {
    Q_OBJECT

private slots:
    void parsesValidPcm();
    void TestWavBogusDataSize();
    void TestWavRejectsUnsupportedFormat();
    void rejectsTruncatedWav();
};

void TestWav::parsesValidPcm()
{
    std::vector<float> pcm;
    int sampleRate = 0;
    QString error;

    QVERIFY2(parse(makeWav({0, 16384}), &pcm, &sampleRate, &error), qPrintable(error));
    QCOMPARE(sampleRate, 22050);
    QCOMPARE(pcm.size(), size_t(2));
    QCOMPARE(pcm.at(0), 0.0f);
    QVERIFY(qAbs(pcm.at(1) - 0.5f) < 0.0001f);
    QVERIFY(error.isEmpty());

    /* The on-disk fixture is the same two-sample WAV used by the fake
     * espeak-ng engine test. */
    QFile fixture(QStringLiteral(TEST_FIXTURES_DIR "/test.wav"));
    QVERIFY2(fixture.open(QIODevice::ReadOnly), qPrintable(fixture.errorString()));
    QVERIFY2(parse(fixture.readAll(), &pcm, &sampleRate, &error), qPrintable(error));
    QCOMPARE(pcm.size(), size_t(2));
}

void TestWav::TestWavBogusDataSize()
{
    std::vector<float> pcm;
    int sampleRate = 0;
    QString error;

    /* data size 0 with eight trailing bytes: espeak-ng --stdout does this. */
    QVERIFY2(parse(makeWav({1, 2, 3, 4}, 1, 22050, 16, 1, 0), &pcm, &sampleRate, &error),
             qPrintable(error));
    QCOMPARE(pcm.size(), size_t(4));
    QCOMPARE(sampleRate, 22050);

    /* 0xFFFFFFFF is "unknown length". */
    QVERIFY2(parse(makeWav({1, 2, 3, 4}, 1, 22050, 16, 1, kBogusDataSize), &pcm, &sampleRate,
                   &error),
             qPrintable(error));
    QCOMPARE(pcm.size(), size_t(4));

    /* A size larger than the remaining bytes also means "read the rest". */
    QVERIFY2(parse(makeWav({1, 2, 3, 4}, 1, 22050, 16, 1, 100), &pcm, &sampleRate, &error),
             qPrintable(error));
    QCOMPARE(pcm.size(), size_t(4));
}

void TestWav::TestWavRejectsUnsupportedFormat()
{
    std::vector<float> pcm;
    int sampleRate = 0;
    QString error;

    QVERIFY(!parse(makeWav({1, 2}, 1, 44100), &pcm, &sampleRate, &error));
    QVERIFY2(error.contains(QStringLiteral("sample rate"), Qt::CaseInsensitive),
             qPrintable(error));

    error.clear();
    QVERIFY(!parse(makeWav({1, 2}, 2, 22050), &pcm, &sampleRate, &error));
    QVERIFY2(error.contains(QStringLiteral("mono"), Qt::CaseInsensitive), qPrintable(error));

    error.clear();
    QVERIFY(!parse(makeWav({1, 2}, 1, 22050, 16, 3), &pcm, &sampleRate, &error));
    QVERIFY(!error.isEmpty());
}

void TestWav::rejectsTruncatedWav()
{
    std::vector<float> pcm;
    int sampleRate = 0;
    QString error;

    QVERIFY(!parse(QByteArray(), &pcm, &sampleRate, &error));
    QVERIFY(!error.isEmpty());

    error.clear();
    const QByteArray valid = makeWav({1, 2});
    QVERIFY(!parse(valid.left(12), &pcm, &sampleRate, &error));
    QVERIFY(!error.isEmpty());

    /* Header promises four data bytes but only three are present: the
     * remaining payload is not a whole 16-bit sample. */
    error.clear();
    QVERIFY(!parse(valid.left(valid.size() - 1), &pcm, &sampleRate, &error));
    QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(TestWav)

#include "test_wav.moc"
