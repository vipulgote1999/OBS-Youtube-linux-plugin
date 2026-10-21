#pragma once

#include <QWidget>

#include "settings.hpp"

class PluginContext;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSlider;
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
 * Settings are applied to the live pipeline as controls move (no file write)
 * and persisted on a 500 ms debounce.  A second 500 ms timer refreshes the
 * queue list and the audio-source status so worker pops and sources added or
 * removed outside the dock do not leave stale labels.
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
    void onTestVoiceClicked();
    void onSkipClicked();
    void onClearQueueClicked();
    void onAddSourceClicked(bool allScenes);
    void onStatusChanged(const QString &state, const QString &detail);
    void onLogLine(const QString &line);
    void onQueueChanged();
    void onControlChanged();
    void maybeAutoConnect();

    void setConnected(bool connected);
    void setStatus(const QString &text, const QString &color);
    void updateSourceStatus();
    void updateValueLabels();
    void updateEngineControls();
    void logEngineSelection();
    void refreshQueue();
    void refreshStatuses();
    void appendLog(const QString &line);

    Settings settingsFromControls() const;
    void applySettingsToControls();
    bool saveSettings();
    void scheduleSave();
    void applyLiveSettingsNow();

    PluginContext *context_ = nullptr;
    QTimer *saveTimer_ = nullptr;
    QTimer *refreshTimer_ = nullptr;
    QTimer *liveApplyTimer_ = nullptr;
    bool loading_ = false;
    bool connected_ = false;
    bool engineRestartPending_ = false;

    QLineEdit *urlEdit_ = nullptr;
    QLineEdit *apiKeyEdit_ = nullptr;
    QComboBox *providerCombo_ = nullptr;
    QLabel *apiKeyLink_ = nullptr;
    QLabel *statusDot_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QPushButton *connectButton_ = nullptr;

    QComboBox *engineCombo_ = nullptr;
    QComboBox *piperVoiceCombo_ = nullptr;
    QComboBox *voiceCombo_ = nullptr;
    QSlider *rateSlider_ = nullptr;
    QSlider *pitchSlider_ = nullptr;
    QSlider *amplitudeSlider_ = nullptr;
    QSlider *volumeSlider_ = nullptr;
    QLabel *rateValueLabel_ = nullptr;
    QLabel *pitchValueLabel_ = nullptr;
    QLabel *amplitudeValueLabel_ = nullptr;
    QLabel *volumeValueLabel_ = nullptr;
    QCheckBox *readAuthorCheck_ = nullptr;
    QLineEdit *authorTemplateEdit_ = nullptr;
    QPushButton *testVoiceButton_ = nullptr;

    QSpinBox *maxCharsSpin_ = nullptr;
    QSpinBox *userCooldownSpin_ = nullptr;
    QSpinBox *globalCooldownSpin_ = nullptr;
    QSpinBox *queueLimitSpin_ = nullptr;
    QLineEdit *commandPrefixEdit_ = nullptr;
    QCheckBox *skipCommandsCheck_ = nullptr;
    QCheckBox *stripUrlsCheck_ = nullptr;
    QCheckBox *skipBlockedCheck_ = nullptr;
    QPlainTextEdit *blocklistEdit_ = nullptr;
    QCheckBox *speakSuperchatsCheck_ = nullptr;
    QCheckBox *speakMembershipsCheck_ = nullptr;
    QCheckBox *readModeratorsCheck_ = nullptr;
    QCheckBox *autoConnectCheck_ = nullptr;

    QLabel *queueLabel_ = nullptr;
    QListWidget *queueList_ = nullptr;
    QPushButton *skipButton_ = nullptr;
    QPushButton *clearQueueButton_ = nullptr;

    QPushButton *addSourceButton_ = nullptr;
    QPushButton *addSourceAllButton_ = nullptr;
    QLabel *sourceStatusLabel_ = nullptr;

    QPlainTextEdit *logView_ = nullptr;
    QPushButton *clearLogButton_ = nullptr;
};
