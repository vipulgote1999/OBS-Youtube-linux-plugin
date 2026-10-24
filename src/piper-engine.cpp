#include "piper-engine.hpp"

#include <QtGlobal>

#include <utility>

namespace {
constexpr int kPiperSampleRate = 22050;
constexpr float kBaseRate = 165.0f;
constexpr float kMinLengthScale = 1.0f / 3.0f;
constexpr float kMaxLengthScale = 3.0f;

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}
} // namespace

PiperEngine::PiperEngine(std::unique_ptr<PiperApi> api, int rate)
    : api_(std::move(api)), rate_(rate)
{
}

bool PiperEngine::synthesize(const QString &text, std::vector<float> *pcm, int *sampleRate,
                             QString *error)
{
    if (error)
        error->clear();
    if (!pcm || !sampleRate)
        return fail(error, QStringLiteral("invalid TTS output buffer"));
    pcm->clear();
    *sampleRate = 0;

    if (!api_)
        return fail(error, QStringLiteral("piper backend is not available"));

    if (!api_->synthesize(text, lengthScale(), pcm, sampleRate, error)) {
        if (error && error->isEmpty())
            *error = QStringLiteral("piper synthesis failed");
        return false;
    }

    if (*sampleRate != kPiperSampleRate) {
        const int actualRate = *sampleRate;
        pcm->clear();
        *sampleRate = 0;
        return fail(error, QStringLiteral("piper returned %1 Hz; expected %2 Hz")
                               .arg(actualRate)
                               .arg(kPiperSampleRate));
    }

    return true;
}

bool PiperEngine::synthesizeStreaming(const QString &text, const ChunkSink &sink,
                                      int *sampleRate, QString *error)
{
    if (error)
        error->clear();
    if (!sampleRate)
        return fail(error, QStringLiteral("invalid TTS output buffer"));
    *sampleRate = 0;

    if (!api_)
        return fail(error, QStringLiteral("piper backend is not available"));

    if (!api_->synthesizeStreaming(text, lengthScale(), sink, sampleRate, error)) {
        if (error && error->isEmpty())
            *error = QStringLiteral("piper synthesis failed");
        return false;
    }

    if (*sampleRate != kPiperSampleRate) {
        const int actualRate = *sampleRate;
        *sampleRate = 0;
        return fail(error, QStringLiteral("piper returned %1 Hz; expected %2 Hz")
                               .arg(actualRate)
                               .arg(kPiperSampleRate));
    }

    return true;
}

float PiperEngine::lengthScale() const
{
    /* Piper's length_scale is the inverse of playback speed: 1.0 is normal,
     * 3.0 is one-third speed, 1/3 is triple speed.  The UI speed is the
     * familiar multiplier (3.0x = three times as fast), so invert it here.
     * A zero (or negative) speed is treated as the slowest supported speed. */
    if (speed_ > 0.0f)
        return qBound(kMinLengthScale, 1.0f / speed_, kMaxLengthScale);
    if (speed_ == 0.0f)
        return kMaxLengthScale;
    return qBound(kMinLengthScale, kBaseRate / float(rate_), kMaxLengthScale);
}

void PiperEngine::cancel()
{
    if (api_)
        api_->cancel();
}

void PiperEngine::resetCancel()
{
    if (api_)
        api_->resetCancel();
}

void PiperEngine::setSpeed(float speed)
{
    speed_ = speed;
}

bool PiperEngine::initializationFailed() const
{
    return api_ && api_->initializationFailed();
}
