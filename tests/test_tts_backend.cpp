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
    void explicitSupertonicSelectsSupertonic();
    void supertonicWithoutModelFallsBackToEspeak();
    void supertonicWinsOverPiperWhenBothArePresent();
    void explicitPiperDoesNotSilentlyUseSupertonic();
};

void TestTtsBackend::missingPiperModelSelectsEspeak()
{
    Settings settings;
    settings.ttsEngine = QStringLiteral("piper");

    QCOMPARE(chooseTtsBackend(settings, false, false), TtsBackend::Espeak);
}

void TestTtsBackend::explicitEspeakNeverSelectsPiper()
{
    Settings settings;
    settings.ttsEngine = QStringLiteral("espeak");

    QCOMPARE(chooseTtsBackend(settings, true, false), TtsBackend::Espeak);
    QCOMPARE(chooseTtsBackend(settings, false, false), TtsBackend::Espeak);
}

void TestTtsBackend::explicitLocalSelectsLocal()
{
    Settings settings;
    settings.ttsEngine = QStringLiteral("local");
    QCOMPARE(chooseTtsBackend(settings, true, false), TtsBackend::Local);
    QCOMPARE(chooseTtsBackend(settings, false, false), TtsBackend::Local);
}

void TestTtsBackend::piperWithModelSelectsPiper()
{
    Settings settings;
    settings.ttsEngine = QStringLiteral("piper");
    settings.piperVoice = QStringLiteral("en_US-ljspeech-medium");

    QCOMPARE(chooseTtsBackend(settings, true, false), TtsBackend::Piper);
}

void TestTtsBackend::unrecognisedEngineBehavesLikePiper()
{
    /* Settings::fromObsData() normalizes unknown engines to "piper"; the
     * selection helper must not silently treat an unnormalized value as an
     * explicit espeak request. */
    Settings settings;
    settings.ttsEngine = QStringLiteral("PIper");

    QCOMPARE(chooseTtsBackend(settings, true, false), TtsBackend::Piper);
    QCOMPARE(chooseTtsBackend(settings, false, false), TtsBackend::Espeak);
}

void TestTtsBackend::explicitSupertonicSelectsSupertonic()
{
    Settings settings;
    settings.ttsEngine = QStringLiteral("supertonic");

    QCOMPARE(chooseTtsBackend(settings, false, true), TtsBackend::Supertonic);
    QCOMPARE(chooseTtsBackend(settings, true, true), TtsBackend::Supertonic);
}

void TestTtsBackend::supertonicWithoutModelFallsBackToEspeak()
{
    Settings settings;
    settings.ttsEngine = QStringLiteral("supertonic");

    QCOMPARE(chooseTtsBackend(settings, true, false), TtsBackend::Espeak);
}

void TestTtsBackend::supertonicWinsOverPiperWhenBothArePresent()
{
    Settings settings;
    settings.ttsEngine = QStringLiteral("SuperTonic");

    QCOMPARE(chooseTtsBackend(settings, true, true), TtsBackend::Supertonic);
}

void TestTtsBackend::explicitPiperDoesNotSilentlyUseSupertonic()
{
    /* A user who picked Piper should get espeak when Piper's model is
     * missing, not a different natural voice they did not choose. */
    Settings settings;
    settings.ttsEngine = QStringLiteral("piper");

    QCOMPARE(chooseTtsBackend(settings, false, true), TtsBackend::Espeak);
}

QTEST_GUILESS_MAIN(TestTtsBackend)

#include "test_tts_backend.moc"
