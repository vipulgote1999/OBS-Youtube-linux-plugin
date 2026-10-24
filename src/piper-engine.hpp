#pragma once

#include <memory>

#include <QString>

#include "piper-api.hpp"
#include "tts-engine.hpp"

/**
 * TtsEngine backed by an injected PiperApi.
 *
 * The engine is deliberately independent of libpiper and ONNX Runtime: the
 * native backend is supplied as a PiperApi so rate mapping, error handling and
 * cancellation can be tested on hosts without the runtime.  The OBS rate is
 * translated to Piper's length scale (165 / rate, clamped to 0.5-2.0), so a
 * higher rate speaks faster.
 */
class PiperEngine : public TtsEngine {
public:
    PiperEngine(std::unique_ptr<PiperApi> api, int rate = 165);

    bool synthesize(const QString &text, std::vector<float> *pcm, int *sampleRate,
                    QString *error) override;
    bool synthesizeStreaming(const QString &text, const ChunkSink &sink, int *sampleRate,
                             QString *error) override;
    void cancel() override;
    void resetCancel() override;

    /* Playback speed multiplier (1.0 = normal, 3.0 = three times as fast).
     * Zero is treated as the slowest supported speed.  The engine converts
     * it to Piper's inverse length scale.  When unset, the legacy
     * rate->length-scale mapping is used. */
    void setSpeed(float speed);
    bool initializationFailed() const override;

private:
    float lengthScale() const;
    std::unique_ptr<PiperApi> api_;
    int rate_;
    float speed_ = 0.0f;
};
