#pragma once

#include <QWidget>

#include "settings.hpp"

class PluginContext;
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
 * Holds only the settings/connection UI plus a non-owning PluginContext
 * pointer.  PluginContext owns ChatClient, MessageFilter, TtsQueue, TtsWorker
 * and the current Settings; the dock edits context->settings() and asks the
 * context to save/connect.
 *
 * Settings load once at construction and save on a 500 ms debounce (or
 * immediately through Save/Connect).
 *
 * Lifetime: owned by the OBS frontend through the OBSDock wrapper created by
 * obs_frontend_add_dock_by_id(); never delete the registered widget.  The
 * context outlives this widget during plugin unload, and the destroyed()
 * connection clears context_ if the context ever goes away first.
 */
class YtChatTtsDock : public QWidget {
    Q_OBJECT

public:
    explicit YtChatTtsDock(PluginContext *context, QWidget *parent = nullptr);
    ~YtChatTtsDock() override;

private:
    void onConnectClicked();
    void onSaveClicked();
    void onTestVoiceClicked();
    void onSkipClicked();
    void onClearQueueClicked();
    void onAddSourceClicked(bool allScenes);
    void onStatusChanged(const QString &state, const QString &detail);
    void onLogLine(const QString &line);
    void onQueueChanged();
    void setConnected(bool connected);
    void updateSourceStatus();
    void appendLog(const QString &line);

    Settings settingsFromControls() const;
    void applySettingsToControls();
    void scheduleSave();
    void saveSettings();

    PluginContext *context_ = nullptr;
    QTimer *saveTimer_ = nullptr;
    bool loading_ = false;
    bool connected_ = false;

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
    QPushButton *testVoiceButton_ = nullptr;
    QPushButton *skipButton_ = nullptr;
    QPushButton *clearQueueButton_ = nullptr;
    QPushButton *addSourceButton_ = nullptr;
    QPushButton *addSourceAllButton_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *sourceStatusLabel_ = nullptr;
    QLabel *queueLabel_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
};
