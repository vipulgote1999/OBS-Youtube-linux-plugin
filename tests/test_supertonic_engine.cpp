#include <QtTest/QtTest>

#include <cmath>
#include <memory>
#include <vector>

#include "supertonic-engine.hpp"

namespace {
constexpr int kModelSampleRate = 44100;
constexpr int kWorkerSampleRate = 22050;
constexpr size_t kFakeSampleCount = 4410;

class FakeSupertonicApi : public SupertonicApi {
public:
    struct Call {
        QString text;
        float speed = 0.0f;
        int steps = 0;
    };

    std::vector<Call> calls;
    int cancelCount = 0;
    int resetCancelCount = 0;

    bool succeed = true;
    bool setErrorOnFailure = true;
    bool initializationFailedFlag = false;
    int resultSampleRate = kModelSampleRate;
    size_t sampleCount = kFakeSampleCount;

    bool synthesize(const QString &text, float speed, int steps, std::vector<float> *pcm,
                    int *sampleRate, QString *error) override
    {
        calls.push_back({text, speed, steps});
        if (!succeed) {
            if (error && setErrorOnFailure)
                *error = QStringLiteral("fake supertonic failure");
            return false;
        }

        if (pcm)
            pcm->assign(sampleCount, 0.25f);
        if (sampleRate)
            *sampleRate = resultSampleRate;
        return true;
    }

    bool synthesizeStreaming(const QString &text, float speed, int steps,
                             const ChunkSink &sink, int *sampleRate, QString *error) override
    {
        calls.push_back({text, speed, steps});
        if (!succeed) {
            if (error && setErrorOnFailure)
                *error = QStringLiteral("fake supertonic streaming failure");
            return false;
        }

        if (sampleRate)
            *sampleRate = resultSampleRate;
        if (!emitChunks)
            return true;

        const std::vector<float> first(100, 0.1f);
        const std::vector<float> last(200, 0.2f);
        if (!sink(first.data(), first.size(), false))
            return false;
        return sink(last.data(), last.size(), true);
    }

    void cancel() override { ++cancelCount; }
    void resetCancel() override { ++resetCancelCount; }
    bool initializationFailed() const override { return initializationFailedFlag; }

    bool emitChunks = true;
};
} // namespace

class TestSupertonicEngine : public QObject {
    Q_OBJECT

private slots:
    void convertsModelRateToWorkerRate();
    void passesDefaultSpeedAndSteps();
    void clampsSpeedAndSteps();
    void propagatesApiFailure();
    void fillsDefaultErrorWhenApiIsSilent();
    void rejectsWrongSampleRate();
    void rejectsWrongStreamingRate();
    void rejectsNullApi();
    void cancelIsForwarded();
    void forwardsInitializationFailure();
    void streamsResampledChunks();
    void emptyOutputIsSuccessful();
};

void TestSupertonicEngine::convertsModelRateToWorkerRate()
{
    auto api = std::make_unique<FakeSupertonicApi>();
    SupertonicEngine engine(std::move(api));

    std::vector<float> pcm{0.75f};
    int sampleRate = -1;
    QString error = QStringLiteral("stale error");

    QVERIFY2(engine.synthesize(QStringLiteral("hello"), &pcm, &sampleRate, &error),
             qPrintable(error));
    QCOMPARE(pcm.size(), size_t(kWorkerSampleRate / 10));
    QCOMPARE(sampleRate, kWorkerSampleRate);
    QVERIFY(error.isEmpty());
}

void TestSupertonicEngine::passesDefaultSpeedAndSteps()
{
    auto api = std::make_unique<FakeSupertonicApi>();
    FakeSupertonicApi *fake = api.get();
    SupertonicEngine engine(std::move(api));

    std::vector<float> pcm;
    int sampleRate = 0;
    QString error;
    QVERIFY(engine.synthesize(QStringLiteral("defaults"), &pcm, &sampleRate, &error));

    QCOMPARE(fake->calls.size(), size_t(1));
    QCOMPARE(fake->calls.at(0).text, QStringLiteral("defaults"));
    QVERIFY(qAbs(fake->calls.at(0).speed - 1.0f) < 0.0001f);
    QCOMPARE(fake->calls.at(0).steps, 8);
}

void TestSupertonicEngine::clampsSpeedAndSteps()
{
    struct Case {
        float input;
        float expected;
    };
    const Case speeds[] = {
        {0.1f, 0.5f},
        {2.5f, 2.0f},
        {1.25f, 1.25f},
        {std::nanf(""), 1.0f},
    };
    for (const Case &testCase : speeds) {
        auto api = std::make_unique<FakeSupertonicApi>();
        FakeSupertonicApi *fake = api.get();
        SupertonicEngine engine(std::move(api));
        engine.setSpeed(testCase.input);

        std::vector<float> pcm;
        int sampleRate = 0;
        QString error;
        QVERIFY(engine.synthesize(QStringLiteral("speed"), &pcm, &sampleRate, &error));
        QVERIFY2(qAbs(fake->calls.at(0).speed - testCase.expected) < 0.0001f,
                 qPrintable(QStringLiteral("speed %1 produced %2")
                                .arg(testCase.input)
                                .arg(fake->calls.at(0).speed)));
    }

    struct StepCase {
        int input;
        int expected;
    };
    const StepCase steps[] = {
        {1, 4},
        {99, 16},
        {6, 6},
    };
    for (const StepCase &testCase : steps) {
        auto api = std::make_unique<FakeSupertonicApi>();
        FakeSupertonicApi *fake = api.get();
        SupertonicEngine engine(std::move(api));
        engine.setSteps(testCase.input);

        std::vector<float> pcm;
        int sampleRate = 0;
        QString error;
        QVERIFY(engine.synthesize(QStringLiteral("steps"), &pcm, &sampleRate, &error));
        QCOMPARE(fake->calls.at(0).steps, testCase.expected);
    }
}

