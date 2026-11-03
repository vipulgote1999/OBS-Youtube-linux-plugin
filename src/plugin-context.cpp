#include "plugin-context.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <utility>

#include "audio-bus.hpp"
#include "audio-source.hpp"
#include "chat-client.hpp"
#include "local-tts-engine.hpp"
#include "paths.hpp"
#include "tts-engine.hpp"
#include "tts-worker.hpp"

#ifdef HAVE_SUPERTONIC
#include "libsupertonic-api.hpp"
#include "supertonic-engine.hpp"
#endif

namespace {
constexpr char kDefaultSupertonicVoice[] = "F1";

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

QString supertonicRuntimeProblem()
{
#ifdef HAVE_SUPERTONIC
    return {};
#else
    /* The plugin no longer has an espeak fallback.  A host build without ONNX
     * Runtime must fail at engine creation with a readable error rather than
     * going silent. */
    return QStringLiteral(
        "this build has no ONNX Runtime (HAVE_SUPERTONIC is not defined)");
#endif
}

QString supertonicVoiceOrDefault(const Settings &settings)
{
    static const QStringList voices = {
        QStringLiteral("F1"), QStringLiteral("F2"), QStringLiteral("F3"),
        QStringLiteral("F4"), QStringLiteral("F5"), QStringLiteral("M1"),
        QStringLiteral("M2"), QStringLiteral("M3"), QStringLiteral("M4"),
        QStringLiteral("M5"),
    };
    const QString voice = settings.supertonicVoice.trimmed().toUpper();
    return voices.contains(voice) ? voice : QString::fromLatin1(kDefaultSupertonicVoice);
}

struct SupertonicResources {
    QString modelDir;
    QString voicePath;
    /* Empty when the requested Supertonic engine can be created; otherwise each
     * entry explains one missing resource/runtime in user-facing terms. */
    QStringList problems;
};

/* Resolves the bundled model and the selected voice style.  The model layout is
 * documented in docs/superpowers/specs/2026-11-03-supertonic-engine-design.md:
 * <modelDir>/onnx/*.onnx plus <modelDir>/voice_styles/<voice>.json. */
SupertonicResources resolveSupertonicResources(const Settings &settings)
{
    SupertonicResources resources;

    const QString configuredModel = settings.supertonicModelPath.trimmed();
    if (!configuredModel.isEmpty()) {
        if (QFileInfo(configuredModel).isDir()) {
            resources.modelDir = configuredModel;
        } else {
            resources.problems.append(QStringLiteral("supertonic model does not exist: ") +
                                      configuredModel);
        }
    } else {
        resources.modelDir = resolveBundledPath(QStringLiteral("supertonic"));
        if (resources.modelDir.isEmpty()) {
            resources.problems.append(
                QStringLiteral("supertonic model not found; looked for: ") +
                bundledPathCandidates(QStringLiteral("supertonic"))
                    .join(QStringLiteral(", ")));
        }
    }

    if (!resources.modelDir.isEmpty()) {
        /* Accept either the package directory (with onnx/) or the onnx
         * directory itself, matching LibSupertonicApi's resolution. */
        const QString nestedOnnx = QDir(resources.modelDir).filePath(QStringLiteral("onnx"));
        const QString onnxDir =
            QFileInfo(nestedOnnx).isDir() ? nestedOnnx : resources.modelDir;
        const QStringList required = {
            QStringLiteral("duration_predictor.onnx"),
            QStringLiteral("text_encoder.onnx"),
            QStringLiteral("vector_estimator.onnx"),
            QStringLiteral("vocoder.onnx"),
            QStringLiteral("tts.json"),
            QStringLiteral("unicode_indexer.json"),
        };
        for (const QString &file : required) {
            const QString path = QDir(onnxDir).filePath(file);
            if (!QFileInfo(path).isFile())
                resources.problems.append(QStringLiteral("supertonic model file missing: ") +
                                          path);
        }

        const QString voice = supertonicVoiceOrDefault(settings);
        const QString packageVoice =
            QDir(resources.modelDir)
                .filePath(QStringLiteral("voice_styles/") + voice + QStringLiteral(".json"));
        const QString parentVoice =
            QDir(resources.modelDir)
                .filePath(QStringLiteral("../voice_styles/") + voice + QStringLiteral(".json"));
        if (QFileInfo(packageVoice).isFile())
            resources.voicePath = packageVoice;
        else if (QFileInfo(parentVoice).isFile())
            resources.voicePath = QFileInfo(parentVoice).canonicalFilePath();
        else
            resources.problems.append(QStringLiteral("supertonic voice not found: ") +
                                      packageVoice);
    }

    const QString runtimeProblem = supertonicRuntimeProblem();
    if (!runtimeProblem.isEmpty())
        resources.problems.append(runtimeProblem);

    return resources;
}

} // namespace

