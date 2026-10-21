#include "yt-chat-tts-dock.hpp"

#include "chat-client.hpp"
#include "paths.hpp"

#include <QtWidgets/QComboBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

YtChatTtsDock::YtChatTtsDock(QWidget *parent) : QWidget(parent), client_(new ChatClient(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto *form = new QFormLayout;

    urlEdit_ = new QLineEdit(this);
    urlEdit_->setPlaceholderText(QStringLiteral("YouTube video URL or ID"));
    form->addRow(QStringLiteral("Video"), urlEdit_);

    apiKeyEdit_ = new QLineEdit(this);
    apiKeyEdit_->setEchoMode(QLineEdit::Password);
    apiKeyEdit_->setPlaceholderText(QStringLiteral("Optional API key"));
    form->addRow(QStringLiteral("API key"), apiKeyEdit_);

    providerCombo_ = new QComboBox(this);
    providerCombo_->addItem(QStringLiteral("Auto"), QStringLiteral("auto"));
    providerCombo_->addItem(QStringLiteral("Official API"), QStringLiteral("official"));
    providerCombo_->addItem(QStringLiteral("No-key (experimental)"), QStringLiteral("innertube"));
    form->addRow(QStringLiteral("Provider"), providerCombo_);

    layout->addLayout(form);

    connectButton_ = new QPushButton(QStringLiteral("Connect"), this);
    layout->addWidget(connectButton_);

    statusLabel_ = new QLabel(QStringLiteral("Not connected"), this);
    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    logView_ = new QPlainTextEdit(this);
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(1000);
    layout->addWidget(logView_, 1);

    connect(connectButton_, &QPushButton::clicked, this, &YtChatTtsDock::onConnectClicked);
    connect(client_, &ChatClient::eventReceived, this, &YtChatTtsDock::onEvent);
}

YtChatTtsDock::~YtChatTtsDock() = default;

void YtChatTtsDock::onConnectClicked()
{
    if (client_->isRunning()) {
        client_->stop();
        setConnected(false);
        statusLabel_->setText(QStringLiteral("Not connected"));
        appendLog(QStringLiteral("disconnected"));
        return;
    }

    const QString video = urlEdit_->text().trimmed();
    if (video.isEmpty()) {
        statusLabel_->setText(QStringLiteral("Enter a YouTube video URL or ID"));
        appendLog(QStringLiteral("error: a video URL or ID is required"));
        return;
    }

    ChatConfig config;
    config.helperPath = resolveBundledPath(QStringLiteral("bin/ytchat-helper"));
    config.provider = providerCombo_->currentData().toString();
    config.video = video;
    config.apiKey = apiKeyEdit_->text();
    config.hl = QStringLiteral("en");

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
