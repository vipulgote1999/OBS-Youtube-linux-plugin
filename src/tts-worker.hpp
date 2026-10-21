#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include <QObject>
#include <QString>

#include "settings.hpp"

class AudioBuffer;
class TtsEngine;
class TtsQueue;
struct ChatMessage;

/**
 * Pulls rendered chat messages from the shared TtsQueue on a dedicated
 * std::thread, synthesizes them with the injected TtsEngine, and appends the
 * resulting 22,050 Hz mono float PCM to the shared AudioBuffer.
 *
 * Threading contract:
 *  - The object lives in the thread that constructs it (normally the Qt UI
 *    thread); start()/stop()/skip() are called from that thread.
 *  - Signals are emitted from the worker thread.  Qt resolves the automatic
 *    connection to the receiver's thread, so UI slots are delivered through
 *    the event loop as queued calls.
 *  - stop() sets running_ = false, cancels the engine and joins.  The 250 ms
 *    queue timeout bounds shutdown when the queue is empty; cancel() bounds it
 *    while espeak is running.  Never call stop() from the worker thread.
 *  - skip() drops queued messages and buffered audio, but the utterance
 *    already being synthesized may still finish and append its audio.
 */
class TtsWorker : public QObject {
    Q_OBJECT

public:
    TtsWorker(TtsQueue &queue, AudioBuffer &audio, std::unique_ptr<TtsEngine> engine,
              QObject *parent = nullptr);
    ~TtsWorker() override;

    void start();
    void stop();
    void skip();

    /* Additive to the brief: the worker renders with the current user settings
     * (read-author/template/max chars) rather than defaults.  PluginContext
     * calls this before start() and again from saveSettings(). */
    void setSettings(const Settings &settings);

signals:
    void statusChanged(const QString &text);
    void errorOccurred(const QString &text);

private:
    void run();
    Settings settingsSnapshot() const;

    TtsQueue &queue_;
    AudioBuffer &audio_;
    std::unique_ptr<TtsEngine> engine_;

    std::thread thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex settingsMutex_;
    Settings settings_;
};
