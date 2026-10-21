#include "tts-worker.hpp"

#include <chrono>
#include <exception>
#include <utility>
#include <vector>

#include "audio-buffer.hpp"
#include "chat-message.hpp"
#include "text-normalizer.hpp"
#include "tts-engine.hpp"
#include "tts-queue.hpp"

namespace {
constexpr auto kPopTimeout = std::chrono::milliseconds(250);
constexpr int kExpectedSampleRate = 22050;
} // namespace

TtsWorker::TtsWorker(TtsQueue &queue, AudioBuffer &audio, std::unique_ptr<TtsEngine> engine,
                     QObject *parent)
    : QObject(parent), queue_(queue), audio_(audio), engine_(std::move(engine))
{
}

TtsWorker::~TtsWorker()
{
    stop();
}

void TtsWorker::setSettings(const Settings &settings)
{
    std::lock_guard<std::mutex> lock(settingsMutex_);
    settings_ = settings;
}

Settings TtsWorker::settingsSnapshot() const
{
    std::lock_guard<std::mutex> lock(settingsMutex_);
    return settings_;
}

void TtsWorker::start()
{
    if (running_.load())
        return;

    /* A previous stop() already joined, but joining here also tolerates a
     * caller that stopped and immediately restarted. */
    if (thread_.joinable())
        thread_.join();

    if (!engine_) {
        emit errorOccurred(QStringLiteral("TTS engine is not available"));
        return;
    }

    running_.store(true);
    try {
        thread_ = std::thread(&TtsWorker::run, this);
    } catch (const std::exception &error) {
        running_.store(false);
        emit errorOccurred(QStringLiteral("could not start the TTS worker: ") +
                           QString::fromUtf8(error.what()));
    } catch (...) {
        running_.store(false);
        emit errorOccurred(QStringLiteral("could not start the TTS worker"));
    }
}

void TtsWorker::stop()
{
    running_.store(false);

    /* cancel() is asynchronous by contract (an atomic flag in EspeakEngine),
     * so it is safe to call from the UI thread while synthesize() runs. */
    if (engine_)
        engine_->cancel();

    if (thread_.joinable())
        thread_.join();
}

void TtsWorker::skip()
{
    /* The current utterance may finish and append its audio; the UI documents
     * this in the Skip Current tooltip. */
    queue_.clear();
    audio_.clear();
}

void TtsWorker::run()
{
    bool speaking = false;

    while (running_.load()) {
        ChatMessage msg;
        if (!queue_.pop(&msg, kPopTimeout)) {
            if (!running_.load())
                break;
            if (speaking && queue_.size() == 0) {
                speaking = false;
                emit statusChanged(QStringLiteral("idle"));
            }
            continue;
        }

        /* stop() may have raced with the pop; do not start new work. */
        if (!running_.load())
            break;

        const Settings settings = settingsSnapshot();
        const QString text = TextNormalizer::render(msg, settings);
        if (text.trimmed().isEmpty())
            continue;

        speaking = true;
        emit statusChanged(QStringLiteral("speaking: ") + msg.author);

        std::vector<float> pcm;
        int sampleRate = 0;
        QString error;
        const bool synthesized = engine_->synthesize(text, &pcm, &sampleRate, &error);

        /* A stop() during synthesis turns the engine's failure into a normal
         * shutdown; swallowing it keeps the UI free of spurious errors. */
        if (!running_.load())
            break;

        if (!synthesized) {
            emit errorOccurred(error.isEmpty() ? QStringLiteral("speech synthesis failed")
                                               : error);
            if (queue_.size() == 0) {
                speaking = false;
                emit statusChanged(QStringLiteral("idle"));
            }
            continue;
        }

        if (sampleRate != kExpectedSampleRate) {
            emit errorOccurred(
                QStringLiteral("speech engine returned %1 Hz; expected %2 Hz")
                    .arg(sampleRate)
                    .arg(kExpectedSampleRate));
            continue;
        }

        /* Re-check after the (possibly slow) synthesis so a stop or skip that
         * happened meanwhile never appends stale audio. */
        if (!running_.load())
            break;

        if (!pcm.empty())
            audio_.push(pcm.data(), pcm.size());

        if (queue_.size() == 0) {
            speaking = false;
            emit statusChanged(QStringLiteral("idle"));
        }
    }

    if (speaking)
        emit statusChanged(QStringLiteral("idle"));
}
