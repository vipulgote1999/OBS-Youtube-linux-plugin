#pragma once

#include <QString>

#include <cstddef>
#include <functional>
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
    using ChunkSink = std::function<bool(const float *samples, size_t count, bool isLast)>;

    virtual ~PiperApi() = default;
    virtual bool synthesize(const QString &text, float lengthScale,
                            std::vector<float> *pcm, int *sampleRate,
                            QString *error) = 0;

    /**
     * Chunked variant.  Default: buffer the whole utterance and emit one final
     * chunk; LibPiperApi overrides it to forward libpiper's own chunks.
     */
    virtual bool synthesizeStreaming(const QString &text, float lengthScale,
                                     const ChunkSink &sink, int *sampleRate, QString *error)
    {
        std::vector<float> pcm;
        if (!synthesize(text, lengthScale, &pcm, sampleRate, error))
            return false;
        if (pcm.empty())
            return true;
        return sink(pcm.data(), pcm.size(), true);
    }
    virtual void cancel() = 0;
    virtual void resetCancel() = 0;

    /* Set once model/config creation has failed permanently.  Implementations
     * must not retry a bad model on every utterance. */
    virtual bool initializationFailed() const { return false; }
};
