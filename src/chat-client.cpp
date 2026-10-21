#include "chat-client.hpp"

#include "paths.hpp"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace {
/* A helper line that never ends must not grow the client without bound. */
constexpr qsizetype kMaxBufferBytes = 1024 * 1024;

QString objectString(const QJsonObject &object, const char *key)
{
    return object.value(QLatin1String(key)).toString();
}

bool objectBool(const QJsonObject &object, const char *key)
{
    return object.value(QLatin1String(key)).toBool(false);
}

ChatMessage parseMessage(const QJsonObject &object)
{
    ChatMessage message;
    message.id = objectString(object, "id");
    message.author = objectString(object, "author");
    message.authorId = objectString(object, "author_id");
    message.text = objectString(object, "text");
    message.publishedAt = objectString(object, "published_at");
    message.kind = objectString(object, "kind");
    message.amount = objectString(object, "amount");
    message.isModerator = objectBool(object, "is_moderator");
    message.isOwner = objectBool(object, "is_owner");
    message.isSponsor = objectBool(object, "is_sponsor");
    return message;
}
} // namespace

ChatClient::ChatClient(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<ChatEvent>("ChatEvent");
}

ChatClient::~ChatClient()
{
    stop();
}

bool ChatClient::parseLine(const QByteArray &line, ChatEvent *event, QString *error)
{
    if (!event) {
        if (error)
            *error = QStringLiteral("no destination event");
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("invalid JSON: ") + parseError.errorString();
        return false;
    }
    if (!document.isObject()) {
        if (error)
            *error = QStringLiteral("helper event is not a JSON object");
        return false;
    }

    const QJsonObject object = document.object();
    const QString type = objectString(object, "type");

    /* Reset every field so a reused destination cannot leak stale values. */
    *event = ChatEvent{};

    if (type == QLatin1String("status")) {
        event->type = ChatEvent::Type::Status;
        event->state = objectString(object, "state");
        event->detail = objectString(object, "detail");
        return true;
    }

    if (type == QLatin1String("message")) {
        const QJsonValue messageValue = object.value(QLatin1String("message"));
        if (!messageValue.isObject()) {
            if (error)
                *error = QStringLiteral("message event has no message object");
            return false;
        }
        event->type = ChatEvent::Type::Message;
        event->message = parseMessage(messageValue.toObject());
        return true;
    }

    if (type == QLatin1String("error")) {
        event->type = ChatEvent::Type::Error;
        event->code = objectString(object, "code");
        event->detail = objectString(object, "detail");
        event->fatal = objectBool(object, "fatal");
        return true;
    }

    if (error)
        *error = type.isEmpty() ? QStringLiteral("helper event has no type")
                                : QStringLiteral("unknown helper event type: ") + type;
    return false;
}

bool ChatClient::start(const ChatConfig &cfg, QString *error)
{
    if (isRunning()) {
        if (error)
            *error = QStringLiteral("chat helper is already running");
        return false;
    }

    discardProcess();
    buffer_.clear();
    userStopped_ = false;
    lastEventWasError_ = false;
    endedSeen_ = false;

    QString helperPath = cfg.helperPath;
    if (helperPath.isEmpty())
        helperPath = resolveBundledPath(QStringLiteral("bin/ytchat-helper"));

    if (helperPath.isEmpty()) {
        if (error)
            *error = QStringLiteral("ytchat-helper was not found next to the plugin");
        return false;
    }

    const QFileInfo helperInfo(helperPath);
    if (!helperInfo.exists() || !helperInfo.isFile()) {
        if (error)
            *error = QStringLiteral("ytchat-helper does not exist: ") + helperPath;
        return false;
    }
    if (!helperInfo.isExecutable()) {
        if (error)
            *error = QStringLiteral("ytchat-helper is not executable: ") + helperPath;
        return false;
    }

    QJsonObject config;
    config.insert(QStringLiteral("provider"),
                  cfg.provider.isEmpty() ? QStringLiteral("auto") : cfg.provider);
    config.insert(QStringLiteral("api_key"), cfg.apiKey);
    config.insert(QStringLiteral("video"), cfg.video);
    config.insert(QStringLiteral("hl"), cfg.hl.isEmpty() ? QStringLiteral("en") : cfg.hl);
    const QByteArray payload = QJsonDocument(config).toJson(QJsonDocument::Compact) + '\n';

    process_ = new QProcess(this);
    process_->setProgram(helperPath);
    connect(process_, &QProcess::readyReadStandardOutput, this, &ChatClient::onReadyRead);
    connect(process_, &QProcess::readyReadStandardError, this, &ChatClient::onReadyReadStandardError);
    connect(process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &ChatClient::onFinished);
    connect(process_, &QProcess::errorOccurred, this, &ChatClient::onErrorOccurred);

    process_->start();
    if (!process_->waitForStarted(5000)) {
        const QString detail = process_->errorString();
        discardProcess();
        if (error)
            *error = QStringLiteral("failed to start ytchat-helper: ") + detail;
        return false;
    }

    if (process_->write(payload) < 0) {
        const QString detail = process_->errorString();
        stop();
        if (error)
            *error = QStringLiteral("failed to send config to ytchat-helper: ") + detail;
        return false;
    }
    process_->closeWriteChannel();
    return true;
}

