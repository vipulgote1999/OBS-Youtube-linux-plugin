#include <QtTest/QtTest>

#include <obs-module.h>

#include "settings.hpp"

/* settings.cpp links paths.cpp for toChatConfig(); libobs expects each module
 * to supply this pointer.  Outside OBS it is null, which makes
 * resolveBundledPath() skip obs_module_file(). */
extern "C" obs_module_t *obs_current_module(void)
{
    return nullptr;
}

class TestTtsBackend : public QObject {
    Q_OBJECT

private slots:
    void missingPiperModelSelectsEspeak();
    void explicitEspeakNeverSelectsPiper();
    void explicitLocalSelectsLocal();
    void piperWithModelSelectsPiper();
    void unrecognisedEngineBehavesLikePiper();
};

void TestTtsBackend::missingPiperModelSelectsEspeak()
{
    Settings settings;
    settings.ttsEngine = QStringLiteral("piper");

    QCOMPARE(chooseTtsBackend(settings, false), TtsBackend::Espeak);
}

void TestTtsBackend::explicitEspeakNeverSelectsPiper()
{
    Settings settings;
    settings.ttsEngine = QStringLiteral("espeak");

    QCOMPARE(chooseTtsBackend(settings, true), TtsBackend::Espeak);
    QCOMPARE(chooseTtsBackend(settings, false), TtsBackend::Espeak);
}

void TestTtsBackend::explicitLocalSelectsLocal()
{
    Settings settings;
    settings.ttsEngine = QStringLiteral("local");
    QCOMPARE(chooseTtsBackend(settings, true), TtsBackend::Local);
    QCOMPARE(chooseTtsBackend(settings, false), TtsBackend::Local);
}

void TestTtsBackend::piperWithModelSelectsPiper()
{
    Settings settings;
    settings.ttsEngine = QStringLiteral("piper");
    settings.piperVoice = QStringLiteral("en_US-ljspeech-medium");

    QCOMPARE(chooseTtsBackend(settings, true), TtsBackend::Piper);
}

void TestTtsBackend::unrecognisedEngineBehavesLikePiper()
{
    /* Settings::fromObsData() normalizes unknown engines to "piper"; the
     * selection helper must not silently treat an unnormalized value as an
     * explicit espeak request. */
    Settings settings;
    settings.ttsEngine = QStringLiteral("PIper");

    QCOMPARE(chooseTtsBackend(settings, true), TtsBackend::Piper);
    QCOMPARE(chooseTtsBackend(settings, false), TtsBackend::Espeak);
}

QTEST_GUILESS_MAIN(TestTtsBackend)

#include "test_tts_backend.moc"
