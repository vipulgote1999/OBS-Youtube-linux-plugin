#include <QtTest/QtTest>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <obs-module.h>

#include <sys/stat.h>

#include "settings.hpp"

/* The settings test executable links src/paths.cpp for toChatConfig(); libobs
 * expects each module to supply this pointer.  Outside OBS it is null, which
 * makes resolveBundledPath() skip obs_module_file(). */
extern "C" obs_module_t *obs_current_module(void)
{
    return nullptr;
}

namespace {
void compareSettings(const Settings &actual, const Settings &expected)
{
    QCOMPARE(actual.provider, expected.provider);
    QCOMPARE(actual.apiKey, expected.apiKey);
    QCOMPARE(actual.videoUrl, expected.videoUrl);
    QCOMPARE(actual.ttsEngine, expected.ttsEngine);
    QCOMPARE(actual.supertonicVoice, expected.supertonicVoice);
    QCOMPARE(actual.supertonicModelPath, expected.supertonicModelPath);
    QCOMPARE(actual.supertonicSpeed, expected.supertonicSpeed);
    QCOMPARE(actual.supertonicSteps, expected.supertonicSteps);
    QCOMPARE(actual.streamAudio, expected.streamAudio);
    QCOMPARE(actual.localTtsUrl, expected.localTtsUrl);
    QCOMPARE(actual.localTtsVoice, expected.localTtsVoice);
    QCOMPARE(actual.localTtsModel, expected.localTtsModel);
    QCOMPARE(actual.volume, expected.volume);
    QCOMPARE(actual.readAuthor, expected.readAuthor);
    QCOMPARE(actual.authorTemplate, expected.authorTemplate);
    QCOMPARE(actual.maxChars, expected.maxChars);
    QCOMPARE(actual.userCooldownSec, expected.userCooldownSec);
    QCOMPARE(actual.globalCooldownSec, expected.globalCooldownSec);
    QCOMPARE(actual.queueLimit, expected.queueLimit);
    QCOMPARE(actual.skipCommands, expected.skipCommands);
    QCOMPARE(actual.commandPrefix, expected.commandPrefix);
    QCOMPARE(actual.stripUrls, expected.stripUrls);
    QCOMPARE(actual.skipBlocked, expected.skipBlocked);
    QCOMPARE(actual.blocklist, expected.blocklist);
    QCOMPARE(actual.speakSuperchats, expected.speakSuperchats);
    QCOMPARE(actual.speakMemberships, expected.speakMemberships);
    QCOMPARE(actual.readModerators, expected.readModerators);
    QCOMPARE(actual.autoConnect, expected.autoConnect);
    QCOMPARE(actual.helperPath, expected.helperPath);
}

Settings changedSettings()
{
    Settings settings;
    settings.provider = QStringLiteral("innertube");
    settings.apiKey = QStringLiteral("secret-key");
    settings.videoUrl = QStringLiteral("https://youtu.be/abcdefghijk");
    settings.ttsEngine = QStringLiteral("local");
    settings.supertonicVoice = QStringLiteral("M3");
    settings.supertonicModelPath = QStringLiteral("/tmp/supertonic");
    settings.supertonicSpeed = 1.25;
    settings.supertonicSteps = 6;
    settings.streamAudio = false;
    settings.localTtsUrl = QStringLiteral("http://127.0.0.1:9999/v1/audio/speech");
    settings.localTtsVoice = QStringLiteral("am_michael");
    settings.localTtsModel = QStringLiteral("kokoro");
    settings.volume = 0.75;
    settings.readAuthor = false;
    settings.authorTemplate = QStringLiteral("{author}: {message}");
    settings.maxChars = 120;
    settings.userCooldownSec = 12;
    settings.globalCooldownSec = 3;
    settings.queueLimit = 10;
    settings.skipCommands = false;
    settings.commandPrefix = QStringLiteral("/");
    settings.stripUrls = false;
    settings.skipBlocked = false;
    settings.blocklist = QStringLiteral("spoiler\nsecret");
    settings.speakSuperchats = false;
    settings.speakMemberships = true;
    settings.readModerators = false;
    settings.autoConnect = true;
    settings.helperPath = QStringLiteral("/tmp/ytchat-helper");
    return settings;
}
} // namespace

class TestSettings : public QObject {
    Q_OBJECT

private slots:
    void roundTripsThroughObsData();
    void savesWithOwnerOnlyPermissionsAndReloads();
    void migratesRemovedEnginesToSupertonic();
    void normalizesSupertonicVoiceAndEngine();
    void clampsSupertonicControls();
    void dockFacingSettingsRoundTripThroughFile();
    void loadsDefaultsForMissingFile();
    void clampsInvalidValues();
    void acceptsCooldownBoundaries();
};

