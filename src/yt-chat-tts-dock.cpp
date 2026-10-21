#include "yt-chat-tts-dock.hpp"

#include "audio-source.hpp"
#include "plugin-context.hpp"

#include <obs-module.h>

#include <QDesktopServices>
#include <QListWidget>
#include <QMessageBox>
#include <QSlider>
#include <QTimer>
#include <QUrl>

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>

namespace {

constexpr char kApiKeyUrl[] = "https://console.cloud.google.com/apis/credentials";
constexpr int kQueuePreviewCount = 5;
constexpr int kLogLineLimit = 500;
constexpr int kSaveDebounceMs = 500;
constexpr int kRefreshIntervalMs = 500;
constexpr int kEngineApplyDebounceMs = 250;

const QString kStatusIdleColor = QStringLiteral("#888888");
const QString kStatusBusyColor = QStringLiteral("#d08b00");
const QString kStatusOkColor = QStringLiteral("#2e9e44");
const QString kStatusErrorColor = QStringLiteral("#c0392b");

/* obs_module_text() returns the lookup key when a translation is missing.
 * The dock wants plain English in that case, including on a host build where
 * the module locale was not loaded. */
QString uiText(const char *key, const char *fallback)
{
    const char *translated = obs_module_text(key);
    if (!translated || !*translated || QLatin1String(translated) == QLatin1String(key))
        return QString::fromUtf8(fallback);
    return QString::fromUtf8(translated);
}

QString uiTextf(const char *key, const char *fallback, const QString &value)
{
    return uiText(key, fallback).arg(value);
}

/* The helper reports "official" or "innertube (experimental)" after connecting;
 * the status detail stays short and provider-focused. */
QString providerLabel(const QString &detail)
{
    const QString trimmed = detail.trimmed();
    if (trimmed.startsWith(QLatin1String("innertube")))
        return QStringLiteral("innertube");
    return trimmed;
}

/* Helper errors arrive as "<code>: <human readable detail>" from
 * PluginContext::statusChanged().  Only the documented helper codes are
 * mapped; unknown details pass through unchanged. */
QString mapHelperError(const QString &detail)
{
    const int separator = detail.indexOf(QStringLiteral(": "));
    const QString code = separator > 0 ? detail.left(separator) : detail;

    if (code == QLatin1String("auth"))
        return uiText("Error.Auth", "API key rejected; check the key and API restrictions");
    if (code == QLatin1String("quota"))
        return uiText("Error.Quota",
                      "YouTube quota exceeded; try No-key or wait until midnight PT");
    if (code == QLatin1String("not_live"))
        return uiText("Error.NotLive", "No active live stream found");
    if (code == QLatin1String("chat_disabled"))
        return uiText("Error.ChatDisabled", "Live chat is disabled for this stream");
    if (code == QLatin1String("network"))
        return uiText("Error.Network", "Network error; retrying");
    if (code == QLatin1String("parse"))
        return uiText("Error.Parse", "YouTube response changed; try the other provider");
    return detail;
}

} // namespace

