#include "speech-conditioner.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;
} // namespace

void SpeechConditioner::process(std::vector<float> *pcm)
{
    process(pcm, Config());
}

void SpeechConditioner::process(std::vector<float> *pcm, const Config &config)
{
    if (!pcm || pcm->empty())
        return;

    const int rate = config.sampleRate > 0 ? config.sampleRate : 22050;

    /* 1. DC blocker: y[n] = a * (y[n-1] + x[n] - x[n-1]).
     *    A constant offset decays to zero with a ~8 ms time constant at
     *    20 Hz, while the speech band is untouched. */
    if (config.dcBlockHz > 0.0f) {
        const float a = std::exp(-kTwoPi * config.dcBlockHz / static_cast<float>(rate));
        float xPrev = (*pcm)[0];
        float yPrev = 0.0f;
        for (float &sample : *pcm) {
            const float x = sample;
            const float y = a * (yPrev + x - xPrev);
            xPrev = x;
            yPrev = y;
            sample = y;
        }
    }

    /* 2. Raised-cosine fade in and out.  The first and last samples become
     *    exactly zero, so the transition to the silence the audio feeder
     *    produces is continuous. */
    const std::size_t n = pcm->size();
    const std::size_t requested = static_cast<std::size_t>(
        std::lround(config.fadeMs * rate / 1000.0f));
    const std::size_t fade = std::min(requested, n / 2);

    if (fade == 0) {
        (*pcm)[0] = 0.0f;
        (*pcm)[n - 1] = 0.0f;
    } else {
        const float denominator = fade > 1 ? static_cast<float>(fade - 1) : 1.0f;
        for (std::size_t i = 0; i < fade; ++i) {
            const float gain = 0.5f * (1.0f - std::cos(kTwoPi * 0.5f * i / denominator));
            (*pcm)[i] *= gain;
            (*pcm)[n - 1 - i] *= gain;
        }
    }

    /* 3. The high-pass can overshoot slightly on a sharp transient.  Clamp so
     *    the downstream buffer never has to. */
    for (float &sample : *pcm)
        sample = std::clamp(sample, -1.0f, 1.0f);
}

SpeechConditioner::Stream::Stream(const Config &config) : config_(config)
{
    const int rate = config_.sampleRate > 0 ? config_.sampleRate : 22050;
    dcA_ = std::exp(-kTwoPi * config_.dcBlockHz / static_cast<float>(rate));
    fadeSamples_ = static_cast<size_t>(
        std::lround(config_.fadeMs * rate / 1000.0f));
    fadeInRemaining_ = fadeSamples_;
}

void SpeechConditioner::Stream::process(std::vector<float> *pcm, bool first, bool last)
{
    if (!pcm || pcm->empty())
        return;

    if (first || !started_) {
        xPrev_ = (*pcm)[0];
        yPrev_ = 0.0f;
        fadeInRemaining_ = fadeSamples_;
        started_ = true;
    }

    for (float &sample : *pcm) {
        const float x = sample;
        const float y = dcA_ * (yPrev_ + x - xPrev_);
        xPrev_ = x;
        yPrev_ = y;
        sample = y;
    }

    const std::size_t n = pcm->size();
    const float denominator =
        fadeSamples_ > 1 ? static_cast<float>(fadeSamples_ - 1) : 1.0f;

    const std::size_t fadeIn = std::min(fadeInRemaining_, n);
    for (std::size_t i = 0; i < fadeIn; ++i) {
        const std::size_t pos = fadeSamples_ - fadeInRemaining_ + i;
        const float gain =
            0.5f * (1.0f - std::cos(kTwoPi * 0.5f * static_cast<float>(pos) / denominator));
        (*pcm)[i] *= gain;
    }
    fadeInRemaining_ -= fadeIn;

    if (last) {
        const std::size_t fadeOut = std::min(fadeSamples_, n);
        /* Span whatever tail this chunk has: a short final chunk must still
         * reach unity at its start, or the previous chunk ends with a step. */
        const float outDenominator =
            fadeOut > 1 ? static_cast<float>(fadeOut - 1) : 1.0f;
        for (std::size_t i = 0; i < fadeOut; ++i) {
            const std::size_t pos = fadeOut - 1 - i;
            const float gain = 0.5f
                               * (1.0f
                                  - std::cos(kTwoPi * 0.5f * static_cast<float>(pos)
                                             / outDenominator));
            (*pcm)[n - 1 - i] *= gain;
        }
        (*pcm)[n - 1] = 0.0f;
    }

    for (float &sample : *pcm)
        sample = std::clamp(sample, -1.0f, 1.0f);
}
