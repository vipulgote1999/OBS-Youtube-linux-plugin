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
    /* Supertonic is the only bundled engine; "local" selects the experimental
     * OpenAI-compatible server path.  Old "piper"/"espeak" values migrate to
     * Supertonic when settings load. */
    QString ttsEngine = QStringLiteral("supertonic");
    /* Bundled Supertonic 3 engine.  Voice is one of the ten model styles
     * (F1-F5, M1-M5); the model path is a development override for an
     * unpacked model directory. */
    QString supertonicVoice = QStringLiteral("F1");
    QString supertonicModelPath;
    /* 0.9-1.5 is the model's natural range; lower values add long pauses. */
    double supertonicSpeed = 1.0;
    int supertonicSteps = 8;
    /* Stream each synthesized sentence to the audio buffer as it arrives
     * instead of waiting for the whole utterance.  Strictly a latency
     * improvement, so it is on by default and exposed as a checkbox. */
    bool streamAudio = true;
    /* Experimental local OpenAI-compatible TTS server (Kokoro, Pocket TTS,
     * Kokoro-FastAPI, ...).  The plugin never bundles those models; the user
     * runs the server and points us at it. */
    QString localTtsUrl = QStringLiteral("http://127.0.0.1:8765");
    QString localTtsVoice = QStringLiteral("af_heart");
    QString localTtsModel = QStringLiteral("tts-1");
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