YtChatTtsDock::YtChatTtsDock(PluginContext *context, QWidget *parent)
    : QWidget(parent), context_(context), saveTimer_(new QTimer(this)),
      refreshTimer_(new QTimer(this)), liveApplyTimer_(new QTimer(this))
{
    saveTimer_->setSingleShot(true);
    saveTimer_->setInterval(kSaveDebounceMs);
    connect(saveTimer_, &QTimer::timeout, this, [this]() { saveSettings(); });

    /* Worker pops do not emit queueChanged, and sources can be added or
     * removed outside the dock, so a periodic refresh keeps both labels
     * honest without polling at an aggressive interval. */
    refreshTimer_->setInterval(kRefreshIntervalMs);
    connect(refreshTimer_, &QTimer::timeout, this, &YtChatTtsDock::refreshStatuses);
    refreshTimer_->start();

    /* Slider drags emit many values; coalesce engine restarts instead of
     * stopping/starting espeak-ng on every tick. */
    liveApplyTimer_->setSingleShot(true);
    liveApplyTimer_->setInterval(kEngineApplyDebounceMs);
    connect(liveApplyTimer_, &QTimer::timeout, this, &YtChatTtsDock::applyLiveSettingsNow);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(4, 4, 4, 4);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *settingsWidget = new QWidget(scroll);
    auto *settingsLayout = new QVBoxLayout(settingsWidget);
    settingsLayout->setContentsMargins(0, 0, 0, 0);

    const auto addSliderRow = [](QFormLayout *form, const QString &label, QSlider *slider,
                                 QLabel *valueLabel) {
        auto *row = new QWidget(form->parentWidget());
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        slider->setOrientation(Qt::Horizontal);
        rowLayout->addWidget(slider, 1);
        valueLabel->setMinimumWidth(64);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rowLayout->addWidget(valueLabel);
        form->addRow(label, row);
        return row;
    };

    /* ---------------------------------------------------------------- */
    /* Connection                                                       */
    /* ---------------------------------------------------------------- */
    auto *connectionGroup =
        new QGroupBox(uiText("Connection.Group", "Connection"), settingsWidget);
    auto *connectionForm = new QFormLayout(connectionGroup);

    urlEdit_ = new QLineEdit(connectionGroup);
    urlEdit_->setPlaceholderText(
        uiText("Connection.VideoPlaceholder", "YouTube video URL or ID"));
    connectionForm->addRow(uiText("Connection.Video", "Video"), urlEdit_);

    apiKeyEdit_ = new QLineEdit(connectionGroup);
    apiKeyEdit_->setEchoMode(QLineEdit::Password);
    apiKeyEdit_->setPlaceholderText(
        uiText("Connection.ApiKeyPlaceholder", "Optional API key"));
    connectionForm->addRow(uiText("Connection.ApiKey", "API key"), apiKeyEdit_);

    providerCombo_ = new QComboBox(connectionGroup);
    providerCombo_->addItem(uiText("Connection.ProviderAuto", "Auto"),
                            QStringLiteral("auto"));
    providerCombo_->addItem(uiText("Connection.ProviderOfficial", "Official API"),
                            QStringLiteral("official"));
    providerCombo_->addItem(
        uiText("Connection.ProviderInnertube", "No-key (experimental)"),
        QStringLiteral("innertube"));
    connectionForm->addRow(uiText("Connection.Provider", "Provider"), providerCombo_);

    apiKeyLink_ = new QLabel(connectionGroup);
    apiKeyLink_->setTextFormat(Qt::RichText);
    apiKeyLink_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    apiKeyLink_->setOpenExternalLinks(false);
    apiKeyLink_->setText(
        QStringLiteral("<a href=\"%1\">%2</a>")
            .arg(QString::fromLatin1(kApiKeyUrl), uiText("Connection.GetApiKey", "Get an API key")));
    apiKeyLink_->setToolTip(uiText("Connection.GetApiKeyTooltip",
                                   "Open the Google Cloud API credentials page"));
    connectionForm->addRow(apiKeyLink_);

    auto *connectionButtons = new QHBoxLayout;
    connectButton_ = new QPushButton(uiText("Connection.Connect", "Connect"), connectionGroup);
    connectionButtons->addWidget(connectButton_);
    connectionButtons->addStretch(1);
    connectionForm->addRow(connectionButtons);

    autoConnectCheck_ =
        new QCheckBox(uiText("Connection.AutoConnect", "Connect automatically"), connectionGroup);
    autoConnectCheck_->setToolTip(uiText(
        "Connection.AutoConnectTooltip",
        "Connect on OBS startup when a video URL or ID is already saved."));
    connectionForm->addRow(autoConnectCheck_);

    auto *statusRow = new QHBoxLayout;
    statusDot_ = new QLabel(QStringLiteral("●"), connectionGroup);
    statusDot_->setStyleSheet(QStringLiteral("color: %1;").arg(kStatusIdleColor));
    statusRow->addWidget(statusDot_);
    statusLabel_ =
        new QLabel(uiText("Connection.StatusNotConnected", "Not connected"), connectionGroup);
    statusLabel_->setWordWrap(true);
    statusRow->addWidget(statusLabel_, 1);
    connectionForm->addRow(statusRow);

    settingsLayout->addWidget(connectionGroup);

    /* ---------------------------------------------------------------- */
    /* Voice                                                            */
    /* ---------------------------------------------------------------- */
    auto *voiceGroup = new QGroupBox(uiText("Voice.Group", "Voice"), settingsWidget);
    auto *voiceForm = new QFormLayout(voiceGroup);

    engineCombo_ = new QComboBox(voiceGroup);
    engineCombo_->addItem(uiText("Voice.EnginePiper", "Piper — natural (CPU)"),
                          QStringLiteral("piper"));
    engineCombo_->addItem(uiText("Voice.EngineEspeak", "espeak-ng — fast fallback"),
                          QStringLiteral("espeak"));
    voiceForm->addRow(uiText("Voice.Engine", "Engine"), engineCombo_);

    piperVoiceCombo_ = new QComboBox(voiceGroup);
    piperVoiceCombo_->addItem(
        uiText("Voice.PiperVoiceLjspeech", "LJSpeech — US female (medium)"),
        QStringLiteral("en_US-ljspeech-medium"));
    voiceForm->addRow(uiText("Voice.PiperVoice", "Piper voice"), piperVoiceCombo_);

    voiceCombo_ = new QComboBox(voiceGroup);
    voiceCombo_->setEditable(true);
    voiceCombo_->setInsertPolicy(QComboBox::NoInsert);
    voiceCombo_->addItems({QStringLiteral("en-us"), QStringLiteral("en-gb"),
                           QStringLiteral("en-in"), QStringLiteral("en-gb-scotland"),
                           QStringLiteral("en-029")});
    voiceCombo_->lineEdit()->setPlaceholderText(
        uiText("Voice.VoicePlaceholder", "espeak-ng voice, e.g. en-us"));
    voiceForm->addRow(uiText("Voice.EspeakVoice", "espeak-ng voice"), voiceCombo_);

    rateSlider_ = new QSlider(Qt::Horizontal, voiceGroup);
    rateSlider_->setRange(80, 450);
    rateValueLabel_ = new QLabel(voiceGroup);
    addSliderRow(voiceForm, uiText("Voice.Rate", "Rate"), rateSlider_, rateValueLabel_);

    pitchSlider_ = new QSlider(Qt::Horizontal, voiceGroup);
    pitchSlider_->setRange(0, 200);
    pitchValueLabel_ = new QLabel(voiceGroup);
    pitchRow_ = addSliderRow(voiceForm, uiText("Voice.Pitch", "Pitch"), pitchSlider_,
                             pitchValueLabel_);

    amplitudeSlider_ = new QSlider(Qt::Horizontal, voiceGroup);
    amplitudeSlider_->setRange(0, 200);
    amplitudeValueLabel_ = new QLabel(voiceGroup);
    amplitudeRow_ = addSliderRow(voiceForm, uiText("Voice.Amplitude", "Amplitude"),
                                 amplitudeSlider_, amplitudeValueLabel_);

    volumeSlider_ = new QSlider(Qt::Horizontal, voiceGroup);
    /* Volume is 0.00-1.50 in the settings; the slider works in hundredths. */
    volumeSlider_->setRange(0, 150);
    volumeSlider_->setSingleStep(5);
    volumeValueLabel_ = new QLabel(voiceGroup);
    addSliderRow(voiceForm, uiText("Voice.Volume", "Volume"), volumeSlider_, volumeValueLabel_);

    resetVoiceButton_ =
        new QPushButton(uiText("Voice.ResetDefaults", "Restore voice defaults"), voiceGroup);
    voiceForm->addRow(resetVoiceButton_);

    readAuthorCheck_ =
        new QCheckBox(uiText("Voice.ReadAuthor", "Read the author name"), voiceGroup);
    voiceForm->addRow(readAuthorCheck_);

    authorTemplateEdit_ = new QLineEdit(voiceGroup);
    authorTemplateEdit_->setPlaceholderText(
        uiText("Voice.TemplatePlaceholder", "{author} says: {message}"));
    voiceForm->addRow(uiText("Voice.Template", "Template"), authorTemplateEdit_);

    testVoiceButton_ = new QPushButton(uiText("Voice.Test", "Test Voice"), voiceGroup);
    testVoiceButton_->setToolTip(
        uiText("Voice.TestTooltip", "Speak a test sentence through the TTS audio source."));
    voiceForm->addRow(testVoiceButton_);

    settingsLayout->addWidget(voiceGroup);

    /* ---------------------------------------------------------------- */
    /* Filtering                                                        */
    /* ---------------------------------------------------------------- */
    auto *filterGroup = new QGroupBox(uiText("Filtering.Group", "Filtering"), settingsWidget);
    auto *filterForm = new QFormLayout(filterGroup);

    maxCharsSpin_ = new QSpinBox(filterGroup);
    maxCharsSpin_->setRange(1, 500);
    maxCharsSpin_->setSuffix(QStringLiteral(" ") +
                             uiText("Filtering.CharsSuffix", "chars"));
    filterForm->addRow(uiText("Filtering.MaxChars", "Max length"), maxCharsSpin_);

    userCooldownSpin_ = new QSpinBox(filterGroup);
    userCooldownSpin_->setRange(0, 3600);
    userCooldownSpin_->setSuffix(QStringLiteral(" ") +
                                 uiText("Filtering.SecondsSuffix", "s"));
    filterForm->addRow(uiText("Filtering.UserCooldown", "Per-user cooldown"),
                       userCooldownSpin_);

    globalCooldownSpin_ = new QSpinBox(filterGroup);
    globalCooldownSpin_->setRange(0, 60);
    globalCooldownSpin_->setSuffix(QStringLiteral(" ") +
                                   uiText("Filtering.SecondsSuffix", "s"));
    filterForm->addRow(uiText("Filtering.GlobalCooldown", "Global cooldown"),
                       globalCooldownSpin_);

    queueLimitSpin_ = new QSpinBox(filterGroup);
    queueLimitSpin_->setRange(1, 100);
    filterForm->addRow(uiText("Filtering.QueueLimit", "Queue limit"), queueLimitSpin_);

    commandPrefixEdit_ = new QLineEdit(filterGroup);
    commandPrefixEdit_->setPlaceholderText(
        uiText("Filtering.CommandPrefixPlaceholder", "!"));
    filterForm->addRow(uiText("Filtering.CommandPrefix", "Command prefix"),
                       commandPrefixEdit_);

    skipCommandsCheck_ =
        new QCheckBox(uiText("Filtering.SkipCommands", "Skip commands"), filterGroup);
    filterForm->addRow(skipCommandsCheck_);

    stripUrlsCheck_ = new QCheckBox(uiText("Filtering.StripUrls", "Strip URLs"), filterGroup);
    filterForm->addRow(stripUrlsCheck_);

    skipBlockedCheck_ =
        new QCheckBox(uiText("Filtering.SkipBlocked", "Skip blocked phrases"), filterGroup);
    filterForm->addRow(skipBlockedCheck_);

    blocklistEdit_ = new QPlainTextEdit(filterGroup);
    blocklistEdit_->setPlaceholderText(
        uiText("Filtering.BlocklistPlaceholder", "One phrase per line"));
    blocklistEdit_->setMaximumHeight(70);
    filterForm->addRow(uiText("Filtering.Blocklist", "Blocklist"), blocklistEdit_);

    speakSuperchatsCheck_ =
        new QCheckBox(uiText("Filtering.SpeakSuperchats", "Read Super Chats"), filterGroup);
    filterForm->addRow(speakSuperchatsCheck_);

    speakMembershipsCheck_ =
        new QCheckBox(uiText("Filtering.SpeakMemberships", "Read new members"), filterGroup);
    filterForm->addRow(speakMembershipsCheck_);

    readModeratorsCheck_ =
        new QCheckBox(uiText("Filtering.ReadModerators", "Read moderators"), filterGroup);
    filterForm->addRow(readModeratorsCheck_);

    settingsLayout->addWidget(filterGroup);

    /* ---------------------------------------------------------------- */
    /* Audio source                                                     */
    /* ---------------------------------------------------------------- */
    auto *audioGroup = new QGroupBox(uiText("Audio.Group", "Audio source"), settingsWidget);
    auto *audioLayout = new QVBoxLayout(audioGroup);

    addSourceButton_ =
        new QPushButton(uiText("Audio.AddCurrent", "Add TTS source to current scene"),
                        audioGroup);
    addSourceAllButton_ =
        new QPushButton(uiText("Audio.AddAll", "Add to all scenes"), audioGroup);
    auto *audioButtonRow = new QHBoxLayout;
    audioButtonRow->addWidget(addSourceButton_);
    audioButtonRow->addWidget(addSourceAllButton_);
    audioButtonRow->addStretch(1);
    audioLayout->addLayout(audioButtonRow);

    sourceStatusLabel_ = new QLabel(audioGroup);
    sourceStatusLabel_->setWordWrap(true);
    audioLayout->addWidget(sourceStatusLabel_);
    settingsLayout->addWidget(audioGroup);

    settingsLayout->addStretch(1);
    scroll->setWidget(settingsWidget);
    scroll->setMinimumHeight(220);
    rootLayout->addWidget(scroll, 3);

    /* ---------------------------------------------------------------- */
    /* Queue                                                            */
    /* ---------------------------------------------------------------- */
    auto *queueGroup = new QGroupBox(uiText("Queue.Group", "Queue"), this);
    auto *queueLayout = new QVBoxLayout(queueGroup);

    queueLabel_ = new QLabel(uiTextf("Queue.Count", "Queue: %1", QStringLiteral("0")),
                             queueGroup);
    queueLayout->addWidget(queueLabel_);

    queueList_ = new QListWidget(queueGroup);
    queueList_->setMaximumHeight(110);
    queueList_->setSelectionMode(QAbstractItemView::NoSelection);
    queueList_->setFocusPolicy(Qt::NoFocus);
    queueLayout->addWidget(queueList_);

    auto *queueButtons = new QHBoxLayout;
    skipButton_ = new QPushButton(uiText("Queue.Skip", "Skip Current"), queueGroup);
    skipButton_->setToolTip(
        uiText("Queue.SkipTooltip", "Stops queued messages; the current message may finish."));
    clearQueueButton_ = new QPushButton(uiText("Queue.Clear", "Clear Queue"), queueGroup);
    clearQueueButton_->setToolTip(
        uiText("Queue.ClearTooltip", "Removes every queued message."));
    queueButtons->addWidget(skipButton_);
    queueButtons->addWidget(clearQueueButton_);
    queueButtons->addStretch(1);
    queueLayout->addLayout(queueButtons);

    rootLayout->addWidget(queueGroup);

    /* ---------------------------------------------------------------- */
    /* Log                                                              */
    /* ---------------------------------------------------------------- */
    auto *logGroup = new QGroupBox(uiText("Log.Group", "Log"), this);
    auto *logLayout = new QVBoxLayout(logGroup);

    auto *logHeader = new QHBoxLayout;
    clearLogButton_ = new QPushButton(uiText("Log.Clear", "Clear Log"), logGroup);
    logHeader->addStretch(1);
    logHeader->addWidget(clearLogButton_);
    logLayout->addLayout(logHeader);

    logView_ = new QPlainTextEdit(logGroup);
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(kLogLineLimit);
    logView_->setMinimumHeight(100);
    logLayout->addWidget(logView_, 1);
    rootLayout->addWidget(logGroup, 2);

    /* Seed the controls before wiring signals so loading saved values cannot
     * schedule a save or a worker restart. */
    applySettingsToControls();
    updateValueLabels();
    updateSourceStatus();
    refreshQueue();

    connect(connectButton_, &QPushButton::clicked, this, &YtChatTtsDock::onConnectClicked);
    connect(testVoiceButton_, &QPushButton::clicked, this,
            &YtChatTtsDock::onTestVoiceClicked);
    connect(resetVoiceButton_, &QPushButton::clicked, this,
            &YtChatTtsDock::onResetVoiceClicked);
    connect(skipButton_, &QPushButton::clicked, this, &YtChatTtsDock::onSkipClicked);
    connect(clearQueueButton_, &QPushButton::clicked, this,
            &YtChatTtsDock::onClearQueueClicked);
    connect(addSourceButton_, &QPushButton::clicked, this,
            [this]() { onAddSourceClicked(false); });
    connect(addSourceAllButton_, &QPushButton::clicked, this,
            [this]() { onAddSourceClicked(true); });
    connect(clearLogButton_, &QPushButton::clicked, this, [this]() { logView_->clear(); });
    connect(apiKeyLink_, &QLabel::linkActivated, this, [](const QString &url) {
        QDesktopServices::openUrl(QUrl(url));
    });

    if (context_) {
        connect(context_, &PluginContext::statusChanged, this,
                &YtChatTtsDock::onStatusChanged);
        connect(context_, &PluginContext::logLine, this, &YtChatTtsDock::onLogLine);
        connect(context_, &PluginContext::queueChanged, this,
                &YtChatTtsDock::onQueueChanged);
        /* Plugin unload deletes the dock before the context; this guards the
         * opposite order (frontend teardown) against a dangling pointer. */
        connect(context_, &QObject::destroyed, this, [this]() { context_ = nullptr; });
    }

    connect(urlEdit_, &QLineEdit::textChanged, this, &YtChatTtsDock::onControlChanged);
    connect(apiKeyEdit_, &QLineEdit::textChanged, this, &YtChatTtsDock::onControlChanged);
    connect(providerCombo_, &QComboBox::currentIndexChanged, this,
            &YtChatTtsDock::onControlChanged);
    connect(engineCombo_, &QComboBox::currentIndexChanged, this,
            &YtChatTtsDock::onControlChanged);
    connect(piperVoiceCombo_, &QComboBox::currentIndexChanged, this,
            &YtChatTtsDock::onControlChanged);
    connect(voiceCombo_, &QComboBox::currentTextChanged, this,
            &YtChatTtsDock::onControlChanged);
    connect(rateSlider_, &QSlider::valueChanged, this, &YtChatTtsDock::onControlChanged);
    connect(pitchSlider_, &QSlider::valueChanged, this, &YtChatTtsDock::onControlChanged);
    connect(amplitudeSlider_, &QSlider::valueChanged, this,
            &YtChatTtsDock::onControlChanged);
    connect(volumeSlider_, &QSlider::valueChanged, this, &YtChatTtsDock::onControlChanged);
    connect(readAuthorCheck_, &QCheckBox::toggled, this, &YtChatTtsDock::onControlChanged);
    connect(authorTemplateEdit_, &QLineEdit::textChanged, this,
            &YtChatTtsDock::onControlChanged);
    connect(maxCharsSpin_, &QSpinBox::valueChanged, this, &YtChatTtsDock::onControlChanged);
    connect(userCooldownSpin_, &QSpinBox::valueChanged, this,
            &YtChatTtsDock::onControlChanged);
    connect(globalCooldownSpin_, &QSpinBox::valueChanged, this,
            &YtChatTtsDock::onControlChanged);
    connect(queueLimitSpin_, &QSpinBox::valueChanged, this,
            &YtChatTtsDock::onControlChanged);
    connect(commandPrefixEdit_, &QLineEdit::textChanged, this,
            &YtChatTtsDock::onControlChanged);
    connect(skipCommandsCheck_, &QCheckBox::toggled, this,
            &YtChatTtsDock::onControlChanged);
    connect(stripUrlsCheck_, &QCheckBox::toggled, this, &YtChatTtsDock::onControlChanged);
    connect(skipBlockedCheck_, &QCheckBox::toggled, this,
            &YtChatTtsDock::onControlChanged);
    connect(blocklistEdit_, &QPlainTextEdit::textChanged, this,
            &YtChatTtsDock::onControlChanged);
    connect(speakSuperchatsCheck_, &QCheckBox::toggled, this,
            &YtChatTtsDock::onControlChanged);
    connect(speakMembershipsCheck_, &QCheckBox::toggled, this,
            &YtChatTtsDock::onControlChanged);
    connect(readModeratorsCheck_, &QCheckBox::toggled, this,
            &YtChatTtsDock::onControlChanged);
    connect(autoConnectCheck_, &QCheckBox::toggled, this,
            &YtChatTtsDock::onControlChanged);

    /* Auto-connect must run after the dock exists so failures can be logged
     * and surfaced without a modal dialog blocking OBS startup. */
    QTimer::singleShot(0, this, &YtChatTtsDock::maybeAutoConnect);
}

