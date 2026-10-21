#include "plugin-context.hpp"

#include <QDateTime>
#include <QFileInfo>
#include <QTimer>

#include <utility>

#include "audio-bus.hpp"
#include "audio-source.hpp"
#include "chat-client.hpp"
#include "espeak-engine.hpp"
#include "paths.hpp"
#include "tts-worker.hpp"

namespace {
QString formatMessage(const ChatMessage &msg)
{
    QString line = msg.author;
    if (msg.isModerator)
        line += QStringLiteral(" [mod]");
    if (msg.isOwner)
        line += QStringLiteral(" [owner]");
    if (msg.isSponsor)
        line += QStringLiteral(" [member]");
    line += QStringLiteral(": ") + msg.text;
    if (!msg.amount.isEmpty())
        line += QStringLiteral(" (") + msg.amount + QLatin1Char(')');
    return line;
}
} // namespace

PluginContext::PluginContext(QObject *parent)
    : QObject(parent), settings_(Settings::load()),
      client_(std::make_unique<ChatClient>())
{
    connect(client_.get(), &ChatClient::eventReceived, this, &PluginContext::onChatEvent);

    AudioBus::instance().buffer().setVolume(static_cast<float>(settings_.volume));
}

PluginContext::~PluginContext()
{
    disconnectChat();
    worker_.reset();
    client_.reset();
}

Settings &PluginContext::settings()
{
    return settings_;
}

bool PluginContext::ensureWorker(QString *error)
{
    if (error)
        error->clear();

    if (worker_) {
        applySettingsToWorker();
        worker_->start();
        AudioBus::instance().buffer().setVolume(static_cast<float>(settings_.volume));
        return true;
    }

    QString binaryPath = settings_.espeakPath;
    if (binaryPath.isEmpty())
        binaryPath = resolveBundledPath(QStringLiteral("bin/espeak-ng"));

    if (binaryPath.isEmpty()) {
        const QString tried = bundledPathCandidates(QStringLiteral("bin/espeak-ng")).join(
            QStringLiteral(", "));
        const QString message =
            QStringLiteral("espeak-ng was not found; looked for: ") + tried;
        if (error)
            *error = message;
        return false;
    }

    const QFileInfo binaryInfo(binaryPath);
    if (!binaryInfo.exists() || !binaryInfo.isFile()) {
        const QString message = QStringLiteral("espeak-ng does not exist: ") + binaryPath;
        if (error)
            *error = message;
        return false;
    }
    if (!binaryInfo.isExecutable()) {
        const QString message = QStringLiteral("espeak-ng is not executable: ") + binaryPath;
        if (error)
            *error = message;
        return false;
    }

    /* The data directory may legitimately be absent in a host install where
     * espeak-ng uses its system data; the Flatpak extension always bundles it. */
    const QString dataPath = resolveBundledPath(QStringLiteral("espeak-ng-data"));

    auto engine = std::make_unique<EspeakEngine>(binaryPath, dataPath, settings_.voice,
                                                 settings_.rate, settings_.pitch,
                                                 settings_.amplitude);

    worker_ = std::make_unique<TtsWorker>(queue_, AudioBus::instance().buffer(),
                                           std::move(engine));

    /* Worker status/errors are diagnostics; the dock's connection label stays
     * owned by chat status so speech does not overwrite it. */
    connect(worker_.get(), &TtsWorker::statusChanged, this, [this](const QString &text) {
        if (text != QLatin1String("idle"))
            emit logLine(QStringLiteral("tts: ") + text);
    });
    connect(worker_.get(), &TtsWorker::errorOccurred, this, [this](const QString &text) {
        emit logLine(QStringLiteral("tts error: ") + text);
    });

    applySettingsToWorker();
    worker_->start();

    engineVoice_ = settings_.voice;
    engineRate_ = settings_.rate;
    enginePitch_ = settings_.pitch;
    engineAmplitude_ = settings_.amplitude;

    AudioBus::instance().buffer().setVolume(static_cast<float>(settings_.volume));
    emit logLine(QStringLiteral("espeak: ") + binaryPath);
    emit logLine(dataPath.isEmpty() ? QStringLiteral("espeak data: bundled data not found")
                                    : QStringLiteral("espeak data: ") + dataPath);
    return true;
}

void PluginContext::applySettingsToWorker()
{
    if (worker_)
        worker_->setSettings(settings_);
}

