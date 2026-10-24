#pragma once

#include <atomic>
#include <mutex>

#include <QString>

#include "piper-api.hpp"

// Keep this header free of <piper.h> so code that only mentions the type can
// include it without the native runtime headers.  The definition is
// `typedef struct piper_synthesizer piper_synthesizer;` in the C API.
struct piper_synthesizer;

/**
 * PiperApi backed by the bundled libpiper/ONNX Runtime.
 *
 * The synthesizer is created lazily on the first synthesize() call, which runs
 * on the TTS worker thread, and is then reused for every later utterance so a
 * message never reloads the 61 MB model.  cancel() is a signal from any thread:
 * it only sets an atomic flag and never touches libpiper, because a synthesis
 * may be in progress.  The destructor frees the synthesizer after the worker
 * has stopped.
 */
class LibPiperApi : public PiperApi {
public:
    LibPiperApi(QString modelPath, QString configPath, QString espeakDataPath);
    ~LibPiperApi() override;

    LibPiperApi(const LibPiperApi &) = delete;
    LibPiperApi &operator=(const LibPiperApi &) = delete;

    bool synthesize(const QString &text, float lengthScale, std::vector<float> *pcm,
                    int *sampleRate, QString *error) override;
    bool synthesizeStreaming(const QString &text, float lengthScale, const ChunkSink &sink,
                             int *sampleRate, QString *error) override;
    void cancel() override;
    void resetCancel() override;
    bool initializationFailed() const override;

private:
    /* Shared implementation.  Exactly one of pcm/sink is non-null. */
    bool run(const QString &text, float lengthScale, std::vector<float> *pcm,
             const ChunkSink *sink, int *sampleRate, QString *error);
    bool ensureSynthesizer(QString *error);
    // Frees and nulls synthesizer_; callers must hold synthesizerMutex_.
    void destroySynthesizerLocked() noexcept;

    QString modelPath_;
    QString configPath_;
    QString espeakDataPath_;

    // Guards lazy creation and the destructor's piper_free().  cancel() must
    // not take this lock: it may be called while synthesize() holds it.
    std::mutex synthesizerMutex_;
    piper_synthesizer *synthesizer_ = nullptr;

    // Latched after a failed piper_create() so a bad model is not reloaded on
    // every utterance.  Cleared only by a successful create.
    std::atomic_bool loadFailed_{false};
    QString loadError_; // written under synthesizerMutex_

    std::atomic_bool cancelRequested_{false};
};
