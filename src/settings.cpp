#include "settings.hpp"

#include "paths.hpp"

#include <obs-module.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace {
constexpr int kMaxCharsMin = 1;
constexpr int kMaxCharsMax = 500;
constexpr int kQueueLimitMin = 1;
constexpr int kQueueLimitMax = 100;
constexpr int kRateMin = 80;
constexpr int kRateMax = 450;
constexpr int kPitchMin = 0;
constexpr int kPitchMax = 200;
constexpr double kVolumeMin = 0.0;
constexpr double kVolumeMax = 1.5;
constexpr int kUserCooldownMin = 0;
constexpr int kUserCooldownMax = 3600;
constexpr int kGlobalCooldownMin = 0;
constexpr int kGlobalCooldownMax = 60;

void clampValues(Settings *settings)
{
    settings->maxChars = qBound(kMaxCharsMin, settings->maxChars, kMaxCharsMax);
    settings->queueLimit = qBound(kQueueLimitMin, settings->queueLimit, kQueueLimitMax);
    settings->rate = qBound(kRateMin, settings->rate, kRateMax);
    settings->pitch = qBound(kPitchMin, settings->pitch, kPitchMax);
    settings->amplitude = qBound(kPitchMin, settings->amplitude, kPitchMax);
    settings->volume = qBound(kVolumeMin, settings->volume, kVolumeMax);
    settings->piperSpeed = qBound(0.5, settings->piperSpeed, 2.0);
    settings->userCooldownSec =
        qBound(kUserCooldownMin, settings->userCooldownSec, kUserCooldownMax);
    settings->globalCooldownSec =
        qBound(kGlobalCooldownMin, settings->globalCooldownSec, kGlobalCooldownMax);

    if (settings->provider != QLatin1String("auto") &&
        settings->provider != QLatin1String("official") &&
        settings->provider != QLatin1String("innertube")) {
        settings->provider = QStringLiteral("auto");
    }

    /* Existing settings files predate ttsEngine; an absent or unrecognised
     * value falls back to Piper, while an explicit espeak choice is kept. */
    const QString engine = settings->ttsEngine.trimmed().toLower();
    settings->ttsEngine =
        engine == QLatin1String("espeak") ? QStringLiteral("espeak") : QStringLiteral("piper");

    settings->piperVoice = settings->piperVoice.trimmed();
    if (settings->piperVoice.isEmpty())
        settings->piperVoice = QStringLiteral("en_US-ljspeech-medium");
}

QString configFilePath()
{
    char *path = obs_module_config_path("settings.json");
    if (!path)
        return {};
    const QString result = QString::fromUtf8(path);
    bfree(path);
    return result;
}

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}
} // namespace

Settings Settings::load()
{
    const QString path = configFilePath();
    if (path.isEmpty())
        return {};

    QString error;
    const Settings settings = loadFromFile(path, &error);
    if (!error.isEmpty())
        blog(LOG_WARNING, "[yt-chat-tts] could not load settings: %s",
             error.toUtf8().constData());
    return settings;
}