bool PluginContext::connectChat(QString *error)
{
    if (error)
        error->clear();

    if (!ensureWorker(error))
        return false;

    queue_.setLimit(settings_.queueLimit);

    /* A fresh connection starts a fresh stream: earlier duplicate IDs and
     * cooldowns must not suppress messages from the new stream. */
    filter_.reset();

    const ChatConfig config = settings_.toChatConfig();
    emit logLine(config.helperPath.isEmpty()
                     ? QStringLiteral("helper: not found; looked for: ") +
                           bundledPathCandidates(QStringLiteral("bin/ytchat-helper"))
                               .join(QStringLiteral(", "))
                     : QStringLiteral("helper: ") + config.helperPath);

    if (!client_->start(config, error)) {
        /* No orphaned speech thread should outlive a failed connection. */
        if (worker_)
            worker_->stop();
        return false;
    }

    emit statusChanged(QStringLiteral("connecting"), config.provider);
    return true;
}

void PluginContext::disconnectChat()
{
    client_->stop();
    if (worker_)
        worker_->stop();

    /* Do not replay stale speech or messages after a later reconnect. */
    queue_.clear();
    AudioBus::instance().buffer().clear();
    emit queueChanged();
    emit statusChanged(QStringLiteral("disconnected"), QString());
}

void PluginContext::testVoice()
{
    QString error;
    if (!ensureWorker(&error)) {
        /* Engine failures are diagnostics, not chat connection state. */
        emit logLine(QStringLiteral("error: ") + error);
        return;
    }

    ChatMessage msg;
    msg.id = QStringLiteral("test-voice-") +
             QString::number(QDateTime::currentMSecsSinceEpoch());
    msg.author = QStringLiteral("Test");
    msg.text = QStringLiteral("This is a test of the YouTube chat text to speech voice.");
    msg.kind = QStringLiteral("text");

    queue_.push(msg, 10);
    emit queueChanged();
    emit logLine(QStringLiteral("queued test voice"));
}

void PluginContext::skipCurrent()
{
    if (worker_)
        worker_->skip();
    else {
        queue_.clear();
        AudioBus::instance().buffer().clear();
    }
    emit queueChanged();
}

void PluginContext::clearQueue()
{
    queue_.clear();
    emit queueChanged();
}

void PluginContext::restartWorker()
{
    if (worker_) {
        worker_->stop();
        worker_.reset();
    }

    AudioBus::instance().buffer().clear();
    emit queueChanged();

    QString error;
    if (!ensureWorker(&error)) {
        /* Engine failures are diagnostics, not chat connection state. */
        emit logLine(QStringLiteral("error: ") + error);
        return;
    }

    emit logLine(QStringLiteral("tts worker restarted"));
}

void PluginContext::saveSettings()
{
    const bool engineChanged = worker_ && (settings_.voice != engineVoice_ ||
                                           settings_.rate != engineRate_ ||
                                           settings_.pitch != enginePitch_ ||
                                           settings_.amplitude != engineAmplitude_);

    if (!settings_.save())
        emit logLine(QStringLiteral("settings: save failed"));

    queue_.setLimit(settings_.queueLimit);
    AudioBus::instance().buffer().setVolume(static_cast<float>(settings_.volume));

    /* Live-appliable settings (author template, max chars) update without a
     * restart; engine parameters need a fresh EspeakEngine. */
    applySettingsToWorker();
    if (engineChanged)
        restartWorker();
}

bool PluginContext::hasAudioSource() const
{
    return audio_source_exists();
}

std::vector<ChatMessage> PluginContext::queueSnapshot(size_t max) const
{
    return queue_.snapshot(max);
}

void PluginContext::onChatEvent(const ChatEvent &event)
{
    switch (event.type) {
    case ChatEvent::Type::Status:
        if (event.state == QLatin1String("log")) {
            /* Helper stderr and parser diagnostics only go to the log pane;
             * they must not replace the connection status label. */
            emit logLine(event.detail);
            return;
        }
        emit statusChanged(event.state, event.detail);
        emit logLine(QStringLiteral("status[") + event.state + QStringLiteral("]: ") +
                     event.detail);
        return;

    case ChatEvent::Type::Error:
        emit logLine(QStringLiteral("error [") + event.code + QStringLiteral("]: ") +
                     event.detail);
        if (event.fatal) {
            emit statusChanged(QStringLiteral("error"),
                               event.code + QStringLiteral(": ") + event.detail);
            /* Do not reconnect automatically, and stop outside this signal
             * emission (the event may originate in ChatClient's process I/O). */
            QTimer::singleShot(0, this, [this]() { client_->stop(); });
        }
        return;

    case ChatEvent::Type::Message: {
        emit logLine(formatMessage(event.message));

        const FilterDecision decision =
            filter_.evaluate(event.message, settings_, QDateTime::currentMSecsSinceEpoch());
        if (!decision.accept)
            return;

        queue_.push(event.message, decision.priority);
        emit queueChanged();
        return;
    }
    }
}
