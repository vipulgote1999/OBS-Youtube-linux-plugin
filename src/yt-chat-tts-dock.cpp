#include "yt-chat-tts-dock.hpp"

#include "audio-source.hpp"
#include "chat-client.hpp"
#include "paths.hpp"

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

#include <QTimer>

YtChatTtsDock::YtChatTtsDock(QWidget *parent)
    : QWidget(parent), client_(new ChatClient(this)), saveTimer_(new QTimer(this)),
      settings_(Settings::load())
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

    auto *buttonRow = new QHBoxLayout;
    connectButton_ = new QPushButton(QStringLiteral("Connect"), this);
    saveButton_ = new QPushButton(QStringLiteral("Save"), this);
    buttonRow->addWidget(connectButton_);
    buttonRow->addWidget(saveButton_);
    layout->addLayout(buttonRow);

    statusLabel_ = new QLabel(QStringLiteral("Not connected"), this);
    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    logView_ = new QPlainTextEdit(this);
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(1000);
    layout->addWidget(logView_, 1);

    applySettingsToControls();
    updateSourceStatus();

    connect(connectButton_, &QPushButton::clicked, this, &YtChatTtsDock::onConnectClicked);
    connect(saveButton_, &QPushButton::clicked, this, &YtChatTtsDock::onSaveClicked);
    connect(addSourceButton_, &QPushButton::clicked, this,
            [this]() { onAddSourceClicked(false); });
    connect(addSourceAllButton_, &QPushButton::clicked, this,
            [this]() { onAddSourceClicked(true); });
    connect(client_, &ChatClient::eventReceived, this, &YtChatTtsDock::onEvent);

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
    if (saveTimer_->isActive()) {
        saveTimer_->stop();
        settings_ = settingsFromControls();
        settings_.save();
    }
}

void YtChatTtsDock::applySettingsToControls()
{
    loading_ = true;

    urlEdit_->setText(settings_.videoUrl);
    apiKeyEdit_->setText(settings_.apiKey);
    const int providerIndex = providerCombo_->findData(settings_.provider);
    providerCombo_->setCurrentIndex(providerIndex < 0 ? 0 : providerIndex);
    voiceEdit_->setText(settings_.voice);
    rateSpin_->setValue(settings_.rate);
    pitchSpin_->setValue(settings_.pitch);
    volumeSpin_->setValue(settings_.volume);
    maxCharsSpin_->setValue(settings_.maxChars);
    userCooldownSpin_->setValue(settings_.userCooldownSec);
    globalCooldownSpin_->setValue(settings_.globalCooldownSec);
    queueLimitSpin_->setValue(settings_.queueLimit);
    commandPrefixEdit_->setText(settings_.commandPrefix);
    skipCommandsCheck_->setChecked(settings_.skipCommands);
    stripUrlsCheck_->setChecked(settings_.stripUrls);
    skipBlockedCheck_->setChecked(settings_.skipBlocked);
    blocklistEdit_->setPlainText(settings_.blocklist);
    readAuthorCheck_->setChecked(settings_.readAuthor);
    authorTemplateEdit_->setText(settings_.authorTemplate);
    speakSuperchatsCheck_->setChecked(settings_.speakSuperchats);
    speakMembershipsCheck_->setChecked(settings_.speakMemberships);
    readModeratorsCheck_->setChecked(settings_.readModerators);

    loading_ = false;
}

Settings YtChatTtsDock::settingsFromControls() const
{
    /* Preserve fields without a control yet (amplitude, auto-connect, explicit
     * helper/espeak paths). */
    Settings settings = settings_;
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
    settings_ = settingsFromControls();
    if (!settings_.save())
        appendLog(QStringLiteral("settings: save failed"));
}

void YtChatTtsDock::onSaveClicked()
{
    saveTimer_->stop();
    saveSettings();
    appendLog(QStringLiteral("settings saved"));
}

