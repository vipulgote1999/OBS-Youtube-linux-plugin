#include "yt-chat-tts-dock.hpp"

#include "audio-source.hpp"
#include "paths.hpp"
#include "plugin-context.hpp"

#include <QMessageBox>
#include <QTimer>

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
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

YtChatTtsDock::YtChatTtsDock(PluginContext *context, QWidget *parent)
    : QWidget(parent), context_(context), saveTimer_(new QTimer(this))
{
    saveTimer_->setSingleShot(true);
    saveTimer_->setInterval(500);
    connect(saveTimer_, &QTimer::timeout, this, &YtChatTtsDock::saveSettings);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto *settingsWidget = new QWidget;
    auto *settingsLayout = new QVBoxLayout(settingsWidget);
    settingsLayout->setContentsMargins(0, 0, 0, 0);

    auto *connectionGroup = new QGroupBox(QStringLiteral("Connection"), settingsWidget);
    auto *connectionForm = new QFormLayout(connectionGroup);

    urlEdit_ = new QLineEdit(connectionGroup);
    urlEdit_->setPlaceholderText(QStringLiteral("YouTube video URL or ID"));
    connectionForm->addRow(QStringLiteral("Video"), urlEdit_);

    apiKeyEdit_ = new QLineEdit(connectionGroup);
    apiKeyEdit_->setEchoMode(QLineEdit::Password);
    apiKeyEdit_->setPlaceholderText(QStringLiteral("Optional API key"));
    connectionForm->addRow(QStringLiteral("API key"), apiKeyEdit_);

    providerCombo_ = new QComboBox(connectionGroup);
    providerCombo_->addItem(QStringLiteral("Auto"), QStringLiteral("auto"));
    providerCombo_->addItem(QStringLiteral("Official API"), QStringLiteral("official"));
    providerCombo_->addItem(QStringLiteral("No-key (experimental)"), QStringLiteral("innertube"));
    connectionForm->addRow(QStringLiteral("Provider"), providerCombo_);
    settingsLayout->addWidget(connectionGroup);

    auto *voiceGroup = new QGroupBox(QStringLiteral("Voice"), settingsWidget);
    auto *voiceForm = new QFormLayout(voiceGroup);

    voiceEdit_ = new QLineEdit(voiceGroup);
    voiceEdit_->setPlaceholderText(QStringLiteral("espeak-ng voice, e.g. en-us"));
    voiceForm->addRow(QStringLiteral("Voice"), voiceEdit_);

    rateSpin_ = new QSpinBox(voiceGroup);
    rateSpin_->setRange(80, 450);
    rateSpin_->setSuffix(QStringLiteral(" wpm"));
    voiceForm->addRow(QStringLiteral("Rate"), rateSpin_);

    pitchSpin_ = new QSpinBox(voiceGroup);
    pitchSpin_->setRange(0, 200);
    voiceForm->addRow(QStringLiteral("Pitch"), pitchSpin_);

    volumeSpin_ = new QDoubleSpinBox(voiceGroup);
    volumeSpin_->setRange(0.0, 1.5);
    volumeSpin_->setSingleStep(0.05);
    volumeSpin_->setDecimals(2);
    voiceForm->addRow(QStringLiteral("Volume"), volumeSpin_);

    maxCharsSpin_ = new QSpinBox(voiceGroup);
    maxCharsSpin_->setRange(1, 500);
    maxCharsSpin_->setSuffix(QStringLiteral(" chars"));
    voiceForm->addRow(QStringLiteral("Max length"), maxCharsSpin_);
    settingsLayout->addWidget(voiceGroup);

    auto *audioGroup = new QGroupBox(QStringLiteral("Audio output"), settingsWidget);
    auto *audioLayout = new QVBoxLayout(audioGroup);

    addSourceButton_ = new QPushButton(QStringLiteral("Add TTS source to current scene"), audioGroup);
    addSourceAllButton_ = new QPushButton(QStringLiteral("Add to all scenes"), audioGroup);
    auto *audioButtonRow = new QHBoxLayout;
    audioButtonRow->addWidget(addSourceButton_);
    audioButtonRow->addWidget(addSourceAllButton_);
    audioButtonRow->addStretch(1);
    audioLayout->addLayout(audioButtonRow);

    sourceStatusLabel_ = new QLabel(audioGroup);
    sourceStatusLabel_->setWordWrap(true);
    audioLayout->addWidget(sourceStatusLabel_);
    settingsLayout->addWidget(audioGroup);

    auto *filterGroup = new QGroupBox(QStringLiteral("Filtering"), settingsWidget);
    auto *filterForm = new QFormLayout(filterGroup);

    userCooldownSpin_ = new QSpinBox(filterGroup);
    userCooldownSpin_->setRange(0, 3600);
    userCooldownSpin_->setSuffix(QStringLiteral(" s"));
    filterForm->addRow(QStringLiteral("Per-user cooldown"), userCooldownSpin_);

    globalCooldownSpin_ = new QSpinBox(filterGroup);
    globalCooldownSpin_->setRange(0, 60);
    globalCooldownSpin_->setSuffix(QStringLiteral(" s"));
    filterForm->addRow(QStringLiteral("Global cooldown"), globalCooldownSpin_);

    queueLimitSpin_ = new QSpinBox(filterGroup);
    queueLimitSpin_->setRange(1, 100);
    filterForm->addRow(QStringLiteral("Queue limit"), queueLimitSpin_);

    commandPrefixEdit_ = new QLineEdit(filterGroup);
    commandPrefixEdit_->setPlaceholderText(QStringLiteral("!"));
    filterForm->addRow(QStringLiteral("Command prefix"), commandPrefixEdit_);

    skipCommandsCheck_ = new QCheckBox(QStringLiteral("Skip commands"), filterGroup);
    filterForm->addRow(skipCommandsCheck_);

    stripUrlsCheck_ = new QCheckBox(QStringLiteral("Strip URLs"), filterGroup);
    filterForm->addRow(stripUrlsCheck_);

    skipBlockedCheck_ = new QCheckBox(QStringLiteral("Skip blocked phrases"), filterGroup);
    filterForm->addRow(skipBlockedCheck_);

    blocklistEdit_ = new QPlainTextEdit(filterGroup);
    blocklistEdit_->setPlaceholderText(QStringLiteral("One phrase per line"));
    blocklistEdit_->setMaximumHeight(70);
    filterForm->addRow(QStringLiteral("Blocklist"), blocklistEdit_);
    settingsLayout->addWidget(filterGroup);

    auto *messageGroup = new QGroupBox(QStringLiteral("Messages"), settingsWidget);
    auto *messageForm = new QFormLayout(messageGroup);

    readAuthorCheck_ = new QCheckBox(QStringLiteral("Read the author name"), messageGroup);
    messageForm->addRow(readAuthorCheck_);

    authorTemplateEdit_ = new QLineEdit(messageGroup);
    authorTemplateEdit_->setPlaceholderText(QStringLiteral("{author} says: {message}"));
    messageForm->addRow(QStringLiteral("Template"), authorTemplateEdit_);

    speakSuperchatsCheck_ = new QCheckBox(QStringLiteral("Read Super Chats"), messageGroup);
    messageForm->addRow(speakSuperchatsCheck_);

    speakMembershipsCheck_ = new QCheckBox(QStringLiteral("Read new members"), messageGroup);
    messageForm->addRow(speakMembershipsCheck_);

    readModeratorsCheck_ = new QCheckBox(QStringLiteral("Read moderators"), messageGroup);
    messageForm->addRow(readModeratorsCheck_);
    settingsLayout->addWidget(messageGroup);

    settingsLayout->addStretch(1);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(settingsWidget);
    scroll->setMinimumHeight(220);
    layout->addWidget(scroll, 1);

    auto *connectionButtons = new QHBoxLayout;
    connectButton_ = new QPushButton(QStringLiteral("Connect"), this);
    saveButton_ = new QPushButton(QStringLiteral("Save"), this);
    connectionButtons->addWidget(connectButton_);
    connectionButtons->addWidget(saveButton_);
    connectionButtons->addStretch(1);
    layout->addLayout(connectionButtons);

    auto *speechButtons = new QHBoxLayout;
    testVoiceButton_ = new QPushButton(QStringLiteral("Test Voice"), this);
    testVoiceButton_->setToolTip(
        QStringLiteral("Speak a test sentence through the TTS audio source."));
    skipButton_ = new QPushButton(QStringLiteral("Skip Current"), this);
    skipButton_->setToolTip(
        QStringLiteral("Stops queued messages; the current message may finish."));
    clearQueueButton_ = new QPushButton(QStringLiteral("Clear Queue"), this);
    clearQueueButton_->setToolTip(QStringLiteral("Removes every queued message."));
    speechButtons->addWidget(testVoiceButton_);
    speechButtons->addWidget(skipButton_);
    speechButtons->addWidget(clearQueueButton_);
    speechButtons->addStretch(1);
    layout->addLayout(speechButtons);

    statusLabel_ = new QLabel(QStringLiteral("Not connected"), this);
    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    queueLabel_ = new QLabel(QStringLiteral("Queue: 0"), this);
    layout->addWidget(queueLabel_);

    logView_ = new QPlainTextEdit(this);
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(1000);
    layout->addWidget(logView_, 1);

    applySettingsToControls();
    updateSourceStatus();

    connect(connectButton_, &QPushButton::clicked, this, &YtChatTtsDock::onConnectClicked);
    connect(saveButton_, &QPushButton::clicked, this, &YtChatTtsDock::onSaveClicked);
    connect(testVoiceButton_, &QPushButton::clicked, this, &YtChatTtsDock::onTestVoiceClicked);
    connect(skipButton_, &QPushButton::clicked, this, &YtChatTtsDock::onSkipClicked);
    connect(clearQueueButton_, &QPushButton::clicked, this, &YtChatTtsDock::onClearQueueClicked);
    connect(addSourceButton_, &QPushButton::clicked, this,
            [this]() { onAddSourceClicked(false); });
    connect(addSourceAllButton_, &QPushButton::clicked, this,
            [this]() { onAddSourceClicked(true); });

    if (context_) {
        connect(context_, &PluginContext::statusChanged, this, &YtChatTtsDock::onStatusChanged);
        connect(context_, &PluginContext::logLine, this, &YtChatTtsDock::onLogLine);
        connect(context_, &PluginContext::queueChanged, this, &YtChatTtsDock::onQueueChanged);
        /* Plugin unload deletes the dock before the context; this guards the
         * opposite order (frontend teardown) against a dangling pointer. */
        connect(context_, &QObject::destroyed, this, [this]() { context_ = nullptr; });
    }

    connect(urlEdit_, &QLineEdit::textChanged, this, &YtChatTtsDock::scheduleSave);
    connect(apiKeyEdit_, &QLineEdit::textChanged, this, &YtChatTtsDock::scheduleSave);
    connect(providerCombo_, &QComboBox::currentIndexChanged, this, &YtChatTtsDock::scheduleSave);
    connect(voiceEdit_, &QLineEdit::textChanged, this, &YtChatTtsDock::scheduleSave);
    connect(rateSpin_, &QSpinBox::valueChanged, this, &YtChatTtsDock::scheduleSave);
    connect(pitchSpin_, &QSpinBox::valueChanged, this, &YtChatTtsDock::scheduleSave);
    connect(volumeSpin_, &QDoubleSpinBox::valueChanged, this, &YtChatTtsDock::scheduleSave);
    connect(maxCharsSpin_, &QSpinBox::valueChanged, this, &YtChatTtsDock::scheduleSave);
    connect(userCooldownSpin_, &QSpinBox::valueChanged, this, &YtChatTtsDock::scheduleSave);
    connect(globalCooldownSpin_, &QSpinBox::valueChanged, this, &YtChatTtsDock::scheduleSave);
    connect(queueLimitSpin_, &QSpinBox::valueChanged, this, &YtChatTtsDock::scheduleSave);
    connect(commandPrefixEdit_, &QLineEdit::textChanged, this, &YtChatTtsDock::scheduleSave);
    connect(skipCommandsCheck_, &QCheckBox::toggled, this, &YtChatTtsDock::scheduleSave);
    connect(stripUrlsCheck_, &QCheckBox::toggled, this, &YtChatTtsDock::scheduleSave);
    connect(skipBlockedCheck_, &QCheckBox::toggled, this, &YtChatTtsDock::scheduleSave);
    connect(blocklistEdit_, &QPlainTextEdit::textChanged, this, &YtChatTtsDock::scheduleSave);
    connect(readAuthorCheck_, &QCheckBox::toggled, this, &YtChatTtsDock::scheduleSave);
    connect(authorTemplateEdit_, &QLineEdit::textChanged, this, &YtChatTtsDock::scheduleSave);
    connect(speakSuperchatsCheck_, &QCheckBox::toggled, this, &YtChatTtsDock::scheduleSave);
    connect(speakMembershipsCheck_, &QCheckBox::toggled, this, &YtChatTtsDock::scheduleSave);
    connect(readModeratorsCheck_, &QCheckBox::toggled, this, &YtChatTtsDock::scheduleSave);
}