YtChatTtsDock::~YtChatTtsDock()
{
    saveTimer_->stop();
    refreshTimer_->stop();
    liveApplyTimer_->stop();

    /* Persist pending edits directly (not through saveSettings()) so teardown
     * never restarts the worker.  context_ is null if the context was already
     * destroyed. */
    if (context_) {
        context_->settings() = settingsFromControls();
        if (!context_->settings().save())
            blog(LOG_WARNING, "[yt-chat-tts] settings save failed during dock teardown");
    }
}

void YtChatTtsDock::applySettingsToControls()
{
    loading_ = true;

    const Settings settings = context_ ? context_->settings() : Settings{};
    urlEdit_->setText(settings.videoUrl);
    apiKeyEdit_->setText(settings.apiKey);
    const int providerIndex = providerCombo_->findData(settings.provider);
    providerCombo_->setCurrentIndex(providerIndex < 0 ? 0 : providerIndex);

    const int engineIndex = engineCombo_->findData(settings.ttsEngine);
    engineCombo_->setCurrentIndex(engineIndex < 0 ? 0 : engineIndex);

    int piperVoiceIndex = piperVoiceCombo_->findData(settings.piperVoice);
    if (piperVoiceIndex < 0) {
        /* Keep a hand-edited voice id selectable instead of silently
         * replacing it with the bundled default. */
        piperVoiceCombo_->addItem(settings.piperVoice, settings.piperVoice);
        piperVoiceIndex = piperVoiceCombo_->count() - 1;
    }
    piperVoiceCombo_->setCurrentIndex(piperVoiceIndex);

    voiceCombo_->setCurrentText(settings.voice);
    rateSlider_->setValue(settings.rate);
    pitchSlider_->setValue(settings.pitch);
    amplitudeSlider_->setValue(settings.amplitude);
    volumeSlider_->setValue(qRound(settings.volume * 100.0));
    readAuthorCheck_->setChecked(settings.readAuthor);
    authorTemplateEdit_->setText(settings.authorTemplate);
    maxCharsSpin_->setValue(settings.maxChars);
    userCooldownSpin_->setValue(settings.userCooldownSec);
    globalCooldownSpin_->setValue(settings.globalCooldownSec);
    queueLimitSpin_->setValue(settings.queueLimit);
    commandPrefixEdit_->setText(settings.commandPrefix);
    skipCommandsCheck_->setChecked(settings.skipCommands);
    stripUrlsCheck_->setChecked(settings.stripUrls);
    skipBlockedCheck_->setChecked(settings.skipBlocked);
    blocklistEdit_->setPlainText(settings.blocklist);
    speakSuperchatsCheck_->setChecked(settings.speakSuperchats);
    speakMembershipsCheck_->setChecked(settings.speakMemberships);
    readModeratorsCheck_->setChecked(settings.readModerators);
    autoConnectCheck_->setChecked(settings.autoConnect);
    updateEngineControls();

    loading_ = false;
}

