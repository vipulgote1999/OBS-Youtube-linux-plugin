#pragma once

#include <QWidget>

#include "chat-message.hpp"
#include "settings.hpp"

class ChatClient;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;

/**
 * YouTube Chat TTS dock widget.
 *
 * Owns the connection UI around ChatClient plus the persisted settings
 * controls.  Settings load once at construction and save on a 500 ms debounce
 * (or immediately through the Save button).  Chat messages are logged but not
 * spoken yet; the TTS queue arrives in a later task.
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
    void onSaveClicked();
    void onAddSourceClicked(bool allScenes);
    void onEvent(const ChatEvent &event);
    void setConnected(bool connected);
    void updateSourceStatus();
    void appendLog(const QString &line);

    Settings settingsFromControls() const;
    void applySettingsToControls();
    void scheduleSave();
    void saveSettings();

    ChatClient *client_ = nullptr;
    QTimer *saveTimer_ = nullptr;
    Settings settings_;
    bool loading_ = false;

    QLineEdit *urlEdit_ = nullptr;
    QLineEdit *apiKeyEdit_ = nullptr;
    QComboBox *providerCombo_ = nullptr;
    QLineEdit *voiceEdit_ = nullptr;
    QSpinBox *rateSpin_ = nullptr;
    QSpinBox *pitchSpin_ = nullptr;
    QDoubleSpinBox *volumeSpin_ = nullptr;
    QSpinBox *maxCharsSpin_ = nullptr;
    QSpinBox *userCooldownSpin_ = nullptr;
    QSpinBox *globalCooldownSpin_ = nullptr;
    QSpinBox *queueLimitSpin_ = nullptr;
    QLineEdit *commandPrefixEdit_ = nullptr;
    QCheckBox *skipCommandsCheck_ = nullptr;
    QCheckBox *stripUrlsCheck_ = nullptr;
    QCheckBox *skipBlockedCheck_ = nullptr;
    QPlainTextEdit *blocklistEdit_ = nullptr;
    QCheckBox *readAuthorCheck_ = nullptr;
    QLineEdit *authorTemplateEdit_ = nullptr;
    QCheckBox *speakSuperchatsCheck_ = nullptr;
    QCheckBox *speakMembershipsCheck_ = nullptr;
    QCheckBox *readModeratorsCheck_ = nullptr;
    QPushButton *connectButton_ = nullptr;
    QPushButton *saveButton_ = nullptr;
    QPushButton *addSourceButton_ = nullptr;
    QPushButton *addSourceAllButton_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *sourceStatusLabel_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
};
