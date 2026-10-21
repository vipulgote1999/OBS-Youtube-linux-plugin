#pragma once

#include <QString>

#include <obs-data.h>

#include "chat-message.hpp"

/**
 * Persisted plugin settings.
 *
 * Outside OBS, use saveToFile()/loadFromFile() with an explicit path; load()
 * and save() resolve the module config directory through libobs and therefore
 * only work while the plugin is loaded.
 *
 * The API key is part of the settings file (mode 0600) and the helper's stdin
 * JSON, but it is never logged or passed on a command line.
 */
struct Settings {
    QString provider = QStringLiteral("auto");
    QString apiKey;
    QString videoUrl;
    QString voice = QStringLiteral("en-us");
    QString ttsEngine = QStringLiteral("piper");
    QString piperVoice = QStringLiteral("en_US-ljspeech-medium");
    QString piperModelPath;
    QString piperConfigPath;
    int rate = 165;
    int pitch = 50;
    int amplitude = 150;
    double volume = 1.0;
    bool readAuthor = true;
    QString authorTemplate = QStringLiteral("{author} says: {message}");
    int maxChars = 200;
    int userCooldownSec = 30;
    int globalCooldownSec = 2;
    int queueLimit = 25;
    bool skipCommands = true;
    QString commandPrefix = QStringLiteral("!");
    bool stripUrls = true;
    bool skipBlocked = true;
    QString blocklist;
    bool speakSuperchats = true;
    bool speakMemberships = false;
    bool readModerators = true;
    bool autoConnect = false;
    QString helperPath;
    QString espeakPath;

    /* Loads <module config dir>/settings.json.  Missing or invalid files fall
     * back to defaults. */
    static Settings load();

    static Settings fromObsData(obs_data_t *data);
    obs_data_t *toObsData() const;

    /* Persists to <module config dir>/settings.json with owner-only mode. */
    bool save() const;

    bool saveToFile(const QString &path, QString *error) const;
    static Settings loadFromFile(const QString &path, QString *error);

    ChatConfig toChatConfig() const;
};