void TestSupertonicEngine::propagatesApiFailure()
{
    auto api = std::make_unique<FakeSupertonicApi>();
    FakeSupertonicApi *fake = api.get();
    fake->succeed = false;
    SupertonicEngine engine(std::move(api));

    std::vector<float> pcm{0.5f};
    int sampleRate = 123;
    QString error;
    QVERIFY(!engine.synthesize(QStringLiteral("fail"), &pcm, &sampleRate, &error));
    QCOMPARE(error, QStringLiteral("fake supertonic failure"));
    QVERIFY(pcm.empty());
    QCOMPARE(sampleRate, 0);
}

void TestSupertonicEngine::fillsDefaultErrorWhenApiIsSilent()
{
    auto api = std::make_unique<FakeSupertonicApi>();
    FakeSupertonicApi *fake = api.get();
    fake->succeed = false;
    fake->setErrorOnFailure = false;
    SupertonicEngine engine(std::move(api));

    std::vector<float> pcm;
    int sampleRate = 0;
    QString error;
    QVERIFY(!engine.synthesize(QStringLiteral("fail"), &pcm, &sampleRate, &error));
    QCOMPARE(error, QStringLiteral("supertonic synthesis failed"));
}

void TestSupertonicEngine::rejectsWrongSampleRate()
{
    auto api = std::make_unique<FakeSupertonicApi>();
    FakeSupertonicApi *fake = api.get();
    fake->resultSampleRate = 48000;
    SupertonicEngine engine(std::move(api));

    std::vector<float> pcm{0.5f};
    int sampleRate = 0;
    QString error;
    QVERIFY(!engine.synthesize(QStringLiteral("rate"), &pcm, &sampleRate, &error));
    QVERIFY2(error.contains(QStringLiteral("48000")), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("44100")), qPrintable(error));
    QVERIFY(pcm.empty());
    QCOMPARE(sampleRate, 0);
}

void TestSupertonicEngine::rejectsWrongStreamingRate()
{
    auto api = std::make_unique<FakeSupertonicApi>();
    FakeSupertonicApi *fake = api.get();
    fake->resultSampleRate = 48000;
    SupertonicEngine engine(std::move(api));

    int sinkCalls = 0;
    const auto sink = [&](const float *, size_t, bool) {
        ++sinkCalls;
        return true;
    };

    int sampleRate = 123;
    QString error;
    QVERIFY(!engine.synthesizeStreaming(QStringLiteral("rate"), sink, &sampleRate, &error));
    QCOMPARE(sinkCalls, 0);
    QCOMPARE(sampleRate, 0);
    QVERIFY2(error.contains(QStringLiteral("48000")), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("44100")), qPrintable(error));
}

void TestSupertonicEngine::rejectsNullApi()
{
    SupertonicEngine engine(nullptr);

    std::vector<float> pcm;
    int sampleRate = 0;
    QString error;
    QVERIFY(!engine.synthesize(QStringLiteral("null"), &pcm, &sampleRate, &error));
    QVERIFY(!error.isEmpty());
}

void TestSupertonicEngine::cancelIsForwarded()
{
    auto api = std::make_unique<FakeSupertonicApi>();
    FakeSupertonicApi *fake = api.get();
    SupertonicEngine engine(std::move(api));

    engine.cancel();
    engine.resetCancel();
    QCOMPARE(fake->cancelCount, 1);
    QCOMPARE(fake->resetCancelCount, 1);
}

void TestSupertonicEngine::forwardsInitializationFailure()
{
    auto api = std::make_unique<FakeSupertonicApi>();
    FakeSupertonicApi *fake = api.get();
    fake->initializationFailedFlag = true;
    SupertonicEngine engine(std::move(api));

    QVERIFY(engine.initializationFailed());
}

void TestSupertonicEngine::streamsResampledChunks()
{
    auto api = std::make_unique<FakeSupertonicApi>();
    FakeSupertonicApi *fake = api.get();
    SupertonicEngine engine(std::move(api));

    std::vector<size_t> sizes;
    std::vector<bool> lastFlags;
    const auto sink = [&](const float *samples, size_t count, bool isLast) {
        Q_UNUSED(samples);
        sizes.push_back(count);
        lastFlags.push_back(isLast);
        return true;
    };

    int sampleRate = 0;
    QString error;
    QVERIFY2(engine.synthesizeStreaming(QStringLiteral("stream"), sink, &sampleRate, &error),
             qPrintable(error));

    QCOMPARE(sampleRate, kWorkerSampleRate);
    QCOMPARE(sizes.size(), size_t(2));
    QCOMPARE(sizes.at(0), size_t(50));
    QCOMPARE(sizes.at(1), size_t(100));
    QCOMPARE(lastFlags.at(0), false);
    QCOMPARE(lastFlags.at(1), true);
    QCOMPARE(fake->calls.size(), size_t(1));
}

void TestSupertonicEngine::emptyOutputIsSuccessful()
{
    auto api = std::make_unique<FakeSupertonicApi>();
    FakeSupertonicApi *fake = api.get();
    fake->sampleCount = 0;
    SupertonicEngine engine(std::move(api));

    std::vector<float> pcm{0.5f};
    int sampleRate = 0;
    QString error;
    QVERIFY(engine.synthesize(QStringLiteral("empty"), &pcm, &sampleRate, &error));
    QVERIFY(pcm.empty());
    QCOMPARE(sampleRate, kWorkerSampleRate);
    QVERIFY(error.isEmpty());
}

QTEST_GUILESS_MAIN(TestSupertonicEngine)

#include "test_supertonic_engine.moc"
