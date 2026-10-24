#include "plugin-context.hpp"

#include <QDateTime>
#include <QFileInfo>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <utility>

#include "audio-bus.hpp"
#include "audio-source.hpp"
#include "chat-client.hpp"
#include "espeak-engine.hpp"
#include "local-tts-engine.hpp"
#include "paths.hpp"
#include "tts-engine.hpp"
#include "tts-worker.hpp"

#ifdef HAVE_LIBPIPER
#include "libpiper-api.hpp"
#include "piper-engine.hpp"
#endif

namespace {
constexpr char kDefaultPiperVoice[] = "en_US-ljspeech-medium";

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

QString piperRuntimeProblem()
{
#ifdef HAVE_LIBPIPER
    return {};
#else
    // The hard link to libpiper/ONNX Runtime is a packaging-integrity choice:
    // a host build without the runtime simply speaks through espeak-ng.
    return QStringLiteral(
        "this build has no libpiper/ONNX Runtime (HAVE_LIBPIPER is not defined)");
#endif
}

QString piperVoiceOrDefault(const Settings &settings)
{
    const QString voice = settings.piperVoice.trimmed();
    return voice.isEmpty() ? QString::fromLatin1(kDefaultPiperVoice) : voice;
}

struct PiperResources {
    QString modelPath;
    QString configPath;
    QString espeakDataPath;
    /* Empty when the requested Piper engine can be created; otherwise each
     * entry explains one missing resource/runtime in user-facing terms. */
    QStringList problems;
};

/* Resolves the bundled voice/model/config/espeak data without touching native
 * code or the UI thread.  Explicit settings paths win over bundled files. */
PiperResources resolvePiperResources(const Settings &settings)
{
    PiperResources resources;

    const QString voice = piperVoiceOrDefault(settings);
    const QString modelRelative = QStringLiteral("piper/") + voice + QStringLiteral(".onnx");
    const QString configRelative = modelRelative + QStringLiteral(".json");

    const QString configuredModel = settings.piperModelPath.trimmed();
    if (!configuredModel.isEmpty()) {
        if (QFileInfo(configuredModel).isFile()) {
            resources.modelPath = configuredModel;
        } else {
            resources.problems.append(QStringLiteral("piper model does not exist: ") +
                                      configuredModel);
        }
    } else {
        resources.modelPath = resolveBundledPath(modelRelative);
        if (resources.modelPath.isEmpty()) {
            resources.problems.append(
                QStringLiteral("piper model not found; looked for: ") +
                bundledPathCandidates(modelRelative).join(QStringLiteral(", ")));
        }
    }

    const QString configuredConfig = settings.piperConfigPath.trimmed();
    if (!configuredConfig.isEmpty()) {
        if (QFileInfo(configuredConfig).isFile()) {
            resources.configPath = configuredConfig;
        } else {
            resources.problems.append(QStringLiteral("piper config does not exist: ") +
                                      configuredConfig);
        }
    } else {
        // libpiper's convention: <model>.onnx.json sits next to the model.
        const QString sibling =
            resources.modelPath.isEmpty() ? QString()
                                          : resources.modelPath + QStringLiteral(".json");
        if (!sibling.isEmpty() && QFileInfo(sibling).isFile())
            resources.configPath = sibling;
        else
            resources.configPath = resolveBundledPath(configRelative);

        if (resources.configPath.isEmpty()) {
            resources.problems.append(
                QStringLiteral("piper config not found; looked for: ") +
                bundledPathCandidates(configRelative).join(QStringLiteral(", ")));
        }
    }

    const QString espeakDataRelative = QStringLiteral("piper/espeak-ng-data");
    resources.espeakDataPath = resolveBundledPath(espeakDataRelative);
    if (resources.espeakDataPath.isEmpty()) {
        resources.problems.append(
            QStringLiteral("piper espeak-ng data not found; looked for: ") +
            bundledPathCandidates(espeakDataRelative).join(QStringLiteral(", ")));
    }

    const QString runtimeProblem = piperRuntimeProblem();
    if (!runtimeProblem.isEmpty())
        resources.problems.append(runtimeProblem);

    return resources;
}

struct EspeakSelection {
    std::unique_ptr<TtsEngine> engine;
    QString binaryPath;
    QString dataPath;
};

/* Resolves and validates the bundled espeak-ng binary.  Errors here are fatal
 * for the connection/test because espeak-ng is the last-resort engine. */
bool createEspeakEngine(const Settings &settings, EspeakSelection *selection, QString *error)
{
    QString binaryPath = settings.espeakPath;
    if (binaryPath.isEmpty())
        binaryPath = resolveBundledPath(QStringLiteral("bin/espeak-ng"));

    if (binaryPath.isEmpty()) {
        const QString tried = bundledPathCandidates(QStringLiteral("bin/espeak-ng"))
                                  .join(QStringLiteral(", "));
        if (error)
            *error = QStringLiteral("espeak-ng was not found; looked for: ") + tried;
        return false;
    }

    const QFileInfo binaryInfo(binaryPath);
    if (!binaryInfo.exists() || !binaryInfo.isFile()) {
        if (error)
            *error = QStringLiteral("espeak-ng does not exist: ") + binaryPath;
        return false;
    }
    if (!binaryInfo.isExecutable()) {
        if (error)
            *error = QStringLiteral("espeak-ng is not executable: ") + binaryPath;
        return false;
    }

    /* The data directory may legitimately be absent in a host install where
     * espeak-ng uses its system data; the Flatpak extension always bundles it. */
    const QString dataPath = resolveBundledPath(QStringLiteral("espeak-ng-data"));

    selection->binaryPath = binaryPath;
    selection->dataPath = dataPath;
    selection->engine = std::make_unique<EspeakEngine>(binaryPath, dataPath, settings.voice,
                                                       settings.rate, settings.pitch,
                                                       settings.amplitude);
    return true;
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

    const bool piperRequested =
        settings_.ttsEngine.trimmed().compare(QLatin1String("piper"), Qt::CaseInsensitive) == 0;
    TtsBackend backend = TtsBackend::Espeak;
    QString fallbackReason;
    PiperResources piper;

    if (forcedEspeak_) {
        /* A Piper engine already failed to initialize in this session.  Keep
         * speaking through espeak until the user changes the engine or asks
         * for an explicit restart. */
        fallbackReason = forcedEspeakReason_.isEmpty()
                             ? QStringLiteral("piper could not initialize")
                             : forcedEspeakReason_;
    } else {
        piper = resolvePiperResources(settings_);
        backend = chooseTtsBackend(settings_, !piper.modelPath.isEmpty());

        /* A requested Piper engine that cannot be created must never go
         * silent: speak through espeak-ng and log the exact missing
         * resource/runtime. */
        if (backend == TtsBackend::Piper && !piper.problems.isEmpty())
            backend = TtsBackend::Espeak;
    }

    if (backend == TtsBackend::Piper) {
#ifdef HAVE_LIBPIPER
        const QString voice = piperVoiceOrDefault(settings_);
        auto api = std::make_unique<LibPiperApi>(piper.modelPath, piper.configPath,
                                                 piper.espeakDataPath);
        auto piperEngine = std::make_unique<PiperEngine>(std::move(api), settings_.rate);
        piperEngine->setSpeed(static_cast<float>(settings_.piperSpeed));
        installWorker(std::move(piperEngine));

        // Report the engine this context actually created, after every check.
        // Construction is lazy: a corrupt model is detected on the worker's
        // first synth, where engineUnavailable() triggers the espeak fallback.
        emit logLine(QStringLiteral("tts engine: piper (") + voice + QLatin1Char(')'));
        emit logLine(QStringLiteral("piper model: ") + piper.modelPath);
        emit logLine(QStringLiteral("piper config: ") + piper.configPath);
        emit logLine(QStringLiteral("piper data: ") + piper.espeakDataPath);
#else
        // Unreachable: piperRuntimeProblem() adds a problem above, but keep a
        // defensive fallback in case that check ever changes.
        backend = TtsBackend::Espeak;
#endif
    }

    if (backend == TtsBackend::Local) {
        auto local = std::make_unique<LocalTtsEngine>(
            settings_.localTtsUrl, settings_.localTtsVoice, settings_.localTtsModel);
        installWorker(std::move(local));
        emit logLine(QStringLiteral("tts engine: local server (") + settings_.localTtsUrl +
                     QLatin1Char(')'));
        emit logLine(QStringLiteral("local tts voice: ") + settings_.localTtsVoice);
    }

    if (backend == TtsBackend::Espeak) {
        QString reason = fallbackReason;
        if (reason.isEmpty() && piperRequested) {
            reason = piper.problems.isEmpty()
                         ? QStringLiteral("piper runtime is unavailable")
                         : piper.problems.join(QStringLiteral("; "));
        }

        EspeakSelection espeak;
        if (!createEspeakEngine(settings_, &espeak, error))
            return false;

        /* Only claim espeak-ng is active after it actually constructed. */
        if (piperRequested && !reason.isEmpty()) {
            emit logLine(QStringLiteral("tts engine: espeak-ng (fallback: ") + reason +
                         QLatin1Char(')'));
        } else {
            emit logLine(QStringLiteral("tts engine: espeak-ng"));
        }

        installWorker(std::move(espeak.engine));
        emit logLine(QStringLiteral("espeak: ") + espeak.binaryPath);
        emit logLine(espeak.dataPath.isEmpty()
                         ? QStringLiteral("espeak data: bundled data not found")
                         : QStringLiteral("espeak data: ") + espeak.dataPath);
    }

    applySettingsToWorker();
    worker_->start();

    engineSelection_ = settings_.ttsEngine;
    enginePiperVoice_ = settings_.piperVoice;
    engineLocalUrl_ = settings_.localTtsUrl;
    engineLocalVoice_ = settings_.localTtsVoice;
    engineLocalModel_ = settings_.localTtsModel;
    enginePiperModelPath_ = settings_.piperModelPath;
    enginePiperConfigPath_ = settings_.piperConfigPath;
    enginePiperSpeed_ = settings_.piperSpeed;
    engineVoice_ = settings_.voice;
    engineRate_ = settings_.rate;
    enginePitch_ = settings_.pitch;
    engineAmplitude_ = settings_.amplitude;

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

    /* The emission is queued onto the UI thread (automatic connection), so the
     * restart below never runs on the worker thread.  The requeued message is
     * spoken by the replacement espeak engine. */
    connect(worker_.get(), &TtsWorker::engineUnavailable, this,
            [this](const QString &reason) {
                forcedEspeak_ = true;
                forcedEspeakReason_ = reason;
                workerUnavailable_ = true;

                emit logLine(QStringLiteral("tts engine: espeak-ng (fallback: ") + reason +
                             QLatin1Char(')'));
                QTimer::singleShot(0, this, [this]() { restartWorkerInternal(); });
            });
}

bool PluginContext::engineSettingsDiffer() const
{
    return worker_ && (settings_.ttsEngine != engineSelection_ ||
                       settings_.piperVoice != enginePiperVoice_ ||
                       settings_.localTtsUrl != engineLocalUrl_ ||
                       settings_.localTtsVoice != engineLocalVoice_ ||
                       settings_.localTtsModel != engineLocalModel_ ||
                       settings_.piperModelPath != enginePiperModelPath_ ||
                       settings_.piperConfigPath != enginePiperConfigPath_ ||
                       settings_.piperSpeed != enginePiperSpeed_ ||
                       settings_.voice != engineVoice_ || settings_.rate != engineRate_ ||
                       settings_.pitch != enginePitch_ ||
                       settings_.amplitude != engineAmplitude_);
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
    /* An explicit restart is the retry path for a Piper model that failed at
     * runtime.  The automatic fallback uses restartWorkerInternal() directly,
     * so it keeps forcedEspeak_ and cannot loop on the bad model. */
    forcedEspeak_ = false;
    forcedEspeakReason_.clear();
    restartWorkerInternal();
}

void PluginContext::applyLiveSettings()
{
    queue_.setLimit(settings_.queueLimit);
    AudioBus::instance().buffer().setVolume(static_cast<float>(settings_.volume));

    /* Only a changed requested engine retries Piper after a runtime fallback.
     * Voice/rate changes preserve the forced-espeak session state. */
    if (worker_ && settings_.ttsEngine != engineSelection_) {
        forcedEspeak_ = false;
        forcedEspeakReason_.clear();
    }

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