YtChatTtsDock::~YtChatTtsDock()
{
    if (saveTimer_->isActive())
        saveTimer_->stop();

    /* Persist pending edits directly (not through saveSettings()) so teardown
     * never restarts the worker.  context_ is null if the context was already
     * destroyed. */
    if (context_) {
        context_->settings() = settingsFromControls();
        if (!context_->settings().save())
            appendLog(QStringLiteral("settings: save failed"));
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
    voiceEdit_->setText(settings.voice);
    rateSpin_->setValue(settings.rate);
    pitchSpin_->setValue(settings.pitch);
    volumeSpin_->setValue(settings.volume);
    maxCharsSpin_->setValue(settings.maxChars);
    userCooldownSpin_->setValue(settings.userCooldownSec);
    globalCooldownSpin_->setValue(settings.globalCooldownSec);
    queueLimitSpin_->setValue(settings.queueLimit);
    commandPrefixEdit_->setText(settings.commandPrefix);
    skipCommandsCheck_->setChecked(settings.skipCommands);
    stripUrlsCheck_->setChecked(settings.stripUrls);
    skipBlockedCheck_->setChecked(settings.skipBlocked);
    blocklistEdit_->setPlainText(settings.blocklist);
    readAuthorCheck_->setChecked(settings.readAuthor);
    authorTemplateEdit_->setText(settings.authorTemplate);
    speakSuperchatsCheck_->setChecked(settings.speakSuperchats);
    speakMembershipsCheck_->setChecked(settings.speakMemberships);
    readModeratorsCheck_->setChecked(settings.readModerators);

    loading_ = false;
}

Settings YtChatTtsDock::settingsFromControls() const
{
    /* Preserve fields without a control yet (amplitude, auto-connect, explicit
     * helper/espeak paths). */
    Settings settings = context_ ? context_->settings() : Settings{};
    settings.provider = providerCombo_->currentData().toString();
    settings.apiKey = apiKeyEdit_->text();
    settings.videoUrl = urlEdit_->text().trimmed();
    settings.voice = voiceEdit_->text();
    settings.rate = rateSpin_->value();
    settings.pitch = pitchSpin_->value();
    settings.volume = volumeSpin_->value();
    settings.maxChars = maxCharsSpin_->value();
    settings.userCooldownSec = userCooldownSpin_->value();
    settings.globalCooldownSec = globalCooldownSpin_->value();
    settings.queueLimit = queueLimitSpin_->value();
    settings.commandPrefix = commandPrefixEdit_->text();
    settings.skipCommands = skipCommandsCheck_->isChecked();
    settings.stripUrls = stripUrlsCheck_->isChecked();
    settings.skipBlocked = skipBlockedCheck_->isChecked();
    settings.blocklist = blocklistEdit_->toPlainText();
    settings.readAuthor = readAuthorCheck_->isChecked();
    settings.authorTemplate = authorTemplateEdit_->text();
    settings.speakSuperchats = speakSuperchatsCheck_->isChecked();
    settings.speakMemberships = speakMembershipsCheck_->isChecked();
    settings.readModerators = readModeratorsCheck_->isChecked();
    return settings;
}

void YtChatTtsDock::scheduleSave()
{
    if (!loading_)
        saveTimer_->start();
}

void YtChatTtsDock::saveSettings()
{
    if (!context_)
        return;

    context_->settings() = settingsFromControls();
    context_->saveSettings();
}

void YtChatTtsDock::onSaveClicked()
{
    saveTimer_->stop();
    saveSettings();
    appendLog(QStringLiteral("settings saved"));
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
        const QString message = QStringLiteral("Enter a YouTube video URL or ID");
        appendLog(QStringLiteral("error: a video URL or ID is required"));
        statusLabel_->setText(message);
        QMessageBox::warning(this, QStringLiteral("Connection"), message);
        return;
    }

    context_->saveSettings();

    QString error;
    if (!context_->connectChat(&error)) {
        appendLog(QStringLiteral("error: ") + error);
        statusLabel_->setText(QStringLiteral("Not connected"));
        setConnected(false);
        QMessageBox::critical(this, QStringLiteral("Connection failed"), error);
        return;
    }

    setConnected(true);
    statusLabel_->setText(QStringLiteral("Connecting…"));

    if (!context_->hasAudioSource()) {
        const QString warning =
            QStringLiteral("No TTS audio source is added; chat is read but audio will be silent");
        appendLog(QStringLiteral("warning: ") + warning);
        sourceStatusLabel_->setText(QStringLiteral("TTS source: not added (audio will be silent)"));
    }
}

