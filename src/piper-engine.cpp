#include "piper-engine.hpp"

#include <QtGlobal>

#include <utility>

namespace {
constexpr int kPiperSampleRate = 22050;
constexpr float kBaseRate = 165.0f;
constexpr float kMinLengthScale = 0.5f;
constexpr float kMaxLengthScale = 2.0f;

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

    const float lengthScale = speed_ > 0.0f
                                  ? qBound(kMinLengthScale, speed_, kMaxLengthScale)
                                  : qBound(kMinLengthScale, kBaseRate / float(rate_), kMaxLengthScale);
    if (!api_->synthesize(text, lengthScale, pcm, sampleRate, error)) {
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
