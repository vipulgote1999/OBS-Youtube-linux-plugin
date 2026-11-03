#include "supertonic-engine.hpp"

#include "audio-resampler.hpp"

#include <QtGlobal>

#include <cmath>
#include <utility>

namespace {
constexpr int kModelSampleRate = 44100;
constexpr int kWorkerSampleRate = 22050;
constexpr float kMinSpeed = 0.5f;
constexpr float kMaxSpeed = 2.0f;
constexpr int kMinSteps = 4;
constexpr int kMaxSteps = 16;

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}
} // namespace

SupertonicEngine::SupertonicEngine(std::unique_ptr<SupertonicApi> api)
    : api_(std::move(api))
{
}

bool SupertonicEngine::synthesize(const QString &text, std::vector<float> *pcm,
                                  int *sampleRate, QString *error)
{
    if (error)
        error->clear();
    if (!pcm || !sampleRate)
        return fail(error, QStringLiteral("invalid TTS output buffer"));
    pcm->clear();
    *sampleRate = 0;

    if (!api_)
        return fail(error, QStringLiteral("supertonic backend is not available"));

    std::vector<float> native;
    int nativeRate = 0;
    if (!api_->synthesize(text, speed_, steps_, &native, &nativeRate, error)) {
        if (error && error->isEmpty())
            *error = QStringLiteral("supertonic synthesis failed");
        return false;
    }

    if (nativeRate != kModelSampleRate) {
        const int actualRate = nativeRate;
        pcm->clear();
        *sampleRate = 0;
        return fail(error, QStringLiteral("supertonic returned %1 Hz; expected %2 Hz")
                               .arg(actualRate)
                               .arg(kModelSampleRate));
    }

    if (!native.empty())
        *pcm = AudioResampler::resample(native, kModelSampleRate, kWorkerSampleRate);
    *sampleRate = kWorkerSampleRate;
    return true;
}

bool SupertonicEngine::synthesizeStreaming(const QString &text, const ChunkSink &sink,
                                           int *sampleRate, QString *error)
{
    if (error)
        error->clear();
    if (!sampleRate)
        return fail(error, QStringLiteral("invalid TTS output buffer"));
    *sampleRate = 0;

    if (!api_)
        return fail(error, QStringLiteral("supertonic backend is not available"));

    const auto resamplingSink = [&](const float *samples, size_t count, bool isLast) {
        const std::vector<float> chunk(samples, samples + count);
        if (chunk.empty())
            return true;
        const std::vector<float> resampled =
            AudioResampler::resample(chunk, kModelSampleRate, kWorkerSampleRate);
        if (resampled.empty())
            return true;
        return sink(resampled.data(), resampled.size(), isLast);
    };

    int nativeRate = 0;
    if (!api_->synthesizeStreaming(text, speed_, steps_, resamplingSink, &nativeRate, error)) {
        if (error && error->isEmpty())
            *error = QStringLiteral("supertonic synthesis failed");
        return false;
    }

    if (nativeRate != kModelSampleRate) {
        const int actualRate = nativeRate;
        *sampleRate = 0;
        return fail(error, QStringLiteral("supertonic returned %1 Hz; expected %2 Hz")
                               .arg(actualRate)
                               .arg(kModelSampleRate));
    }

    *sampleRate = kWorkerSampleRate;
    return true;
}

void SupertonicEngine::cancel()
{
    if (api_)
        api_->cancel();
}

void SupertonicEngine::resetCancel()
{
    if (api_)
        api_->resetCancel();
}

void SupertonicEngine::setSpeed(float speed)
{
    /* A settings file or a spin box can produce NaN when parsed oddly; fall
     * back to normal speed rather than letting qBound propagate it. */
    if (!std::isfinite(speed)) {
        speed_ = 1.0f;
        return;
    }
    speed_ = qBound(kMinSpeed, speed, kMaxSpeed);
}

void SupertonicEngine::setSteps(int steps)
{
    steps_ = qBound(kMinSteps, steps, kMaxSteps);
}

bool SupertonicEngine::initializationFailed() const
{
    return api_ && api_->initializationFailed();
}