void YtChatTtsDock::onTestVoiceClicked()
{
    if (!context_)
        return;

    saveTimer_->stop();
    context_->settings() = settingsFromControls();
    context_->saveSettings();
    context_->testVoice();

    if (!context_->hasAudioSource()) {
        const QString warning =
            QStringLiteral("No TTS audio source is added; test audio is buffered but silent");
        appendLog(QStringLiteral("warning: ") + warning);
        sourceStatusLabel_->setText(QStringLiteral("TTS source: not added (audio will be silent)"));
    }
}

void YtChatTtsDock::onSkipClicked()
{
    if (!context_)
        return;

    context_->skipCurrent();
    appendLog(QStringLiteral("tts: skipped queued messages (current message may finish)"));
}

void YtChatTtsDock::onClearQueueClicked()
{
    if (!context_)
        return;

    context_->clearQueue();
    appendLog(QStringLiteral("tts: queue cleared"));
}

void YtChatTtsDock::onAddSourceClicked(bool allScenes)
{
    /* Source messages belong on the source status label so they never replace
     * the chat connection state. */
    if (audio_source_exists()) {
        const QString message = QStringLiteral("TTS source already exists; not adding another");
        appendLog(QStringLiteral("audio: ") + message);
        sourceStatusLabel_->setText(message);
        return;
    }

    create_audio_source(allScenes);

    if (audio_source_exists()) {
        const QString message = allScenes ? QStringLiteral("TTS source added to all scenes")
                                          : QStringLiteral("TTS source added to the current scene");
        appendLog(QStringLiteral("audio: ") + message);
        sourceStatusLabel_->setText(message);
    } else {
        const QString message =
            QStringLiteral("Could not add the TTS source; open a scene and try again");
        appendLog(QStringLiteral("audio: ") + message);
        sourceStatusLabel_->setText(message);
    }
}