Settings YtChatTtsDock::settingsFromControls() const
{
    /* Preserve fields without a control (explicit helper/espeak paths). */
    Settings settings = context_ ? context_->settings() : Settings{};
    settings.provider = providerCombo_->currentData().toString();
    settings.apiKey = apiKeyEdit_->text();
    settings.videoUrl = urlEdit_->text().trimmed();
    settings.voice = voiceCombo_->currentText().trimmed();
    const QString engine = engineCombo_->currentData().toString();
    settings.ttsEngine =
        engine == QLatin1String("espeak") ? QStringLiteral("espeak") : QStringLiteral("piper");
    settings.piperVoice = piperVoiceCombo_->currentData().toString().trimmed();
    if (settings.piperVoice.isEmpty())
        settings.piperVoice = QStringLiteral("en_US-ljspeech-medium");
    settings.rate = rateSlider_->value();
    settings.pitch = pitchSlider_->value();
    settings.amplitude = amplitudeSlider_->value();
    settings.volume = volumeSlider_->value() / 100.0;
    settings.readAuthor = readAuthorCheck_->isChecked();
    settings.authorTemplate = authorTemplateEdit_->text();
    settings.maxChars = maxCharsSpin_->value();
    settings.userCooldownSec = userCooldownSpin_->value();
    settings.globalCooldownSec = globalCooldownSpin_->value();
    settings.queueLimit = queueLimitSpin_->value();
    settings.commandPrefix = commandPrefixEdit_->text();
    settings.skipCommands = skipCommandsCheck_->isChecked();
    settings.stripUrls = stripUrlsCheck_->isChecked();
    settings.skipBlocked = skipBlockedCheck_->isChecked();
    settings.blocklist = blocklistEdit_->toPlainText();
    settings.speakSuperchats = speakSuperchatsCheck_->isChecked();
    settings.speakMemberships = speakMembershipsCheck_->isChecked();
    settings.readModerators = readModeratorsCheck_->isChecked();
    settings.autoConnect = autoConnectCheck_->isChecked();
    return settings;
}