void TestSettings::roundTripsThroughObsData()
{
    const Settings original = changedSettings();

    obs_data_t *data = original.toObsData();
    QVERIFY(data != nullptr);
    const Settings restored = Settings::fromObsData(data);
    obs_data_release(data);

    compareSettings(restored, original);
}

void TestSettings::savesWithOwnerOnlyPermissionsAndReloads()
{
    QTemporaryDir directory;
    QVERIFY2(directory.isValid(), "temporary directory is not available");

    const QString path = directory.filePath(QStringLiteral("nested/settings.json"));
    const Settings original = changedSettings();

    QString error;
    QVERIFY2(original.saveToFile(path, &error), qPrintable(error));
    QVERIFY(error.isEmpty());
    QVERIFY(QFileInfo::exists(path));

    struct stat info = {};
    QCOMPARE(::stat(QFile::encodeName(path).constData(), &info), 0);
    QCOMPARE(info.st_mode & 0777, 0600u);

    error = QStringLiteral("not cleared");
    const Settings restored = Settings::loadFromFile(path, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    compareSettings(restored, original);
}

void TestSettings::migratesRemovedEnginesToSupertonic()
{
    for (const char *removed : {"piper", "espeak", "PIper", "not-an-engine"}) {
        obs_data_t *data = obs_data_create();
        obs_data_set_string(data, "ttsEngine", removed);
        const Settings migrated = Settings::fromObsData(data);
        obs_data_release(data);

        QCOMPARE(migrated.ttsEngine, QStringLiteral("supertonic"));
    }

    obs_data_t *localData = obs_data_create();
    obs_data_set_string(localData, "ttsEngine", "local");
    const Settings local = Settings::fromObsData(localData);
    obs_data_release(localData);

    QCOMPARE(local.ttsEngine, QStringLiteral("local"));
}

void TestSettings::normalizesSupertonicVoiceAndEngine()
{
    obs_data_t *data = obs_data_create();
    obs_data_set_string(data, "ttsEngine", "SuperTonic");
    obs_data_set_string(data, "supertonicVoice", "M9");
    const Settings normalized = Settings::fromObsData(data);
    obs_data_release(data);

    QCOMPARE(normalized.ttsEngine, QStringLiteral("supertonic"));
    QCOMPARE(normalized.supertonicVoice, QStringLiteral("F1"));

    obs_data_t *voiceData = obs_data_create();
    obs_data_set_string(voiceData, "ttsEngine", "supertonic");
    obs_data_set_string(voiceData, "supertonicVoice", "f5");
    const Settings lowerCaseVoice = Settings::fromObsData(voiceData);
    obs_data_release(voiceData);

    QCOMPARE(lowerCaseVoice.supertonicVoice, QStringLiteral("F5"));
}

void TestSettings::clampsSupertonicControls()
{
    obs_data_t *data = obs_data_create();
    obs_data_set_double(data, "supertonicSpeed", 5.0);
    obs_data_set_int(data, "supertonicSteps", 99);
    const Settings tooHigh = Settings::fromObsData(data);
    obs_data_release(data);

    QCOMPARE(tooHigh.supertonicSpeed, 1.5);
    QCOMPARE(tooHigh.supertonicSteps, 16);

    data = obs_data_create();
    obs_data_set_double(data, "supertonicSpeed", 0.5);
    obs_data_set_int(data, "supertonicSteps", 4);
    const Settings belowNatural = Settings::fromObsData(data);
    obs_data_release(data);

    QCOMPARE(belowNatural.supertonicSpeed, 0.9);

    data = obs_data_create();
    obs_data_set_double(data, "supertonicSpeed", 0.0);
    obs_data_set_int(data, "supertonicSteps", 1);
    const Settings tooLow = Settings::fromObsData(data);
    obs_data_release(data);

    QCOMPARE(tooLow.supertonicSpeed, 1.0);
    QCOMPARE(tooLow.supertonicSteps, 4);
}

void TestSettings::dockFacingSettingsRoundTripThroughFile()
{
    /* Mirrors the dock path: every control is copied into Settings, written
     * with Settings::saveToFile(), and reloaded from disk.  Keeping this
     * explicit (rather than reusing changedSettings()) documents the complete
     * UI-facing surface and fails if a control loses its persisted field. */
    QTemporaryDir directory;
    QVERIFY2(directory.isValid(), "temporary directory is not available");
    const QString path = directory.filePath(QStringLiteral("settings.json"));

    Settings original;
    original.provider = QStringLiteral("official");
    original.apiKey = QStringLiteral("AIza-dock-test-key");
    original.videoUrl = QStringLiteral("https://www.youtube.com/watch?v=dQw4w9WgXcQ");
    original.ttsEngine = QStringLiteral("supertonic");
    original.supertonicVoice = QStringLiteral("F2");
    original.supertonicModelPath = QStringLiteral("/tmp/supertonic");
    original.supertonicSpeed = 1.25;
    original.supertonicSteps = 10;
    original.streamAudio = false;
    original.localTtsUrl = QStringLiteral("http://127.0.0.1:8880/v1/audio/speech");
    original.localTtsVoice = QStringLiteral("af_heart");
    original.localTtsModel = QStringLiteral("kokoro");
    original.volume = 0.35;
    original.readAuthor = false;
    original.authorTemplate = QStringLiteral("{author} says: {message}");
    original.maxChars = 90;
    original.userCooldownSec = 15;
    original.globalCooldownSec = 7;
    original.queueLimit = 40;
    original.skipCommands = false;
    original.commandPrefix = QStringLiteral("$");
    original.skipBlocked = false;
    original.stripUrls = false;
    original.blocklist = QStringLiteral("spoiler\nsecret phrase");
    original.speakSuperchats = false;
    original.speakMemberships = true;
    original.readModerators = false;
    original.autoConnect = true;

    QString error;
    QVERIFY2(original.saveToFile(path, &error), qPrintable(error));
    const Settings restored = Settings::loadFromFile(path, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    compareSettings(restored, original);
}

void TestSettings::loadsDefaultsForMissingFile()
{
    QTemporaryDir directory;
    QVERIFY2(directory.isValid(), "temporary directory is not available");

    QString error = QStringLiteral("not cleared");
    const Settings settings =
        Settings::loadFromFile(directory.filePath(QStringLiteral("absent.json")), &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(settings.provider, QStringLiteral("auto"));
    QCOMPARE(settings.ttsEngine, QStringLiteral("supertonic"));
    QCOMPARE(settings.supertonicVoice, QStringLiteral("F1"));
    QVERIFY(settings.supertonicModelPath.isEmpty());
    QCOMPARE(settings.supertonicSpeed, 1.0);
    QCOMPARE(settings.supertonicSteps, 8);
    QCOMPARE(settings.volume, 1.0);
    QCOMPARE(settings.maxChars, 200);
    QCOMPARE(settings.userCooldownSec, 30);
    QCOMPARE(settings.globalCooldownSec, 2);
    QCOMPARE(settings.queueLimit, 25);
    QCOMPARE(settings.commandPrefix, QStringLiteral("!"));
    QVERIFY(settings.readAuthor);
    QVERIFY(settings.skipCommands);
    QVERIFY(settings.stripUrls);
    QVERIFY(settings.skipBlocked);
    QVERIFY(settings.speakSuperchats);
    QVERIFY(!settings.speakMemberships);
    QVERIFY(settings.readModerators);
    QVERIFY(!settings.autoConnect);
    QVERIFY(settings.apiKey.isEmpty());
    QVERIFY(settings.helperPath.isEmpty());
}

void TestSettings::clampsInvalidValues()
{
    obs_data_t *data = obs_data_create();
    obs_data_set_int(data, "maxChars", 9999);
    obs_data_set_int(data, "queueLimit", 0);
    obs_data_set_double(data, "volume", 5.0);
    obs_data_set_int(data, "userCooldownSec", -5);
    obs_data_set_int(data, "globalCooldownSec", 9999);

    const Settings settings = Settings::fromObsData(data);
    obs_data_release(data);

    QCOMPARE(settings.maxChars, 500);
    QCOMPARE(settings.queueLimit, 1);
    QCOMPARE(settings.volume, 1.5);
    QCOMPARE(settings.userCooldownSec, 0);
    QCOMPARE(settings.globalCooldownSec, 60);
}

void TestSettings::acceptsCooldownBoundaries()
{
    obs_data_t *data = obs_data_create();
    obs_data_set_int(data, "userCooldownSec", 3600);
    obs_data_set_int(data, "globalCooldownSec", 0);

    const Settings settings = Settings::fromObsData(data);
    obs_data_release(data);

    QCOMPARE(settings.userCooldownSec, 3600);
    QCOMPARE(settings.globalCooldownSec, 0);
}

QTEST_GUILESS_MAIN(TestSettings)

#include "test_settings.moc"
