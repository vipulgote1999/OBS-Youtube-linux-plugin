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
    QCOMPARE(actual.voice, expected.voice);
    QCOMPARE(actual.rate, expected.rate);
    QCOMPARE(actual.pitch, expected.pitch);
    QCOMPARE(actual.amplitude, expected.amplitude);
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
    QCOMPARE(actual.espeakPath, expected.espeakPath);
}

Settings changedSettings()
{
    Settings settings;
    settings.provider = QStringLiteral("innertube");
    settings.apiKey = QStringLiteral("secret-key");
    settings.videoUrl = QStringLiteral("https://youtu.be/abcdefghijk");
    settings.voice = QStringLiteral("en-gb");
    settings.rate = 200;
    settings.pitch = 60;
    settings.amplitude = 120;
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
    settings.espeakPath = QStringLiteral("/usr/bin/espeak-ng");
    return settings;
}
} // namespace

class TestSettings : public QObject {
    Q_OBJECT

private slots:
    void roundTripsThroughObsData();
    void savesWithOwnerOnlyPermissionsAndReloads();
    void loadsDefaultsForMissingFile();
    void clampsInvalidValues();
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

void TestSettings::loadsDefaultsForMissingFile()
{
    QTemporaryDir directory;
    QVERIFY2(directory.isValid(), "temporary directory is not available");

    QString error = QStringLiteral("not cleared");
    const Settings settings =
        Settings::loadFromFile(directory.filePath(QStringLiteral("absent.json")), &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(settings.provider, QStringLiteral("auto"));
    QCOMPARE(settings.voice, QStringLiteral("en-us"));
    QCOMPARE(settings.rate, 165);
    QCOMPARE(settings.pitch, 50);
    QCOMPARE(settings.amplitude, 150);
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
    obs_data_set_int(data, "rate", 5);
    obs_data_set_int(data, "pitch", -1);
    obs_data_set_int(data, "amplitude", 999);
    obs_data_set_double(data, "volume", 5.0);

    const Settings settings = Settings::fromObsData(data);
    obs_data_release(data);

    QCOMPARE(settings.maxChars, 500);
    QCOMPARE(settings.queueLimit, 1);
    QCOMPARE(settings.rate, 80);
    QCOMPARE(settings.pitch, 0);
    QCOMPARE(settings.amplitude, 200);
    QCOMPARE(settings.volume, 1.5);
}

QTEST_GUILESS_MAIN(TestSettings)

#include "test_settings.moc"
