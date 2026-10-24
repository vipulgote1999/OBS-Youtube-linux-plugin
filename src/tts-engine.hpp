#pragma once

#include <QString>

#include <cstddef>
#include <functional>
#include <vector>

/**
 * Converts text to mono float PCM at 22,050 Hz.
 *
 * synthesize() is called from the TTS worker thread.  cancel() is an
 * asynchronous signal that may be called from any thread and must only mark
 * state; it must never touch the engine's subprocess directly.
 *
 * resetCancel() clears a previous cancel() mark and is called by the worker
 * immediately before each utterance, after its running check.  Implementations
 * must not clear the cancel mark inside synthesize(): a stop() that lands
 * between the worker's check and synthesize() would otherwise be lost.
 */
class TtsEngine {
public:
    /* Chunk callback for the low-latency path.  Returning false stops
     * synthesis; isLast marks the final chunk so the caller can fade out. */
    using ChunkSink = std::function<bool(const float *samples, size_t count, bool isLast)>;

    virtual ~TtsEngine() = default;
    virtual bool synthesize(const QString &text, std::vector<float> *pcm, int *sampleRate,
                            QString *error) = 0;

    /**
     * Optional streaming variant.
     *
     * Default: synthesize the whole utterance and offer it as one final chunk,
     * so engines without a chunked API (espeak, local server) work unchanged.
     * Piper overrides this to emit each libpiper chunk as it arrives.
     */
    virtual bool synthesizeStreaming(const QString &text, const ChunkSink &sink,
                                     int *sampleRate, QString *error)
    {
        std::vector<float> pcm;
        if (!synthesize(text, &pcm, sampleRate, error))
            return false;
        if (pcm.empty())
            return true;
        return sink(pcm.data(), pcm.size(), true);
    }
    virtual void cancel() = 0;
    virtual void resetCancel() = 0;

    /* True when start-up failed permanently (for example a corrupt voice
     * model) and synthesize() will keep failing.  The worker then hands the
     * current message to a replacement engine instead of retrying this one. */
    virtual bool initializationFailed() const { return false; }
};
