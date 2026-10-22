#include <QtTest/QtTest>

#include <memory>
#include <vector>

#include "piper-engine.hpp"

namespace {
constexpr int kPiperSampleRate = 22050;
constexpr size_t kFakeSampleCount = 1000;

class FakePiperApi : public PiperApi {
public:
    struct Call {
        QString text;
        float lengthScale = 0.0f;
    };

    std::vector<Call> calls;
    int cancelCount = 0;
    int resetCancelCount = 0;

    bool succeed = true;
    bool setErrorOnFailure = true;
    bool initializationFailedFlag = false;
    int resultSampleRate = kPiperSampleRate;

    bool synthesize(const QString &text, float lengthScale, std::vector<float> *pcm,
                    int *sampleRate, QString *error) override
    {
        calls.push_back({text, lengthScale});
        if (!succeed) {
            if (error && setErrorOnFailure)
                *error = QStringLiteral("fake piper failure");
            return false;
        }

        if (pcm)
            pcm->assign(kFakeSampleCount, 0.25f);
        if (sampleRate)
            *sampleRate = resultSampleRate;
        return true;
    }

    void cancel() override { ++cancelCount; }
    void resetCancel() override { ++resetCancelCount; }
    bool initializationFailed() const override { return initializationFailedFlag; }
};
} // namespace

class TestPiperEngine : public QObject {
    Q_OBJECT

private slots:
    void returnsPcmAndRate();
    void mapsRateToClampedLengthScale();
    void mapsSpeedInverseToLengthScale();
    void reportsApiFailure();
    void rejectsWrongSampleRate();
    void rejectsNullApi();
    void cancelIsForwarded();
    void forwardsInitializationFailure();
};

void TestPiperEngine::returnsPcmAndRate()
{
    auto api = std::make_unique<FakePiperApi>();
    FakePiperApi *fake = api.get();
    PiperEngine engine(std::move(api));

    std::vector<float> pcm{0.75f};
    int sampleRate = -1;
    QString error = QStringLiteral("stale error");

    QVERIFY2(engine.synthesize(QStringLiteral("hello"), &pcm, &sampleRate, &error),
             qPrintable(error));
    QCOMPARE(pcm.size(), kFakeSampleCount);
    QVERIFY(qAbs(pcm.at(0) - 0.25f) < 0.0001f);
    QCOMPARE(sampleRate, kPiperSampleRate);
    QVERIFY(error.isEmpty());

    QCOMPARE(fake->calls.size(), size_t(1));
    QCOMPARE(fake->calls.at(0).text, QStringLiteral("hello"));
    QVERIFY(qAbs(fake->calls.at(0).lengthScale - 1.0f) < 0.0001f);
}

void TestPiperEngine::mapsRateToClampedLengthScale()
{
    struct Case {
        int rate;
        float expected;
    };
    const Case cases[] = {
        {165, 1.0f},
        {80, 2.0f},
        {450, 0.5f},
    };

    for (const Case &testCase : cases) {
        auto api = std::make_unique<FakePiperApi>();
        FakePiperApi *fake = api.get();
        PiperEngine engine(std::move(api), testCase.rate);

        std::vector<float> pcm;
        int sampleRate = 0;
        QString error;
        QVERIFY2(engine.synthesize(QStringLiteral("rate"), &pcm, &sampleRate, &error),
                 qPrintable(error));
        QCOMPARE(fake->calls.size(), size_t(1));
        QVERIFY2(qAbs(fake->calls.at(0).lengthScale - testCase.expected) < 0.0001f,
                 qPrintable(QStringLiteral("rate %1 produced length scale %2")
                                .arg(testCase.rate)
                                .arg(fake->calls.at(0).lengthScale)));
    }
}

void TestPiperEngine::mapsSpeedInverseToLengthScale()
{
    struct Case {
        float speed;
        float expected;
    };
    const Case cases[] = {
        {0.0f, 3.0f},
        {3.0f, 1.0f / 3.0f},
        {2.0f, 0.5f},
        {1.0f, 1.0f},
        {0.85f, 1.0f / 0.85f},
        {0.5f, 2.0f},
        {0.25f, 2.0f},
        {4.0f, 1.0f / 3.0f},
    };

    for (const Case &testCase : cases) {
        auto api = std::make_unique<FakePiperApi>();
        FakePiperApi *fake = api.get();
        PiperEngine engine(std::move(api));
        engine.setSpeed(testCase.speed);

        std::vector<float> pcm;
        int sampleRate = 0;
        QString error;
        QVERIFY2(engine.synthesize(QStringLiteral("speed"), &pcm, &sampleRate, &error),
                 qPrintable(error));
        QCOMPARE(fake->calls.size(), size_t(1));
        QVERIFY2(qAbs(fake->calls.at(0).lengthScale - testCase.expected) < 0.0001f,
                 qPrintable(QStringLiteral("speed %1 produced length scale %2")
                                .arg(testCase.speed)
                                .arg(fake->calls.at(0).lengthScale)));
    }
}