void YtChatTtsDock::scheduleSave()
{
    if (!loading_)
        saveTimer_->start();
}

bool YtChatTtsDock::saveSettings()
{
    if (!context_)
        return false;

    context_->settings() = settingsFromControls();
    /* The context logs "settings: save failed" on its own; the return value
     * lets any later caller avoid reporting a false success. */
    return context_->saveSettings();
}

void YtChatTtsDock::onControlChanged()
{
    if (loading_)
        return;

    updateValueLabels();

    QObject *changed = sender();
    const bool engineSelection = changed == engineCombo_ || changed == piperVoiceCombo_;
    const bool engineControl = changed == voiceCombo_ || changed == rateSlider_ ||
                               changed == pitchSlider_ || changed == amplitudeSlider_ ||
                               engineSelection;
    if (engineSelection) {
        /* Coalesce with slider drags: one apply after the user settles on an
         * engine/voice.  PluginContext owns the restart decision. */
        updateEngineControls();
    }
    if (engineControl) {
        liveApplyTimer_->start();
    } else {
        applyLiveSettingsNow();
    }

    if (changed == queueLimitSpin_)
        refreshQueue();

    scheduleSave();
}

void YtChatTtsDock::applyLiveSettingsNow()
{
    if (!context_)
        return;

    context_->settings() = settingsFromControls();

    /* PluginContext detects engine/voice/rate changes from the settings it was
     * given and restarts the worker itself.  It is also the only place that
     * logs the active engine, so a missing Piper model is reported as the
     * espeak fallback rather than a false "Piper active" claim. */
    context_->applyLiveSettings();
}

