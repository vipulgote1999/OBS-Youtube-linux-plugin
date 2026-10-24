#include "tts-worker.hpp"

#include <chrono>
#include <exception>
#include <utility>
#include <vector>

#include "audio-buffer.hpp"
#include "chat-message.hpp"
#include "speech-conditioner.hpp"
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

        /* Reset the engine's cancel mark only after the running check.  The
         * second check closes the race with stop(): a stop before reset is
         * caught here, and a stop after reset is observed by synthesize(). */
        engine_->resetCancel();
        if (!running_.load())
            break;

        const Settings settings = settingsSnapshot();
        const QString text = TextNormalizer::render(msg, settings);
        if (text.trimmed().isEmpty())
            continue;

        speaking = true;
        emit statusChanged(QStringLiteral("speaking: ") + msg.author);

        int sampleRate = 0;
        QString error;
        bool synthesized = false;

        if (settings.streamAudio) {
            /* Push each Piper chunk as it arrives.  The conditioner keeps its
             * DC-blocker state across chunks and fades only the first and last
             * one, so chunk boundaries stay click-free. */
            SpeechConditioner::Stream conditioner({kExpectedSampleRate});
            bool first = true;
            bool sinkStopped = false;
            bool rateMismatch = false;

            const auto sink = [&](const float *samples, size_t count, bool isLast) -> bool {
                if (!running_.load()) {
                    sinkStopped = true;
                    return false;
                }
                if (sampleRate != 0 && sampleRate != kExpectedSampleRate) {
                    rateMismatch = true;
                    return false;
                }
                std::vector<float> chunk(samples, samples + count);
                conditioner.process(&chunk, first, isLast);
                first = false;
                if (!chunk.empty())
                    audio_.push(chunk.data(), chunk.size());
                return true;
            };

            synthesized = engine_->synthesizeStreaming(text, sink, &sampleRate, &error);
            if (sinkStopped)
                break;
            if (rateMismatch) {
                emit errorOccurred(
                    QStringLiteral("speech engine returned %1 Hz; expected %2 Hz")
                        .arg(sampleRate)
                        .arg(kExpectedSampleRate));
                continue;
            }
        } else {
            std::vector<float> pcm;
            synthesized = engine_->synthesize(text, &pcm, &sampleRate, &error);
            if (!running_.load())
                break;
            if (synthesized && sampleRate == kExpectedSampleRate && !pcm.empty()) {
                SpeechConditioner::Config conditioner;
                conditioner.sampleRate = sampleRate;
                SpeechConditioner::process(&pcm, conditioner);
                audio_.push(pcm.data(), pcm.size());
            }
        }

        /* A stop() during synthesis turns the engine's failure into a normal
         * shutdown; swallowing it keeps the UI free of spurious errors. */
        if (!running_.load())
            break;

        if (!synthesized) {
            /* A permanent initialization failure (for example a corrupt Piper
             * model) will never produce audio on this engine.  Hand the current
             * message to the replacement engine and stop this run loop instead
             * of retrying the dead engine on every utterance. */
            if (engine_->initializationFailed()) {
                queue_.push(msg, 0);
                const QString reason =
                    error.isEmpty() ? QStringLiteral("speech engine could not initialize")
                                    : error;
                if (speaking) {
                    speaking = false;
                    emit statusChanged(QStringLiteral("idle"));
                }
                running_.store(false);
                emit engineUnavailable(reason);
                break;
            }

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

        if (queue_.size() == 0) {
            speaking = false;
            emit statusChanged(QStringLiteral("idle"));
        }
    }

    if (speaking)
        emit statusChanged(QStringLiteral("idle"));
}
