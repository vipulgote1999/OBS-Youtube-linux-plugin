#include "libpiper-api.hpp"

#include <piper.h>

#include <QtGlobal>

#include <cstddef>
#include <utility>

namespace {
constexpr int kPiperSampleRate = 22050;

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

QByteArray toNativePath(const QString &path)
{
    return path.toUtf8();
}
} // namespace

LibPiperApi::LibPiperApi(QString modelPath, QString configPath, QString espeakDataPath)
    : modelPath_(std::move(modelPath)),
      configPath_(std::move(configPath)),
      espeakDataPath_(std::move(espeakDataPath))
{
}

LibPiperApi::~LibPiperApi()
{
    std::lock_guard<std::mutex> lock(synthesizerMutex_);
    if (synthesizer_) {
        piper_free(synthesizer_);
        synthesizer_ = nullptr;
    }
}

bool LibPiperApi::ensureSynthesizer(QString *error)
{
    if (synthesizer_)
        return true;

    if (modelPath_.isEmpty())
        return fail(error, QStringLiteral("piper model path is not configured"));

    // Keep the converted paths alive for the whole call; piper_create may use
    // them during model/config/espeak-ng initialization.
    const QByteArray modelPath = toNativePath(modelPath_);
    const QByteArray configPath = toNativePath(configPath_);
    const QByteArray espeakDataPath = toNativePath(espeakDataPath_);

    // libpiper treats NULL as "model_path + .json" for the config and "not
    // needed" for the espeak-ng data, so only pass non-empty overrides.
    synthesizer_ = piper_create(modelPath.constData(),
                                configPath.isEmpty() ? nullptr : configPath.constData(),
                                espeakDataPath.isEmpty() ? nullptr : espeakDataPath.constData());
    if (!synthesizer_) {
        return fail(error, QStringLiteral("failed to load Piper model '%1' (config '%2', "
                                          "espeak-ng data '%3')")
                               .arg(modelPath_, configPath_, espeakDataPath_));
    }

    return true;
}

bool LibPiperApi::synthesize(const QString &text, float lengthScale, std::vector<float> *pcm,
                             int *sampleRate, QString *error)
{
    if (error)
        error->clear();
    if (!pcm || !sampleRate)
        return fail(error, QStringLiteral("invalid TTS output buffer"));
    pcm->clear();
    *sampleRate = 0;

    // The worker calls resetCancel() right before each utterance; a stop that
    // lands after that call must still abort this synthesis.
    if (cancelRequested_.load())
        return fail(error, QStringLiteral("cancelled"));

    // Only one utterance is synthesized at a time on the worker thread.  The
    // lock also serializes lazy creation with the destructor.
    std::lock_guard<std::mutex> lock(synthesizerMutex_);
    if (!ensureSynthesizer(error))
        return false;

    const QByteArray textUtf8 = text.toUtf8();
    piper_synthesize_options options = piper_default_synthesize_options(synthesizer_);
    options.length_scale = lengthScale;

    int rc = piper_synthesize_start(synthesizer_, textUtf8.constData(), &options);
    if (rc != PIPER_OK) {
        return fail(error, QStringLiteral("piper_synthesize_start failed (rc=%1)").arg(rc));
    }

    int rate = 0;
    bool finished = false;
    while (!finished) {
        if (cancelRequested_.load()) {
            pcm->clear();
            return fail(error, QStringLiteral("cancelled"));
        }

        piper_audio_chunk chunk{};
        rc = piper_synthesize_next(synthesizer_, &chunk);
        if (rc != PIPER_OK && rc != PIPER_DONE) {
            pcm->clear();
            return fail(error, QStringLiteral("piper_synthesize_next failed (rc=%1)").arg(rc));
        }

        // PIPER_DONE still fills the final chunk, so consume it before
        // stopping.
        if (chunk.samples && chunk.num_samples > 0) {
            pcm->insert(pcm->end(), chunk.samples, chunk.samples + chunk.num_samples);
        }
        if (chunk.sample_rate > 0) {
            if (rate == 0) {
                rate = chunk.sample_rate;
            } else if (chunk.sample_rate != rate) {
                pcm->clear();
                return fail(error, QStringLiteral("piper sample rate changed from %1 Hz to %2 Hz")
                                       .arg(rate)
                                       .arg(chunk.sample_rate));
            }
        }

        finished = (rc == PIPER_DONE) || chunk.is_last;
    }

    if (rate != kPiperSampleRate) {
        pcm->clear();
        return fail(error, QStringLiteral("piper returned %1 Hz; expected %2 Hz")
                               .arg(rate)
                               .arg(kPiperSampleRate));
    }
    if (pcm->empty())
        return fail(error, QStringLiteral("piper produced no audio"));

    *sampleRate = rate;
    return true;
}

void LibPiperApi::cancel()
{
    cancelRequested_.store(true);
}

void LibPiperApi::resetCancel()
{
    cancelRequested_.store(false);
}