Settings Settings::fromObsData(obs_data_t *data)
{
    Settings settings;
    if (!data)
        return settings;

    const auto readString = [data](const char *key, const QString &fallback) {
        if (!obs_data_has_user_value(data, key))
            return fallback;
        return QString::fromUtf8(obs_data_get_string(data, key));
    };
    const auto readInt = [data](const char *key, int fallback) {
        return obs_data_has_user_value(data, key) ? int(obs_data_get_int(data, key)) : fallback;
    };
    const auto readDouble = [data](const char *key, double fallback) {
        return obs_data_has_user_value(data, key) ? obs_data_get_double(data, key) : fallback;
    };
    const auto readBool = [data](const char *key, bool fallback) {
        return obs_data_has_user_value(data, key) ? obs_data_get_bool(data, key) : fallback;
    };

    settings.provider = readString("provider", settings.provider);
    settings.apiKey = readString("apiKey", settings.apiKey);
    settings.videoUrl = readString("videoUrl", settings.videoUrl);
    settings.voice = readString("voice", settings.voice);
    settings.ttsEngine = readString("ttsEngine", settings.ttsEngine);
    settings.piperVoice = readString("piperVoice", settings.piperVoice);
    settings.piperModelPath = readString("piperModelPath", settings.piperModelPath);
    settings.piperConfigPath = readString("piperConfigPath", settings.piperConfigPath);
    settings.piperSpeed = readDouble("piperSpeed", settings.piperSpeed);
    settings.rate = readInt("rate", settings.rate);
    settings.pitch = readInt("pitch", settings.pitch);
    settings.amplitude = readInt("amplitude", settings.amplitude);
    settings.volume = readDouble("volume", settings.volume);
    settings.readAuthor = readBool("readAuthor", settings.readAuthor);
    settings.authorTemplate = readString("authorTemplate", settings.authorTemplate);
    settings.maxChars = readInt("maxChars", settings.maxChars);
    settings.userCooldownSec = readInt("userCooldownSec", settings.userCooldownSec);
    settings.globalCooldownSec = readInt("globalCooldownSec", settings.globalCooldownSec);
    settings.queueLimit = readInt("queueLimit", settings.queueLimit);
    settings.skipCommands = readBool("skipCommands", settings.skipCommands);
    settings.commandPrefix = readString("commandPrefix", settings.commandPrefix);
    settings.stripUrls = readBool("stripUrls", settings.stripUrls);
    settings.skipBlocked = readBool("skipBlocked", settings.skipBlocked);
    settings.blocklist = readString("blocklist", settings.blocklist);
    settings.speakSuperchats = readBool("speakSuperchats", settings.speakSuperchats);
    settings.speakMemberships = readBool("speakMemberships", settings.speakMemberships);
    settings.readModerators = readBool("readModerators", settings.readModerators);
    settings.autoConnect = readBool("autoConnect", settings.autoConnect);
    settings.helperPath = readString("helperPath", settings.helperPath);
    settings.espeakPath = readString("espeakPath", settings.espeakPath);

    clampValues(&settings);
    return settings;
}

obs_data_t *Settings::toObsData() const
{
    obs_data_t *data = obs_data_create();
    obs_data_set_string(data, "provider", provider.toUtf8().constData());
    obs_data_set_string(data, "apiKey", apiKey.toUtf8().constData());
    obs_data_set_string(data, "videoUrl", videoUrl.toUtf8().constData());
    obs_data_set_string(data, "voice", voice.toUtf8().constData());
    obs_data_set_string(data, "ttsEngine", ttsEngine.toUtf8().constData());
    obs_data_set_string(data, "piperVoice", piperVoice.toUtf8().constData());
    obs_data_set_string(data, "piperModelPath", piperModelPath.toUtf8().constData());
    obs_data_set_string(data, "piperConfigPath", piperConfigPath.toUtf8().constData());
    obs_data_set_double(data, "piperSpeed", piperSpeed);
    obs_data_set_int(data, "rate", rate);
    obs_data_set_int(data, "pitch", pitch);
    obs_data_set_int(data, "amplitude", amplitude);
    obs_data_set_double(data, "volume", volume);
    obs_data_set_bool(data, "readAuthor", readAuthor);
    obs_data_set_string(data, "authorTemplate", authorTemplate.toUtf8().constData());
    obs_data_set_int(data, "maxChars", maxChars);
    obs_data_set_int(data, "userCooldownSec", userCooldownSec);
    obs_data_set_int(data, "globalCooldownSec", globalCooldownSec);
    obs_data_set_int(data, "queueLimit", queueLimit);
    obs_data_set_bool(data, "skipCommands", skipCommands);
    obs_data_set_string(data, "commandPrefix", commandPrefix.toUtf8().constData());
    obs_data_set_bool(data, "stripUrls", stripUrls);
    obs_data_set_bool(data, "skipBlocked", skipBlocked);
    obs_data_set_string(data, "blocklist", blocklist.toUtf8().constData());
    obs_data_set_bool(data, "speakSuperchats", speakSuperchats);
    obs_data_set_bool(data, "speakMemberships", speakMemberships);
    obs_data_set_bool(data, "readModerators", readModerators);
    obs_data_set_bool(data, "autoConnect", autoConnect);
    obs_data_set_string(data, "helperPath", helperPath.toUtf8().constData());
    obs_data_set_string(data, "espeakPath", espeakPath.toUtf8().constData());
    return data;
}

