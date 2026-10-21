#pragma once

#include <QString>

#include <vector>

/**
 * Narrow boundary around Piper synthesis.
 *
 * PiperEngine depends on this interface rather than libpiper/ONNX Runtime so
 * the engine can be unit-tested on hosts without the native backend.  The real
 * implementation (Task 3) lazily creates the Piper synthesizer on the TTS
 * worker thread; fakes return canned PCM.
 *
 * synthesize() runs on the TTS worker thread.  cancel() may be called from any
 * thread and must only mark cancellation state.
 */
class PiperApi {
public:
    virtual ~PiperApi() = default;
    virtual bool synthesize(const QString &text, float lengthScale,
                            std::vector<float> *pcm, int *sampleRate,
                            QString *error) = 0;
    virtual void cancel() = 0;
    virtual void resetCancel() = 0;
};