PluginContext::PluginContext(QObject *parent)
    : QObject(parent), settings_(Settings::load()),
      client_(std::make_unique<ChatClient>())
{
    connect(client_.get(), &ChatClient::eventReceived, this, &PluginContext::onChatEvent);

    /* One bounded summary line per interval keeps high-volume chat readable;
     * the counters below explain what was dropped without per-message spam. */
    dropLogTimer_ = new QTimer(this);
    dropLogTimer_->setInterval(10000);
    connect(dropLogTimer_, &QTimer::timeout, this, &PluginContext::flushDropSummary);
    dropLogTimer_->start();

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

    /* Reuse the live worker unless its engine failed at runtime or an engine
     * parameter changed (for example an engine switch whose coalescing timer
     * is still pending). */
    if (worker_ && !workerUnavailable_ && !engineSettingsDiffer()) {
        applySettingsToWorker();
        worker_->start();
        AudioBus::instance().buffer().setVolume(static_cast<float>(settings_.volume));
        return true;
    }

    if (worker_) {
        const bool engineChange = !workerUnavailable_;
        worker_->stop();
        worker_.reset();
        AudioBus::instance().buffer().clear();
        emit queueChanged();
        if (engineChange)
            emit logLine(QStringLiteral("tts worker restarted for engine change"));
    }

    const bool localRequested = settings_.ttsEngine.trimmed().compare(
                                    QLatin1String("local"), Qt::CaseInsensitive) == 0;
    SupertonicResources supertonic;

    if (localRequested) {
        auto local = std::make_unique<LocalTtsEngine>(
            settings_.localTtsUrl, settings_.localTtsVoice, settings_.localTtsModel);
        installWorker(std::move(local));
        emit logLine(QStringLiteral("tts engine: local server (") + settings_.localTtsUrl +
                     QLatin1Char(')'));
        emit logLine(QStringLiteral("local tts voice: ") + settings_.localTtsVoice);
    } else {
        supertonic = resolveSupertonicResources(settings_);
        if (!supertonic.problems.isEmpty()) {
            if (error)
                *error = QStringLiteral("Supertonic is unavailable: ") +
                         supertonic.problems.join(QStringLiteral("; "));
            return false;
        }

#ifdef HAVE_SUPERTONIC
        const QString voice = supertonicVoiceOrDefault(settings_);
        /* Leave OBS room to render: half the cores, never more than four. */
        const int threads = qBound(1, QThread::idealThreadCount() / 2, 4);
        auto api = std::make_unique<LibSupertonicApi>(supertonic.modelDir,
                                                      supertonic.voicePath,
                                                      QStringLiteral("en"), threads);
        auto engine = std::make_unique<SupertonicEngine>(std::move(api));
        engine->setSpeed(static_cast<float>(settings_.supertonicSpeed));
        engine->setSteps(settings_.supertonicSteps);
        installWorker(std::move(engine));

        emit logLine(QStringLiteral("tts engine: supertonic (") + voice + QLatin1Char(')'));
        emit logLine(QStringLiteral("supertonic model: ") + supertonic.modelDir);
        emit logLine(QStringLiteral("supertonic voice: ") + supertonic.voicePath);
        emit logLine(QStringLiteral("supertonic steps: ") +
                     QString::number(settings_.supertonicSteps));
#else
        if (error)
            *error = QStringLiteral(
                "this build has no ONNX Runtime (HAVE_SUPERTONIC is not defined)");
        return false;
#endif
    }

    applySettingsToWorker();
    worker_->start();

    engineSelection_ = settings_.ttsEngine;
    engineLocalUrl_ = settings_.localTtsUrl;
    engineLocalVoice_ = settings_.localTtsVoice;
    engineLocalModel_ = settings_.localTtsModel;
    engineSupertonicVoice_ = settings_.supertonicVoice;
    engineSupertonicModelPath_ = settings_.supertonicModelPath;
    engineSupertonicSpeed_ = settings_.supertonicSpeed;
    engineSupertonicSteps_ = settings_.supertonicSteps;

    AudioBus::instance().buffer().setVolume(static_cast<float>(settings_.volume));
    return true;
}

void PluginContext::restartWorkerInternal()
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

