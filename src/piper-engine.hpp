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
    void cancel() override;
    void resetCancel() override;
    bool initializationFailed() const override;

private:
    std::unique_ptr<PiperApi> api_;
    int rate_;
};
