#include "yt-chat-tts-dock.hpp"

#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>

YtChatTtsDock::YtChatTtsDock(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    statusLabel_ = new QLabel(QStringLiteral("Not connected"), this);
    statusLabel_->setAlignment(Qt::AlignCenter);

    layout->addWidget(statusLabel_);
}

YtChatTtsDock::~YtChatTtsDock() = default;