void PluginContext::installWorker(std::unique_ptr<TtsEngine> engine)
{
    worker_ = std::make_unique<TtsWorker>(queue_, AudioBus::instance().buffer(),
                                           std::move(engine));
    workerUnavailable_ = false;

    /* Worker status/errors are diagnostics; the dock's connection label stays
     * owned by chat status so speech does not overwrite it. */
    connect(worker_.get(), &TtsWorker::statusChanged, this, [this](const QString &text) {
        if (text != QLatin1String("idle"))
            emit logLine(QStringLiteral("tts: ") + text);
    });
    connect(worker_.get(), &TtsWorker::errorOccurred, this, [this](const QString &text) {
        emit logLine(QStringLiteral("tts error: ") + text);
    });

    /* The engine reported a permanent initialization failure, requeued the
     * current message and stopped its run loop.  There is no espeak fallback:
     * log the reason, stop the dead worker, and retry only when the user asks
     * for a Test Voice/connect or changes the engine. */
    connect(worker_.get(), &TtsWorker::engineUnavailable, this,
            [this](const QString &reason) {
                workerUnavailable_ = true;
                emit logLine(QStringLiteral("tts engine unavailable: ") + reason);
                emit logLine(QStringLiteral(
                    "fix the Supertonic model, then click Test Voice or reconnect"));
                QTimer::singleShot(0, this, [this]() {
                    if (worker_) {
                        worker_->stop();
                        worker_.reset();
                    }
                    AudioBus::instance().buffer().clear();
                    emit queueChanged();
                });
            });
}

bool PluginContext::engineSettingsDiffer() const
{
    return worker_ &&
           (settings_.ttsEngine != engineSelection_ ||
            settings_.localTtsUrl != engineLocalUrl_ ||
            settings_.localTtsVoice != engineLocalVoice_ ||
            settings_.localTtsModel != engineLocalModel_ ||
            settings_.supertonicVoice != engineSupertonicVoice_ ||
            settings_.supertonicModelPath != engineSupertonicModelPath_ ||
            settings_.supertonicSpeed != engineSupertonicSpeed_ ||
            settings_.supertonicSteps != engineSupertonicSteps_);
}

void PluginContext::applySettingsToWorker()
{
    if (worker_)
        worker_->setSettings(settings_);
}

void PluginContext::flushDropSummary()
{
    int dropped = 0;
    for (auto it = filterDrops_.cbegin(); it != filterDrops_.cend(); ++it)
        dropped += it.value();

    const uint64_t evictedTotal = queue_.evictionCount();
    const uint64_t evicted =
        evictedTotal > queueEvictionsLogged_ ? evictedTotal - queueEvictionsLogged_ : 0;
    if (dropped == 0 && evicted == 0)
        return;

    QString details;
    if (dropped > 0) {
        QStringList reasons = filterDrops_.keys();
        std::sort(reasons.begin(), reasons.end());
        QStringList counts;
        for (const QString &reason : reasons) {
            counts.append(
                QStringLiteral("%1 %2").arg(reason).arg(filterDrops_.value(reason)));
        }
        details = QStringLiteral(" (") + counts.join(QStringLiteral(", ")) + QLatin1Char(')');
    }

    emit logLine(
        QStringLiteral("tts: filter dropped %1 message(s)%2; queue evicted %3 message(s)")
            .arg(dropped)
            .arg(details)
            .arg(static_cast<qulonglong>(evicted)));

    filterDrops_.clear();
    queueEvictionsLogged_ = evictedTotal;
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
    /* Explicit retry after a runtime engine failure.  Automatic recovery is
     * gone with the espeak fallback: the next connect/Test Voice recreates the
     * worker and retries the configured engine. */
    restartWorkerInternal();
}

void PluginContext::applyLiveSettings()
{
    queue_.setLimit(settings_.queueLimit);
    AudioBus::instance().buffer().setVolume(static_cast<float>(settings_.volume));

    const bool engineChanged = engineSettingsDiffer();

    /* Live-appliable settings (author template, max chars) update without a
     * restart; engine parameters need a fresh engine/worker.  The context owns
     * this decision so the dock cannot double-restart or log a false engine. */
    applySettingsToWorker();
    if (engineChanged)
        restartWorkerInternal();
}

bool PluginContext::saveSettings()
{
    const bool saved = settings_.save();
    if (!saved)
        emit logLine(QStringLiteral("settings: save failed"));

    applyLiveSettings();
    return saved;
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
        if (!decision.accept) {
            const QString reason =
                decision.reason.isEmpty() ? QStringLiteral("filtered") : decision.reason;
            ++filterDrops_[reason];
            return;
        }

        queue_.push(event.message, decision.priority);
        emit queueChanged();
        return;
    }
    }
}