void ChatClient::stop()
{
    userStopped_ = true;

    if (!process_) {
        buffer_.clear();
        return;
    }

    if (process_->state() != QProcess::NotRunning) {
        process_->terminate();
        if (!process_->waitForFinished(2000)) {
            process_->kill();
            process_->waitForFinished(2000);
        }
    }

    discardProcess();
    buffer_.clear();
}

bool ChatClient::isRunning() const
{
    return process_ && process_->state() != QProcess::NotRunning;
}

void ChatClient::onReadyRead()
{
    if (!process_)
        return;

    processOutput(process_->readAllStandardOutput());
}

void ChatClient::processOutput(const QByteArray &data)
{
    buffer_.append(data);
    processBuffer();
}

void ChatClient::processBuffer()
{
    qsizetype newline = -1;
    while ((newline = buffer_.indexOf('\n')) >= 0) {
        QByteArray line = buffer_.left(newline);
        buffer_.remove(0, newline + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.trimmed().isEmpty())
            continue;

        ChatEvent event;
        QString error;
        if (parseLine(line, &event, &error)) {
            trackHelperEvent(event);
            emit eventReceived(event);
        } else {
            emitStatus(QStringLiteral("log"),
                       QStringLiteral("ignored malformed helper output: ") + error);
        }
    }

    if (buffer_.size() > kMaxBufferBytes) {
        buffer_.clear();
        ChatEvent event;
        event.type = ChatEvent::Type::Error;
        event.code = QStringLiteral("buffer_overflow");
        event.detail = QStringLiteral("helper output exceeded 1 MiB without a line break");
        event.fatal = false;
        emit eventReceived(event);
    }
}

void ChatClient::trackHelperEvent(const ChatEvent &event)
{
    switch (event.type) {
    case ChatEvent::Type::Error:
        lastEventWasError_ = true;
        endedSeen_ = false;
        break;
    case ChatEvent::Type::Status:
        lastEventWasError_ = false;
        endedSeen_ = event.state == QLatin1String("ended");
        break;
    case ChatEvent::Type::Message:
        lastEventWasError_ = false;
        endedSeen_ = false;
        break;
    }
}

void ChatClient::onReadyReadStandardError()
{
    if (!process_)
        return;

    const QList<QByteArray> lines = process_->readAllStandardError().split('\n');
    for (QByteArray line : lines) {
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty())
            continue;
        emitStatus(QStringLiteral("log"), QString::fromUtf8(line));
    }
}

void ChatClient::onFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    QProcess *finished = process_;

    /* The helper's final JSON line can arrive in the same event-loop turn as
     * finished().  Drain stdout before deciding so a terminal error is never
     * replaced by helper_exited/ended. */
    if (finished)
        processOutput(finished->readAllStandardOutput());

    process_ = nullptr;
    buffer_.clear();

    const bool wasUserStopped = userStopped_;
    const bool errorWasFinal = lastEventWasError_;
    const bool alreadyEnded = endedSeen_;
    userStopped_ = false;
    lastEventWasError_ = false;
    endedSeen_ = false;

    if (!wasUserStopped && !errorWasFinal) {
        if (exitStatus == QProcess::CrashExit) {
            emitError(QStringLiteral("helper_crashed"),
                      QStringLiteral("helper crashed (exit code %1)").arg(exitCode), true);
        } else if (exitCode != 0) {
            emitError(QStringLiteral("helper_exited"),
                      QStringLiteral("helper exited with code %1").arg(exitCode), true);
        } else if (!alreadyEnded) {
            emitStatus(QStringLiteral("ended"), QStringLiteral("helper exited"));
        }
    }

    /* deleteLater(), not delete: this slot may run inside a signal emitted by
     * the process itself. */
    if (finished)
        finished->deleteLater();
}

void ChatClient::onErrorOccurred(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart)
        return;

    emitError(QStringLiteral("helper_start_failed"),
              process_ ? process_->errorString() : QStringLiteral("helper failed to start"),
              true);
}

void ChatClient::emitStatus(const QString &state, const QString &detail)
{
    ChatEvent event;
    event.type = ChatEvent::Type::Status;
    event.state = state;
    event.detail = detail;
    emit eventReceived(event);
}

void ChatClient::emitError(const QString &code, const QString &detail, bool fatal)
{
    ChatEvent event;
    event.type = ChatEvent::Type::Error;
    event.code = code;
    event.detail = detail;
    event.fatal = fatal;
    emit eventReceived(event);
}

void ChatClient::discardProcess()
{
    if (!process_)
        return;

    process_->disconnect(this);
    process_->deleteLater();
    process_ = nullptr;
}
