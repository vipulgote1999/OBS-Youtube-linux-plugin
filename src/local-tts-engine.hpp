#pragma once

#include <atomic>

#include <QString>

#include "tts-engine.hpp"

/**
 * TtsEngine backed by a local OpenAI-compatible TTS server.
 *
 * This is the quality option: the plugin does not bundle Kokoro or Pocket TTS,
 * it talks to a server the user runs (Kokoro-FastAPI, PocketTTS.cpp,
 * pocket-tts-openai_streaming_server, ...).  That keeps model licensing and
 * ~100 MB weights out of the Flatpak, and keeps an unstable model out of the
 * plugin process.
 *
 * Uses a blocking QTcpSocket on the TTS worker thread: the request is short
 * and local, and a worker-thread event loop would complicate cancellation.
 * cancel() only sets an atomic flag; the read loop aborts the socket.
 */
class LocalTtsEngine : public TtsEngine {
public:
    LocalTtsEngine(QString url, QString voice, QString model);

    bool synthesize(const QString &text, std::vector<float> *pcm, int *sampleRate,
                    QString *error) override;
    void cancel() override;
    void resetCancel() override;

private:
    bool post(const QByteArray &body, QByteArray *response, QString *error);

    QString url_;
    QString voice_;
    QString model_;
    std::atomic_bool cancelRequested_{false};
};
