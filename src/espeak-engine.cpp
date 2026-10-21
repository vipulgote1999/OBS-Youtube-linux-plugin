#include "espeak-engine.hpp"

#include <QByteArray>
#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>

#include <utility>

#include "wav.hpp"

namespace {
constexpr int kStartTimeoutMs = 5000;
constexpr int kWaitSliceMs = 50;
constexpr int kSynthesisTimeoutMs = 15000;
constexpr int kKillTimeoutMs = 1000;
constexpr int kMaxOutputBytes = 64 * 1024 * 1024;
constexpr int kMaxStderrBytes = 64 * 1024;

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

void killProcess(QProcess *process)
{
    if (process->state() == QProcess::NotRunning)
        return;
    process->kill();
    process->waitForFinished(kKillTimeoutMs);
}
} // namespace

EspeakEngine::EspeakEngine(QString binaryPath, QString dataPath, QString voice, int rate,
                           int pitch, int amplitude)
    : binaryPath_(std::move(binaryPath)),
      dataPath_(std::move(dataPath)),
      voice_(std::move(voice)),
      rate_(rate),
      pitch_(pitch),
      amplitude_(amplitude)
{
}

bool EspeakEngine::synthesize(const QString &text, std::vector<float> *pcm, int *sampleRate,
                              QString *error)
{
    if (error)
        error->clear();
    if (!pcm || !sampleRate)
        return fail(error, QStringLiteral("invalid TTS output buffer"));
    pcm->clear();
    *sampleRate = 0;

    /* The worker calls resetCancel() before synthesizing; clearing cancel_
     * here would swallow a stop() that landed after the worker's running
     * check and let this utterance run into the 15 s timeout. */

    if (binaryPath_.isEmpty())
        return fail(error, QStringLiteral("espeak-ng binary path is empty"));

    QProcess process;
    process.setProgram(binaryPath_);
    process.setArguments({QStringLiteral("--stdout"), QStringLiteral("-v"), voice_,
                          QStringLiteral("-s"), QString::number(rate_), QStringLiteral("-p"),
                          QString::number(pitch_), QStringLiteral("-a"),
                          QString::number(amplitude_)});

    if (!dataPath_.isEmpty()) {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("ESPEAK_DATA_PATH"), dataPath_);
        process.setProcessEnvironment(environment);
    }

    process.start();
    if (!process.waitForStarted(kStartTimeoutMs)) {
        return fail(error, QStringLiteral("failed to start espeak-ng: %1")
                               .arg(process.errorString()));
    }

    const QByteArray input = text.toUtf8() + '\n';
    if (process.write(input) != input.size()) {
        killProcess(&process);
        return fail(error, QStringLiteral("failed to write text to espeak-ng"));
    }
    process.closeWriteChannel();

    QByteArray output;
    QByteArray stderrOutput;
    QElapsedTimer timer;
    timer.start();

    /* Read on every slice; a WAV large enough to fill the pipe would
     * otherwise block the child and deadlock the wait. */
    while (process.state() != QProcess::NotRunning) {
        if (cancel_.load()) {
            killProcess(&process);
            return fail(error, QStringLiteral("cancelled"));
        }
        if (timer.elapsed() >= kSynthesisTimeoutMs) {
            killProcess(&process);
            return fail(error, QStringLiteral("espeak-ng timed out after 15 seconds"));
        }

        process.waitForReadyRead(kWaitSliceMs);

        const QByteArray chunk = process.readAllStandardOutput();
        if (qint64(output.size()) + chunk.size() > kMaxOutputBytes) {
            killProcess(&process);
            return fail(error, QStringLiteral("espeak-ng produced too much audio data"));
        }
        output.append(chunk);

        const QByteArray errChunk = process.readAllStandardError();
        if (stderrOutput.size() < kMaxStderrBytes) {
            stderrOutput.append(
                errChunk.left(kMaxStderrBytes - stderrOutput.size()));
        }
    }

    const QByteArray tail = process.readAllStandardOutput();
    if (qint64(output.size()) + tail.size() > kMaxOutputBytes)
        return fail(error, QStringLiteral("espeak-ng produced too much audio data"));
    output.append(tail);

    const QByteArray errTail = process.readAllStandardError();
    if (stderrOutput.size() < kMaxStderrBytes)
        stderrOutput.append(errTail.left(kMaxStderrBytes - stderrOutput.size()));

    if (cancel_.load())
        return fail(error, QStringLiteral("cancelled"));

    if (process.exitStatus() != QProcess::NormalExit) {
        return fail(error, QStringLiteral("espeak-ng crashed (exit status %1)")
                               .arg(int(process.exitStatus())));
    }
    if (process.exitCode() != 0) {
        const QString stderrText = QString::fromUtf8(stderrOutput).trimmed();
        if (!stderrText.isEmpty())
            return fail(error, stderrText);
        return fail(error, QStringLiteral("espeak-ng exited with code %1")
                               .arg(process.exitCode()));
    }

    QString parseError;
    if (!parseWav(output, pcm, sampleRate, &parseError)) {
        return fail(error, QStringLiteral("failed to parse espeak-ng output: %1")
                               .arg(parseError));
    }

    return true;
}

void EspeakEngine::cancel()
{
    cancel_ = true;
}

void EspeakEngine::resetCancel()
{
    cancel_ = false;
}
