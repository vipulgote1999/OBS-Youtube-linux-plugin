#pragma once

#include <QProcess>
#include <QString>

#include "chat-message.hpp"

/**
 * Owns the ytchat-helper child process and decodes its JSON Lines stdout.
 *
 * The API key travels only in the helper's stdin config; it is never placed
 * on the command line or written to a log/event.
 */
class ChatClient : public QObject {
    Q_OBJECT

public:
    explicit ChatClient(QObject *parent = nullptr);
    ~ChatClient() override;

    /* Resolves/starts the helper and writes the stdin config.  Returns false
     * and sets error when the helper is missing, not executable, or fails to
     * start.  Never includes the API key in error. */
    bool start(const ChatConfig &cfg, QString *error);

    /* Terminates the helper (2 s grace period, then kill).  A user stop does
     * not emit a helper-exited error event. */
    void stop();

    bool isRunning() const;

    /* Decodes one JSONL line.  Returns false for malformed JSON, non-object
     * documents, unknown event types, or a message event without its nested
     * message object. */
    static bool parseLine(const QByteArray &line, ChatEvent *event, QString *error);

signals:
    void eventReceived(const ChatEvent &event);

private:
    void onReadyRead();
    void onReadyReadStandardError();
    void onFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onErrorOccurred(QProcess::ProcessError error);
    void emitStatus(const QString &state, const QString &detail);
    void emitError(const QString &code, const QString &detail, bool fatal);
    void discardProcess();
    void processOutput(const QByteArray &data);
    void processBuffer();
    void trackHelperEvent(const ChatEvent &event);

    QProcess *process_ = nullptr;
    QByteArray buffer_;
    bool userStopped_ = false;

    /* Whether the most recent helper event was a terminal error, and whether
     * the helper already announced a normal end.  onFinished() uses these to
     * avoid overwriting the real provider error with helper_exited/ended. */
    bool lastEventWasError_ = false;
    bool endedSeen_ = false;
};