bool Settings::save() const
{
    const QString path = configFilePath();
    if (path.isEmpty()) {
        blog(LOG_WARNING, "[yt-chat-tts] settings path is unavailable");
        return false;
    }

    QString error;
    if (!saveToFile(path, &error)) {
        blog(LOG_WARNING, "[yt-chat-tts] could not save settings: %s",
             error.toUtf8().constData());
        return false;
    }
    return true;
}

bool Settings::saveToFile(const QString &path, QString *error) const
{
    if (path.isEmpty()) {
        setError(error, QStringLiteral("settings path is empty"));
        return false;
    }

    Settings validated = *this;
    clampValues(&validated);

    obs_data_t *data = validated.toObsData();
    const char *json = obs_data_get_json(data);
    if (!json) {
        obs_data_release(data);
        setError(error, QStringLiteral("could not serialize settings"));
        return false;
    }
    const QByteArray payload(json);
    obs_data_release(data);

    const QFileInfo info(path);
    const QString directory = info.absolutePath();
    if (!QDir().mkpath(directory)) {
        setError(error, QStringLiteral("could not create settings directory: ") + directory);
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("could not open settings file: ") + file.errorString());
        return false;
    }
    if (file.write(payload) != payload.size()) {
        setError(error, QStringLiteral("could not write settings file: ") + file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        setError(error, QStringLiteral("could not commit settings file: ") + file.errorString());
        return false;
    }

    /* QSaveFile's temporary file is already owner-only; make the final mode
     * explicit in case the temporary inherited a wider umask. */
    if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        setError(error, QStringLiteral("could not set owner-only permissions on ") + path);
        return false;
    }

    if (error)
        error->clear();
    return true;
}

Settings Settings::loadFromFile(const QString &path, QString *error)
{
    if (error)
        error->clear();

    Settings settings;
    if (path.isEmpty())
        return settings;

    QFile file(path);
    if (!file.exists())
        return settings;

    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("could not open settings file: ") + file.errorString());
        return settings;
    }

    const QByteArray payload = file.readAll();
    const char *json = payload.constData();
    obs_data_t *data = obs_data_create_from_json(json);
    if (!data) {
        setError(error, QStringLiteral("settings file is not valid JSON"));
        return settings;
    }

    settings = fromObsData(data);
    obs_data_release(data);
    return settings;
}

TtsBackend chooseTtsBackend(const Settings &settings, bool piperModelPresent)
{
    if (settings.ttsEngine.trimmed().compare(QLatin1String("espeak"), Qt::CaseInsensitive) == 0)
        return TtsBackend::Espeak;
    if (!piperModelPresent)
        return TtsBackend::Espeak;
    return TtsBackend::Piper;
}

ChatConfig Settings::toChatConfig() const
{
    ChatConfig config;
    config.helperPath =
        helperPath.isEmpty() ? resolveBundledPath(QStringLiteral("bin/ytchat-helper")) : helperPath;
    config.provider = provider.isEmpty() ? QStringLiteral("auto") : provider;
    config.apiKey = apiKey;
    config.video = videoUrl;
    config.hl = QStringLiteral("en");
    return config;
}
