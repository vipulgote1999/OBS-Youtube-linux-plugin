#pragma once

#include <QWidget>

class QLabel;

/**
 * YouTube Chat TTS dock widget.
 *
 * Task 1 stub: shows a connection status placeholder. Later tasks extend
 * this class with the connection UI, TTS controls, queue, and filter views.
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
    QLabel *statusLabel_ = nullptr;
};