void YtChatTtsDock::onConnectClicked()
{
    if (!context_)
        return;

    if (connected_) {
        context_->disconnectChat();
        setConnected(false);
        return;
    }

    /* Persist pending edits and connect with exactly what the controls show. */
    saveTimer_->stop();
    context_->settings() = settingsFromControls();

    const QString video = context_->settings().videoUrl.trimmed();
    if (video.isEmpty()) {
        const QString message =
            uiText("Connection.ErrorUrlRequired", "Enter a YouTube video URL or ID");
        appendLog(QStringLiteral("error: a video URL or ID is required"));
        setStatus(message, kStatusErrorColor);
        QMessageBox::warning(this, uiText("Connection.Group", "Connection"), message);
        return;
    }

    /* Flush any coalesced engine/voice apply first: saveSettings() applies the
     * settings synchronously, and PluginContext restarts before connectChat()
     * starts speech.  This prevents a pending timer from clearing the first
     * utterance or recreating the worker after the connection starts. */
    liveApplyTimer_->stop();
    saveSettings();

    QString error;
    if (!context_->connectChat(&error)) {
        appendLog(QStringLiteral("error: ") + error);
        setStatus(uiTextf("Connection.StatusError", "Error: %1", error), kStatusErrorColor);
        setConnected(false);
        QMessageBox::critical(this, uiText("Connection.ErrorTitle", "Connection failed"),
                              error);
        return;
    }

    setConnected(true);
    setStatus(uiText("Connection.StatusConnecting", "Connecting…"), kStatusBusyColor);

    if (!context_->hasAudioSource()) {
        const QString warning = uiText(
            "Connection.WarningNoSource",
            "No TTS audio source is added; chat is read but audio will be silent");
        appendLog(QStringLiteral("warning: ") + warning);
        updateSourceStatus();
    }
}