void YtChatTtsDock::onAddSourceClicked(bool allScenes)
{
    if (audio_source_exists()) {
        const QString message = QStringLiteral("TTS source already exists; not adding another");
        appendLog(QStringLiteral("audio: ") + message);
        statusLabel_->setText(message);
        updateSourceStatus();
        return;
    }

    create_audio_source(allScenes);

    if (audio_source_exists()) {
        const QString message = allScenes ? QStringLiteral("TTS source added to all scenes")
                                          : QStringLiteral("TTS source added to the current scene");
        appendLog(QStringLiteral("audio: ") + message);
        statusLabel_->setText(message);
    } else {
        const QString message =
            QStringLiteral("Could not add the TTS source; open a scene and try again");
        appendLog(QStringLiteral("audio: ") + message);
        statusLabel_->setText(message);
    }

    updateSourceStatus();
}

void YtChatTtsDock::updateSourceStatus()
{
    sourceStatusLabel_->setText(audio_source_exists()
                                    ? QStringLiteral("TTS source: added")
                                    : QStringLiteral("TTS source: not added (audio will be silent)"));
}

void YtChatTtsDock::onConnectClicked()
{
    if (client_->isRunning()) {
        client_->stop();
        setConnected(false);
        statusLabel_->setText(QStringLiteral("Not connected"));
        appendLog(QStringLiteral("disconnected"));
        return;
    }

    /* Persist pending edits and connect with exactly what the controls show. */
    saveTimer_->stop();
    saveSettings();

    const QString video = settings_.videoUrl.trimmed();
    if (video.isEmpty()) {
        statusLabel_->setText(QStringLiteral("Enter a YouTube video URL or ID"));
        appendLog(QStringLiteral("error: a video URL or ID is required"));
        return;
    }

    const ChatConfig config = settings_.toChatConfig();

    appendLog(config.helperPath.isEmpty() ? QStringLiteral("helper: not found")
                                          : QStringLiteral("helper: ") + config.helperPath);

    QString error;
    if (!client_->start(config, &error)) {
        appendLog(QStringLiteral("error: ") + error);
        statusLabel_->setText(QStringLiteral("Not connected"));
        setConnected(false);
        return;
    }

    setConnected(true);
    statusLabel_->setText(QStringLiteral("Connecting…"));

    if (!audio_source_exists()) {
        appendLog(QStringLiteral(
            "warning: no TTS audio source added; chat is read but audio will be silent"));
        updateSourceStatus();
    }
}

void YtChatTtsDock::onEvent(const ChatEvent &event)
{
    switch (event.type) {
    case ChatEvent::Type::Status:
        if (event.state == QLatin1String("connected")) {
            statusLabel_->setText(QStringLiteral("Connected (") + event.detail + QLatin1Char(')'));
            appendLog(QStringLiteral("status: connected (") + event.detail + QLatin1Char(')'));
        } else if (event.state == QLatin1String("ended")) {
            statusLabel_->setText(QStringLiteral("Not connected"));
            appendLog(QStringLiteral("status: helper ended"));
            setConnected(false);
        } else if (event.state == QLatin1String("log")) {
            appendLog(event.detail);
        } else {
            statusLabel_->setText(event.state + QStringLiteral("…"));
            appendLog(QStringLiteral("status[") + event.state + QStringLiteral("]: ") + event.detail);
        }
        break;

    case ChatEvent::Type::Message: {
        QString line = event.message.author;
        if (event.message.isModerator)
            line += QStringLiteral(" [mod]");
        if (event.message.isOwner)
            line += QStringLiteral(" [owner]");
        if (event.message.isSponsor)
            line += QStringLiteral(" [member]");
        line += QStringLiteral(": ") + event.message.text;
        if (!event.message.amount.isEmpty())
            line += QStringLiteral(" (") + event.message.amount + QLatin1Char(')');
        appendLog(line);
        break;
    }

    case ChatEvent::Type::Error:
        appendLog(QStringLiteral("error [") + event.code + QStringLiteral("]: ") + event.detail);
        if (event.fatal) {
            statusLabel_->setText(QStringLiteral("Error: ") + event.code);
            setConnected(false);
        }
        break;
    }
}

void YtChatTtsDock::setConnected(bool connected)
{
    urlEdit_->setEnabled(!connected);
    apiKeyEdit_->setEnabled(!connected);
    providerCombo_->setEnabled(!connected);
    connectButton_->setText(connected ? QStringLiteral("Disconnect") : QStringLiteral("Connect"));
}

void YtChatTtsDock::appendLog(const QString &line)
{
    logView_->appendPlainText(line);
}
