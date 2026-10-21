#pragma once

#include <QString>

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
    virtual ~TtsEngine() = default;
    virtual bool synthesize(const QString &text, std::vector<float> *pcm, int *sampleRate,
                            QString *error) = 0;
    virtual void cancel() = 0;
    virtual void resetCancel() = 0;

    /* True when start-up failed permanently (for example a corrupt voice
     * model) and synthesize() will keep failing.  The worker then hands the
     * current message to a replacement engine instead of retrying this one. */
    virtual bool initializationFailed() const { return false; }
};