void YtChatTtsDock::onTestVoiceClicked()
{
    if (!context_)
        return;

    saveTimer_->stop();
    /* Flush a pending engine/voice apply before queueing the test: the restart
     * must happen before the first utterance, not while it is being spoken. */
    liveApplyTimer_->stop();
    saveSettings();
    context_->testVoice();

    if (!context_->hasAudioSource()) {
        const QString warning = uiText(
            "Voice.WarningNoSource",
            "No TTS audio source is added; test audio is buffered but silent");
        appendLog(QStringLiteral("warning: ") + warning);
        updateSourceStatus();
    }
}

void YtChatTtsDock::onSkipClicked()
{
    if (!context_)
        return;

    context_->skipCurrent();
    appendLog(uiText("Log.Skipped",
                     "tts: skipped queued messages (current message may finish)"));
}

void YtChatTtsDock::onClearQueueClicked()
{
    if (!context_)
        return;

    context_->clearQueue();
    appendLog(uiText("Log.QueueCleared", "tts: queue cleared"));
    refreshQueue();
}

void YtChatTtsDock::onAddSourceClicked(bool allScenes)
{
    /* Source messages belong on the source status label so they never replace
     * the chat connection state. */
    if (audio_source_exists()) {
        const QString message =
            uiText("Audio.AlreadyExists", "TTS source already exists; not adding another");
        appendLog(QStringLiteral("audio: ") + message);
        sourceStatusLabel_->setText(message);
        return;
    }

    create_audio_source(allScenes);

    if (audio_source_exists()) {
        const QString message =
            allScenes ? uiText("Audio.AddedAll", "TTS source added to all scenes")
                      : uiText("Audio.AddedCurrent", "TTS source added to the current scene");
        appendLog(QStringLiteral("audio: ") + message);
        sourceStatusLabel_->setText(message);
    } else {
        const QString message = uiText(
            "Audio.AddFailed", "Could not add the TTS source; open a scene and try again");
        appendLog(QStringLiteral("audio: ") + message);
        sourceStatusLabel_->setText(message);
    }
}

void YtChatTtsDock::updateSourceStatus()
{
    if (!context_)
        return;

    sourceStatusLabel_->setText(
        context_->hasAudioSource()
            ? uiText("Audio.SourceAdded", "TTS source: added")
            : uiText("Audio.SourceMissing",
                     "TTS source: not added (audio will be silent)"));
}

void YtChatTtsDock::updateValueLabels()
{
    const bool piper = engineCombo_->currentData().toString() != QLatin1String("espeak");
    if (piper) {
        const double lengthScale = 165.0 / qMax(1, rateSlider_->value());
        rateValueLabel_->setText(
            uiTextf("Voice.PiperRateValue", "%1×", QString::number(lengthScale, 'f', 2)));
    } else {
        rateValueLabel_->setText(
            uiTextf("Voice.RateValue", "%1 wpm", QString::number(rateSlider_->value())));
    }
    pitchValueLabel_->setText(QString::number(pitchSlider_->value()));
    amplitudeValueLabel_->setText(QString::number(amplitudeSlider_->value()));
    volumeValueLabel_->setText(
        QString::number(volumeSlider_->value() / 100.0, 'f', 2));
}

void YtChatTtsDock::updateEngineControls()
{
    const bool piper = engineCombo_->currentData().toString() != QLatin1String("espeak");

    /* Each engine keeps its own voice control so switching back and forth
     * never overwrites the other engine's saved choice. */
    piperVoiceCombo_->setEnabled(piper);
    voiceCombo_->setEnabled(!piper);

    /* Piper has no pitch/amplitude knobs; its voice model fixes them. */
    const QString tooltip =
        piper ? uiText("Voice.PiperFixedTooltip",
                       "Pitch and amplitude are fixed for Piper voices.")
              : QString();
    pitchSlider_->setEnabled(!piper);
    amplitudeSlider_->setEnabled(!piper);
    pitchSlider_->setToolTip(tooltip);
    amplitudeSlider_->setToolTip(tooltip);
    if (pitchRow_)
        pitchRow_->setVisible(!piper);
    if (amplitudeRow_)
        amplitudeRow_->setVisible(!piper);
    updateValueLabels();
}

