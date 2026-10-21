#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <QHash>
#include <QObject>
#include <QString>

#include "chat-message.hpp"
#include "message-filter.hpp"
#include "settings.hpp"
#include "tts-queue.hpp"

class ChatClient;
class QTimer;
class TtsEngine;
class TtsWorker;

/**
 * Single owner of the chat and speech pipeline: ChatClient, MessageFilter,
 * TtsQueue, TtsWorker and the current Settings.  Audio always flows through
 * the process-wide AudioBus buffer read by the OBS audio source.
 *
 * Threading: every public method and slot runs on the Qt UI thread.  The
 * TtsWorker owns the only extra thread; ChatClient uses QProcess's event-driven
 * I/O.  Chat/worker signals are connected with automatic connections, so
 * worker-thread emissions are queued to this object's thread.
 *
 * Lifetime: plugin-main owns one instance from obs_module_load() until
 * obs_module_unload().  It must outlive the dock, which only holds a
 * non-owning pointer.
 */
class PluginContext : public QObject {
    Q_OBJECT

public:
    explicit PluginContext(QObject *parent = nullptr);
    ~PluginContext() override;

    Settings &settings();

    /* Validates the worker/espeak, resolves the helper and starts ChatClient.
     * Returns false with a user-displayable error on failure. */
    bool connectChat(QString *error);
    void disconnectChat();

    void testVoice();
    void skipCurrent();
    void clearQueue();

    /* Stops/deletes the worker, clears buffered audio and starts a fresh one
     * with the current engine settings.  Queued messages are preserved.  An
     * explicit restart also clears a Piper initialization fallback so Piper is
     * retried; automatic fallback restarts use the internal path instead. */
    void restartWorker();

    /* Applies settings that can change without a persistence write: queue
     * limit, output volume and worker-visible settings.  A change of engine,
     * Piper voice, espeak voice, rate, pitch or amplitude restarts the worker
     * while preserving the queue.  PluginContext is the only place that
     * chooses/logs the active engine and any espeak fallback.  Used by live UI
     * controls; saveSettings() calls it too. */
    void applyLiveSettings();

    /* Persists settings and applies them live.  Returns false when the file
     * could not be written; the failure is also emitted as a log line. */
    bool saveSettings();

    bool hasAudioSource() const;
    std::vector<ChatMessage> queueSnapshot(size_t max) const;

signals:
    void statusChanged(const QString &state, const QString &detail);
    void logLine(const QString &line);
    void queueChanged();

private slots:
    void onChatEvent(const ChatEvent &event);

private:
    bool ensureWorker(QString *error);
    void restartWorkerInternal();
    void installWorker(std::unique_ptr<TtsEngine> engine);
    bool engineSettingsDiffer() const;
    void applySettingsToWorker();
    void flushDropSummary();

    Settings settings_;
    MessageFilter filter_;
    TtsQueue queue_;
    std::unique_ptr<ChatClient> client_;
    std::unique_ptr<TtsWorker> worker_;

    /* Bounded reporting for high-volume chat: filter drops are counted per
     * reason and queue evictions are read from the queue, then one summary line
     * is emitted periodically instead of one line per dropped message. */
    QHash<QString, int> filterDrops_;
    uint64_t queueEvictionsLogged_ = 0;
    QTimer *dropLogTimer_ = nullptr;

    /* Engine parameters the current worker was created with; used to restart
     * the worker when the engine, voice, rate, pitch or amplitude changes.
     * engineSelection_ is the requested value ("piper"/"espeak"), not the
     * backend that was actually created after a fallback. */
    QString engineSelection_;
    QString enginePiperVoice_;
    QString enginePiperModelPath_;
    QString enginePiperConfigPath_;
    QString engineVoice_;
    int engineRate_ = -1;
    int enginePitch_ = -1;
    int engineAmplitude_ = -1;

    /* Set when a Piper engine reported a permanent initialization failure.
     * espeak is then authoritative until the user changes the requested engine
     * or explicitly restarts; the flag survives the automatic fallback restart
     * so a bad model cannot cause a restart loop. */
    bool forcedEspeak_ = false;
    QString forcedEspeakReason_;
    bool workerUnavailable_ = false;
};
