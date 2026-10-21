#pragma once

#include <QWidget>

#include "chat-message.hpp"

class ChatClient;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

/**
 * YouTube Chat TTS dock widget.
 *
 * Task 6: minimal connection UI around ChatClient.  It chooses a provider,
 * starts/stops the helper, and appends every helper event to a read-only log.
 * TTS controls, queueing, and filters arrive in later tasks.
 *
 * Lifetime: owned by the OBS frontend through the OBSDock wrapper created
 * by obs_frontend_add_dock_by_id(); never delete the registered widget.
 */
class YtChatTtsDock : public QWidget {
    Q_OBJECT

public:
    explicit YtChatTtsDock(QWidget *parent = nullptr);
    ~YtChatTtsDock() override;

private:
    void onConnectClicked();
    void onEvent(const ChatEvent &event);
    void setConnected(bool connected);
    void appendLog(const QString &line);

    ChatClient *client_ = nullptr;
    QLineEdit *urlEdit_ = nullptr;
    QLineEdit *apiKeyEdit_ = nullptr;
    QComboBox *providerCombo_ = nullptr;
    QPushButton *connectButton_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
};
