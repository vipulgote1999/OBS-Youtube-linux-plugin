#pragma once

#include <QString>

#include <cstddef>
#include <functional>
#include <vector>

/**
 * Narrow boundary around Supertonic synthesis.
 *
 * SupertonicEngine depends on this interface rather than ONNX Runtime so the
 * adapter can be unit-tested on hosts without the native backend.  The real
 * implementation lazily loads the model on the TTS worker thread and returns
 * mono float PCM at the model's native 44,100 Hz; SupertonicEngine converts it
 * to the worker contract of 22,050 Hz.
 *
 * synthesize() runs on the TTS worker thread.  cancel() may be called from any
 * thread and must only mark cancellation state.
 */
class SupertonicApi {
public:
    using ChunkSink = std::function<bool(const float *samples, size_t count, bool isLast)>;

    virtual ~SupertonicApi() = default;

    virtual bool synthesize(const QString &text, float speed, int steps,
                            std::vector<float> *pcm, int *sampleRate, QString *error) = 0;

    /**
     * Chunked variant.  Default: buffer the whole utterance and emit one final
     * chunk; LibSupertonicApi overrides it to emit each sentence chunk as it
     * is produced.
     */
    virtual bool synthesizeStreaming(const QString &text, float speed, int steps,
                                     const ChunkSink &sink, int *sampleRate, QString *error)
    {
        std::vector<float> pcm;
        if (!synthesize(text, speed, steps, &pcm, sampleRate, error))
            return false;
        if (pcm.empty())
            return true;
        return sink(pcm.data(), pcm.size(), true);
    }

    virtual void cancel() = 0;
    virtual void resetCancel() = 0;

    /* Set once model creation has failed permanently.  Implementations must
     * not retry a bad model on every utterance. */
    virtual bool initializationFailed() const { return false; }
};
