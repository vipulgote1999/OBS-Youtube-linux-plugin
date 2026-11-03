#pragma once

#include <memory>

#include <QString>

#include "supertonic-api.hpp"
#include "tts-engine.hpp"

/**
 * TtsEngine backed by an injected SupertonicApi.
 *
 * The model produces 44,100 Hz audio; this adapter resamples it to the
 * worker's 22,050 Hz contract.  Speed and denoising steps are clamped to the
 * model's practical range so a hand-edited settings file cannot ask for a
 * 200-step utterance.
 */
class SupertonicEngine : public TtsEngine {
public:
    explicit SupertonicEngine(std::unique_ptr<SupertonicApi> api);

    bool synthesize(const QString &text, std::vector<float> *pcm, int *sampleRate,
                    QString *error) override;
    bool synthesizeStreaming(const QString &text, const ChunkSink &sink, int *sampleRate,
                             QString *error) override;
    void cancel() override;
    void resetCancel() override;

    /* Playback speed multiplier (1.0 = normal, 2.0 = twice as fast). */
    void setSpeed(float speed);
    /* Flow-matching denoising steps; higher is slower and usually cleaner. */
    void setSteps(int steps);

    bool initializationFailed() const override;

private:
    std::unique_ptr<SupertonicApi> api_;
    float speed_ = 1.0f;
    int steps_ = 8;
};
