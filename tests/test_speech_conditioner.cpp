#include <QtTest/QtTest>

#include <algorithm>
#include <cmath>
#include <vector>

#include "speech-conditioner.hpp"

namespace {
constexpr int kRate = 22050;
constexpr float kPi = 3.14159265358979323846f;

std::vector<float> constant(float value, size_t count)
{
    return std::vector<float>(count, value);
}

std::vector<float> sine(float amplitude, float hz, size_t count)
{
    std::vector<float> values(count);
    for (size_t i = 0; i < count; ++i)
        values[i] = amplitude * std::sin(2.0f * float(M_PI) * hz * i / kRate);
    return values;
}

double rms(const std::vector<float> &values, size_t begin, size_t end)
{
    double sum = 0.0;
    for (size_t i = begin; i < end; ++i)
        sum += double(values[i]) * values[i];
    return std::sqrt(sum / double(end - begin));
}
} // namespace

class TestSpeechConditioner : public QObject {
    Q_OBJECT

private slots:
    void fadesBothEnds();
    void removesDcOffset();
    void preservesSpeechLevel();
    void removesEndClick();
    void keepsSampleCount();
    void handlesEmptyAndTinyBuffers();
    void clampsOvershoot();
    void streamsAcrossChunks();
};

void TestSpeechConditioner::fadesBothEnds()
{
    std::vector<float> pcm = constant(0.5f, kRate);
    SpeechConditioner::process(&pcm, {kRate});
    QCOMPARE(pcm.front(), 0.0f);
    QCOMPARE(pcm.back(), 0.0f);
}

void TestSpeechConditioner::removesDcOffset()
{
    std::vector<float> pcm = constant(0.5f, kRate);
    SpeechConditioner::process(&pcm, {kRate});

    /* Skip the first 100 ms so the one-pole high-pass has settled. */
    const size_t begin = kRate / 10;
    double mean = 0.0;
    for (size_t i = begin; i < pcm.size(); ++i)
        mean += pcm[i];
    mean /= double(pcm.size() - begin);
    QVERIFY2(std::fabs(mean) < 1e-3, qPrintable(QString::number(mean)));
}

void TestSpeechConditioner::preservesSpeechLevel()
{
    std::vector<float> pcm = sine(0.5f, 440.0f, kRate);
    SpeechConditioner::process(&pcm, {kRate});

    /* The middle 80% is untouched by the fades. */
    const double measured = rms(pcm, kRate / 10, kRate * 9 / 10);
    const double expected = 0.5 / std::sqrt(2.0);
    QVERIFY2(std::fabs(measured - expected) / expected < 0.02,
             qPrintable(QString::number(measured)));
}

void TestSpeechConditioner::removesEndClick()
{
    /* 100.25 cycles at 1 kHz ends on the sine peak, i.e. exactly the
     * non-zero-last-sample case measured in the bake-off. */
    const size_t count = size_t(kRate * 0.10025);
    std::vector<float> pcm(count);
    for (size_t i = 0; i < count; ++i)
        pcm[i] = 0.5f * std::sin(2.0f * float(M_PI) * 1000.0f * i / kRate);

    QVERIFY(std::fabs(pcm.back()) > 0.4f);

    SpeechConditioner::process(&pcm, {kRate});

    QCOMPARE(pcm.back(), 0.0f);
    QCOMPARE(pcm.front(), 0.0f);

    /* A 1 kHz sine legitimately changes ~0.14 per sample, so the fade must
     * not introduce a step larger than the waveform's own.  Compare the fade
     * window against the equally long window just before it. */
    const size_t fade = size_t(kRate * 5 / 1000);
    float fadeStep = 0.0f;
    for (size_t i = pcm.size() - fade; i < pcm.size(); ++i)
        fadeStep = std::max(fadeStep, std::fabs(pcm[i] - pcm[i - 1]));
    float bodyStep = 0.0f;
    for (size_t i = pcm.size() - 2 * fade; i < pcm.size() - fade; ++i)
        bodyStep = std::max(bodyStep, std::fabs(pcm[i] - pcm[i - 1]));

    QVERIFY2(fadeStep <= bodyStep * 1.5f,
             qPrintable(QStringLiteral("%1 vs %2").arg(fadeStep).arg(bodyStep)));
}

void TestSpeechConditioner::keepsSampleCount()
{
    std::vector<float> pcm = sine(0.3f, 220.0f, 1234);
    SpeechConditioner::process(&pcm, {kRate});
    QCOMPARE(pcm.size(), size_t(1234));
}

void TestSpeechConditioner::handlesEmptyAndTinyBuffers()
{
    std::vector<float> empty;
    SpeechConditioner::process(&empty, {kRate});
    QVERIFY(empty.empty());

    std::vector<float> tiny = {0.4f, -0.2f};
    SpeechConditioner::process(&tiny, {kRate});
    QCOMPARE(tiny.front(), 0.0f);
    QCOMPARE(tiny.back(), 0.0f);
}

void TestSpeechConditioner::clampsOvershoot()
{
    std::vector<float> pcm(2, 1.0f);
    pcm.insert(pcm.end(), 100, 0.99f);
    SpeechConditioner::process(&pcm, {kRate});
    for (float sample : pcm)
        QVERIFY(sample >= -1.0f && sample <= 1.0f);
}

void TestSpeechConditioner::streamsAcrossChunks()
{
    const std::vector<float> input = sine(0.5f, 440.0f, kRate);
    std::vector<float> chunked = input;

    SpeechConditioner::Stream stream({kRate});
    const size_t step = input.size() / 3;
    for (size_t begin = 0; begin < chunked.size(); begin += step) {
        const size_t end = std::min(begin + step, chunked.size());
        std::vector<float> part(chunked.begin() + qsizetype(begin),
                                chunked.begin() + qsizetype(end));
        stream.process(&part, begin == 0, end == chunked.size());
        std::copy(part.begin(), part.end(), chunked.begin() + qsizetype(begin));
    }

    QCOMPARE(chunked.front(), 0.0f);
    QCOMPARE(chunked.back(), 0.0f);

    const double measured = rms(chunked, kRate / 10, kRate * 9 / 10);
    const double expected = 0.5 / std::sqrt(2.0);
    QVERIFY2(std::fabs(measured - expected) / expected < 0.03,
             qPrintable(QString::number(measured)));

    /* No step larger than the waveform's own at any chunk boundary. */
    for (size_t boundary = step; boundary < chunked.size(); boundary += step) {
        const float stepSize = std::fabs(chunked[boundary] - chunked[boundary - 1]);
        QVERIFY2(stepSize < 0.2f, qPrintable(QString::number(stepSize)));
    }
}

QTEST_GUILESS_MAIN(TestSpeechConditioner)

#include "test_speech_conditioner.moc"
