#pragma once

#include <atomic>

#include <QString>

#include "tts-engine.hpp"

/**
 * Speech engine backed by the bundled espeak-ng binary.
 *
 * The binary is started directly with QProcess (never through a shell) and
 * its stdout WAV is parsed by parseWav().  ESpeak data is selected with the
 * ESPEAK_DATA_PATH environment variable so the Flatpak extension layout works
 * without a system-wide install.
 */
class EspeakEngine : public TtsEngine {
public:
    EspeakEngine(QString binaryPath, QString dataPath, QString voice = QStringLiteral("en-us"),
                 int rate = 165, int pitch = 50, int amplitude = 150);

    bool synthesize(const QString &text, std::vector<float> *pcm, int *sampleRate,
                    QString *error) override;
    void cancel() override;
    void resetCancel() override;

private:
    QString binaryPath_;
    QString dataPath_;
    QString voice_;
    int rate_;
    int pitch_;
    int amplitude_;
    std::atomic<bool> cancel_{false};
};
