#pragma once

#include <memory>

#include <QString>

#include "supertonic-api.hpp"

/**
 * SupertonicApi backed by the bundled ONNX models.
 *
 * This header deliberately exposes no ONNX Runtime types.  The model is loaded
 * lazily on the first synthesis, which runs on the TTS worker thread, and is
 * reused for every later utterance.  cancel() is a signal from any thread: it
 * only sets an atomic flag and never touches the sessions.
 *
 * modelDir contains `onnx/` and may also be the onnx directory itself.  The
 * caller resolves voicePath from the bundled voice_styles directory.
 */
class LibSupertonicApi : public SupertonicApi {
public:
    LibSupertonicApi(QString modelDir, QString voicePath, QString language, int threads);
    ~LibSupertonicApi() override;

    LibSupertonicApi(const LibSupertonicApi &) = delete;
    LibSupertonicApi &operator=(const LibSupertonicApi &) = delete;

    bool synthesize(const QString &text, float speed, int steps, std::vector<float> *pcm,
                    int *sampleRate, QString *error) override;
    bool synthesizeStreaming(const QString &text, float speed, int steps,
                             const ChunkSink &sink, int *sampleRate, QString *error) override;
    void cancel() override;
    void resetCancel() override;
    bool initializationFailed() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