void TestPiperEngine::reportsApiFailure()
{
    auto api = std::make_unique<FakePiperApi>();
    FakePiperApi *fake = api.get();
    fake->succeed = false;
    PiperEngine engine(std::move(api));

    std::vector<float> pcm{0.5f};
    int sampleRate = 123;
    QString error;
    QVERIFY(!engine.synthesize(QStringLiteral("boom"), &pcm, &sampleRate, &error));
    QVERIFY2(error.contains(QStringLiteral("fake piper failure")), qPrintable(error));
    QVERIFY(pcm.empty());
    QCOMPARE(sampleRate, 0);

    auto silentApi = std::make_unique<FakePiperApi>();
    FakePiperApi *silentFake = silentApi.get();
    silentFake->succeed = false;
    silentFake->setErrorOnFailure = false;
    PiperEngine silentEngine(std::move(silentApi));

    std::vector<float> silentPcm;
    int silentRate = 0;
    QString silentError;
    QVERIFY(!silentEngine.synthesize(QStringLiteral("boom"), &silentPcm, &silentRate,
                                     &silentError));
    QVERIFY2(!silentError.isEmpty(), "engine must report an error when the API sets none");
}

void TestPiperEngine::rejectsWrongSampleRate()
{
    auto api = std::make_unique<FakePiperApi>();
    FakePiperApi *fake = api.get();
    fake->resultSampleRate = 16000;
    PiperEngine engine(std::move(api));

    std::vector<float> pcm{0.5f};
    int sampleRate = 123;
    QString error;
    QVERIFY(!engine.synthesize(QStringLiteral("wrong rate"), &pcm, &sampleRate, &error));
    QVERIFY2(error.contains(QStringLiteral("22050")), qPrintable(error));
    QVERIFY(pcm.empty());
    QCOMPARE(sampleRate, 0);
}

void TestPiperEngine::rejectsNullApi()
{
    PiperEngine engine(nullptr);

    std::vector<float> pcm{0.5f};
    int sampleRate = 123;
    QString error;
    QVERIFY(!engine.synthesize(QStringLiteral("no api"), &pcm, &sampleRate, &error));
    QVERIFY2(!error.isEmpty(), "missing API must produce an error");
    QVERIFY(pcm.empty());
    QCOMPARE(sampleRate, 0);

    /* Cancellation forwarding must tolerate a missing backend. */
    engine.cancel();
    engine.resetCancel();
}

void TestPiperEngine::cancelIsForwarded()
{
    auto api = std::make_unique<FakePiperApi>();
    FakePiperApi *fake = api.get();
    PiperEngine engine(std::move(api));

    QCOMPARE(fake->cancelCount, 0);
    engine.cancel();
    QCOMPARE(fake->cancelCount, 1);
    engine.cancel();
    QCOMPARE(fake->cancelCount, 2);

    QCOMPARE(fake->resetCancelCount, 0);
    engine.resetCancel();
    QCOMPARE(fake->resetCancelCount, 1);
    engine.resetCancel();
    QCOMPARE(fake->resetCancelCount, 2);
}

void TestPiperEngine::forwardsInitializationFailure()
{
    auto brokenApi = std::make_unique<FakePiperApi>();
    brokenApi->initializationFailedFlag = true;
    PiperEngine broken(std::move(brokenApi));
    QVERIFY(broken.initializationFailed());

    auto healthyApi = std::make_unique<FakePiperApi>();
    PiperEngine healthy(std::move(healthyApi));
    QVERIFY(!healthy.initializationFailed());

    /* A missing backend cannot have initialized, but it also has no latched
     * failure to report; the worker treats it as an ordinary error. */
    PiperEngine noApi(nullptr);
    QVERIFY(!noApi.initializationFailed());
}

QTEST_GUILESS_MAIN(TestPiperEngine)

#include "test_piper_engine.moc"