void YtChatTtsDock::onResetVoiceClicked()
{
    rateSlider_->setValue(165);
    pitchSlider_->setValue(50);
    amplitudeSlider_->setValue(150);
    volumeSlider_->setValue(100);
    updateValueLabels();
    applyLiveSettingsNow();
    scheduleSave();
    appendLog(QStringLiteral("voice settings restored to defaults"));
}

void YtChatTtsDock::onStatusChanged(const QString &state, const QString &detail)
{
    if (state == QLatin1String("connected")) {
        const QString provider = providerLabel(detail);
        const QString text =
            provider.isEmpty()
                ? uiText("Connection.StatusConnected", "Connected")
                : uiTextf("Connection.StatusConnectedProvider", "Connected (%1)", provider);
        setStatus(text, kStatusOkColor);
        appendLog(QStringLiteral("status: connected (") + detail + QLatin1Char(')'));
        setConnected(true);
        return;
    }

    if (state == QLatin1String("connecting")) {
        const QString provider = providerLabel(detail);
        const QString text =
            provider.isEmpty()
                ? uiText("Connection.StatusConnecting", "Connecting…")
                : uiTextf("Connection.StatusConnectingProvider", "Connecting… (%1)", provider);
        setStatus(text, kStatusBusyColor);
        appendLog(QStringLiteral("status[connecting]: ") + detail);
        return;
    }

    if (state == QLatin1String("reconnecting")) {
        setStatus(uiText("Connection.StatusReconnecting", "Reconnecting…"),
                  kStatusBusyColor);
        appendLog(QStringLiteral("status[reconnecting]: ") + detail);
        return;
    }

    if (state == QLatin1String("ended")) {
        setStatus(uiText("Connection.StatusEnded", "Stream ended"), kStatusIdleColor);
        appendLog(QStringLiteral("status: helper ended"));
        setConnected(false);
        return;
    }

    if (state == QLatin1String("disconnected")) {
        setStatus(uiText("Connection.StatusNotConnected", "Not connected"),
                  kStatusIdleColor);
        appendLog(QStringLiteral("disconnected"));
        setConnected(false);
        return;
    }

    if (state == QLatin1String("error")) {
        const QString mapped = mapHelperError(detail);
        setStatus(uiTextf("Connection.StatusError", "Error: %1", mapped),
                  kStatusErrorColor);
        appendLog(QStringLiteral("error: ") + mapped);
        setConnected(false);
        return;
    }

    setStatus(state + QStringLiteral("…"), kStatusIdleColor);
    appendLog(QStringLiteral("status[") + state + QStringLiteral("]: ") + detail);
}

void YtChatTtsDock::onLogLine(const QString &line)
{
    appendLog(line);
}

void YtChatTtsDock::onQueueChanged()
{
    refreshQueue();
}

void YtChatTtsDock::refreshQueue()
{
    if (!queueList_ || !queueLabel_)
        return;

    queueList_->clear();
    if (!context_) {
        queueLabel_->setText(uiTextf("Queue.Count", "Queue: %1", QStringLiteral("0")));
        return;
    }

    const std::vector<ChatMessage> snapshot = context_->queueSnapshot(100);
    const size_t previewCount = std::min(snapshot.size(), size_t(kQueuePreviewCount));
    for (size_t i = 0; i < previewCount; ++i) {
        const ChatMessage &message = snapshot[i];
        const QString text = message.author.isEmpty()
                                 ? message.text
                                 : message.author + QStringLiteral(": ") + message.text;
        auto *item = new QListWidgetItem(text, queueList_);
        item->setToolTip(text);
    }

    queueLabel_->setText(
        uiTextf("Queue.Count", "Queue: %1", QString::number(snapshot.size())));
}

void YtChatTtsDock::refreshStatuses()
{
    refreshQueue();
    updateSourceStatus();
}

void YtChatTtsDock::maybeAutoConnect()
{
    if (!context_ || !autoConnectCheck_->isChecked())
        return;

    const QString video = urlEdit_->text().trimmed();
    if (video.isEmpty())
        return;

    context_->settings() = settingsFromControls();

    if (!context_->hasAudioSource()) {
        const QString warning = uiText(
            "Connection.WarningNoSource",
            "No TTS audio source is added; chat is read but audio will be silent");
        appendLog(QStringLiteral("warning: ") + warning);
        updateSourceStatus();
    }

    QString error;
    if (!context_->connectChat(&error)) {
        appendLog(uiTextf("Log.AutoConnectFailed", "auto-connect failed: %1", error));
        setStatus(uiTextf("Connection.StatusError", "Error: %1", error), kStatusErrorColor);
        setConnected(false);
        return;
    }

    setConnected(true);
    setStatus(uiText("Connection.StatusConnecting", "Connecting…"), kStatusBusyColor);
}

void YtChatTtsDock::setConnected(bool connected)
{
    connected_ = connected;
    urlEdit_->setEnabled(!connected);
    apiKeyEdit_->setEnabled(!connected);
    providerCombo_->setEnabled(!connected);
    connectButton_->setText(
        connected ? uiText("Connection.Disconnect", "Disconnect")
                  : uiText("Connection.Connect", "Connect"));
}

void YtChatTtsDock::setStatus(const QString &text, const QString &color)
{
    statusLabel_->setText(text);
    statusDot_->setStyleSheet(QStringLiteral("color: %1;").arg(color));
}

void YtChatTtsDock::appendLog(const QString &line)
{
    logView_->appendPlainText(line);
}