void YtChatTtsDock::updateSourceStatus()
{
    sourceStatusLabel_->setText(audio_source_exists()
                                    ? QStringLiteral("TTS source: added")
                                    : QStringLiteral("TTS source: not added (audio will be silent)"));
}

void YtChatTtsDock::onStatusChanged(const QString &state, const QString &detail)
{
    if (state == QLatin1String("connected")) {
        statusLabel_->setText(QStringLiteral("Connected (") + detail + QLatin1Char(')'));
        appendLog(QStringLiteral("status: connected (") + detail + QLatin1Char(')'));
        setConnected(true);
        return;
    }

    if (state == QLatin1String("ended")) {
        statusLabel_->setText(QStringLiteral("Not connected"));
        appendLog(QStringLiteral("status: helper ended"));
        setConnected(false);
        return;
    }

    if (state == QLatin1String("disconnected")) {
        statusLabel_->setText(QStringLiteral("Not connected"));
        appendLog(QStringLiteral("disconnected"));
        setConnected(false);
        return;
    }

    if (state == QLatin1String("error")) {
        statusLabel_->setText(QStringLiteral("Error: ") + detail);
        setConnected(false);
        return;
    }

    statusLabel_->setText(state + QStringLiteral("…"));
    appendLog(QStringLiteral("status[") + state + QStringLiteral("]: ") + detail);
}

void YtChatTtsDock::onLogLine(const QString &line)
{
    appendLog(line);
}

void YtChatTtsDock::onQueueChanged()
{
    if (!context_) {
        queueLabel_->setText(QStringLiteral("Queue: 0"));
        return;
    }

    queueLabel_->setText(
        QStringLiteral("Queue: %1").arg(context_->queueSnapshot(100).size()));
}

void YtChatTtsDock::setConnected(bool connected)
{
    connected_ = connected;
    urlEdit_->setEnabled(!connected);
    apiKeyEdit_->setEnabled(!connected);
    providerCombo_->setEnabled(!connected);
    connectButton_->setText(connected ? QStringLiteral("Disconnect") : QStringLiteral("Connect"));
}

void YtChatTtsDock::appendLog(const QString &line)
{
    logView_->appendPlainText(line);
}
