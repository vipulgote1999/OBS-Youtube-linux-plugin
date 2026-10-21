#include <QtTest/QtTest>

#include <vector>

#include "audio-buffer.hpp"

namespace {
std::vector<float> sequential(size_t count, float first = -0.5f)
{
    /* Stay inside [-1, 1] so the order checks observe the raw values rather
     * than pop()'s intentional clamping. */
    std::vector<float> values(count);
    for (size_t i = 0; i < count; ++i)
        values[i] = first + float(i) * 0.01f;
    return values;
}
} // namespace

class TestAudioBuffer : public QObject {
    Q_OBJECT

private slots:
    void popsInOrder();
    void zeroFillsRemainder();
    void clearEmptiesBuffer();
    void dropsOldestWhenAtCapacity();
    void appliesVolumeAndClamps();
    void defaultCapacityIs120Seconds();
};

void TestAudioBuffer::popsInOrder()
{
    AudioBuffer buffer;
    const std::vector<float> input = sequential(100);
    buffer.push(input.data(), input.size());
    QCOMPARE(buffer.available(), size_t(100));

    std::vector<float> output(50, -1.0f);
    QCOMPARE(buffer.pop(output.data(), output.size()), size_t(50));
    QCOMPARE(buffer.available(), size_t(50));

    for (size_t i = 0; i < output.size(); ++i)
        QCOMPARE(output[i], input[i]);
}

void TestAudioBuffer::zeroFillsRemainder()
{
    AudioBuffer buffer;
    const std::vector<float> input = sequential(50);
    buffer.push(input.data(), input.size());

    std::vector<float> output(100, 7.0f);
    QCOMPARE(buffer.pop(output.data(), output.size()), size_t(50));
    QCOMPARE(buffer.available(), size_t(0));

    for (size_t i = 0; i < input.size(); ++i)
        QCOMPARE(output[i], input[i]);
    for (size_t i = input.size(); i < output.size(); ++i)
        QCOMPARE(output[i], 0.0f);
}

void TestAudioBuffer::clearEmptiesBuffer()
{
    AudioBuffer buffer;
    const std::vector<float> input = sequential(10);
    buffer.push(input.data(), input.size());
    QCOMPARE(buffer.available(), size_t(10));

    buffer.clear();
    QCOMPARE(buffer.available(), size_t(0));

    std::vector<float> output(4, 3.0f);
    QCOMPARE(buffer.pop(output.data(), output.size()), size_t(0));
    for (float value : output)
        QCOMPARE(value, 0.0f);
}

void TestAudioBuffer::dropsOldestWhenAtCapacity()
{
    AudioBuffer buffer;
    buffer.setMaxSamples(100);

    const std::vector<float> input = sequential(150);
    buffer.push(input.data(), input.size());
    QCOMPARE(buffer.available(), size_t(100));

    std::vector<float> output(100, -1.0f);
    QCOMPARE(buffer.pop(output.data(), output.size()), size_t(100));
    for (size_t i = 0; i < output.size(); ++i)
        QCOMPARE(output[i], input[i + 50]);
}

void TestAudioBuffer::appliesVolumeAndClamps()
{
    AudioBuffer buffer;
    buffer.setVolume(0.5f);
    QCOMPARE(buffer.volume(), 0.5f);

    const std::vector<float> input = {1.5f, -1.5f, 0.5f, -0.25f, 2.0f, -2.0f};
    buffer.push(input.data(), input.size());

    std::vector<float> output(input.size(), 0.0f);
    QCOMPARE(buffer.pop(output.data(), output.size()), input.size());

    const std::vector<float> expected = {0.75f, -0.75f, 0.25f, -0.125f, 1.0f, -1.0f};
    for (size_t i = 0; i < output.size(); ++i)
        QCOMPARE(output[i], expected[i]);
}

void TestAudioBuffer::defaultCapacityIs120Seconds()
{
    AudioBuffer buffer;
    const std::vector<float> input(120 * 22050 + 1, 0.0f);
    buffer.push(input.data(), input.size());
    QCOMPARE(buffer.available(), size_t(120 * 22050));
}

QTEST_GUILESS_MAIN(TestAudioBuffer)

#include "test_audio_buffer.moc"
