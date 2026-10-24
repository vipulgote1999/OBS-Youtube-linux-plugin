#include <QtTest/QtTest>

#include <cmath>
#include <vector>

#include "audio-resampler.hpp"

namespace {
constexpr int kInRate = 24000;
constexpr int kOutRate = 22050;
constexpr float kPi = 3.14159265358979323846f;

double rms(const std::vector<float> &values, size_t begin, size_t end)
{
    double sum = 0.0;
    for (size_t i = begin; i < end; ++i)
        sum += double(values[i]) * values[i];
    return std::sqrt(sum / double(end - begin));
}

std::vector<float> tone(float amplitude, float hz, int rate, size_t count)
{
    std::vector<float> values(count);
    for (size_t i = 0; i < count; ++i)
        values[i] = amplitude * std::sin(2.0f * kPi * hz * i / rate);
    return values;
}
} // namespace

class TestAudioResampler : public QObject {
    Q_OBJECT

private slots:
    void keepsLengthRatio();
    void preservesOneKhzTone();
    void preservesDc();
    void handlesEmptyAndSameRate();
};

void TestAudioResampler::keepsLengthRatio()
{
    const std::vector<float> input(kInRate, 0.0f); // one second
    const std::vector<float> output = AudioResampler::resample(input, kInRate, kOutRate);
    QCOMPARE(output.size(), size_t(kOutRate));
}

void TestAudioResampler::preservesOneKhzTone()
{
    const std::vector<float> input = tone(0.5f, 1000.0f, kInRate, kInRate);
    const std::vector<float> output = AudioResampler::resample(input, kInRate, kOutRate);

    const double measured = rms(output, output.size() / 10, output.size() * 9 / 10);
    const double expected = 0.5 / std::sqrt(2.0);
    QVERIFY2(std::fabs(measured - expected) / expected < 0.02,
             qPrintable(QString::number(measured)));
}

void TestAudioResampler::preservesDc()
{
    const std::vector<float> input(kInRate / 10, 0.25f);
    const std::vector<float> output = AudioResampler::resample(input, kInRate, kOutRate);
    const double measured = rms(output, output.size() / 10, output.size() * 9 / 10);
    QVERIFY2(std::fabs(measured - 0.25) < 0.01, qPrintable(QString::number(measured)));
}

void TestAudioResampler::handlesEmptyAndSameRate()
{
    QVERIFY(AudioResampler::resample({}, kInRate, kOutRate).empty());

    const std::vector<float> input = {0.1f, -0.2f, 0.3f};
    const std::vector<float> same = AudioResampler::resample(input, kInRate, kInRate);
    QCOMPARE(same.size(), input.size());
    for (size_t i = 0; i < same.size(); ++i)
        QCOMPARE(same[i], input[i]);
}

QTEST_GUILESS_MAIN(TestAudioResampler)

#include "test_audio_resampler.moc"
