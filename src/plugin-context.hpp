#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <QObject>
#include <QString>

#include "chat-message.hpp"
#include "message-filter.hpp"
#include "settings.hpp"
#include "tts-queue.hpp"

class ChatClient;
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
     * with the current engine settings.  Queued messages are preserved. */
    void restartWorker();

    void saveSettings();

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
    void applySettingsToWorker();

    Settings settings_;
    MessageFilter filter_;
    TtsQueue queue_;
    std::unique_ptr<ChatClient> client_;
    std::unique_ptr<TtsWorker> worker_;

    /* Engine parameters the current worker was created with; used to restart
     * the worker when voice/rate/pitch/amplitude change. */
    QString engineVoice_;
    int engineRate_ = -1;
    int enginePitch_ = -1;
    int engineAmplitude_ = -1;
};
