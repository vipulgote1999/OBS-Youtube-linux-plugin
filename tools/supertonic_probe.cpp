#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "libsupertonic-api.hpp"
#include "supertonic-engine.hpp"

namespace {

constexpr int kWorkerSampleRate = 22050;

/* Same deterministic phrase the Piper probe uses, so the two engines can be
 * compared on identical input. */
constexpr char kBenchText[] =
    "Live chat is busy tonight, so the plugin keeps reading every message while "
    "Piper synthesizes each sentence on the CPU and mixes the speech into OBS "
    "without blocking the interface for your live stream.";
constexpr int kBenchChars = 200;
constexpr int kBenchRuns = 3;

static_assert(sizeof(kBenchText) - 1 == kBenchChars,
              "benchmark phrase must stay exactly 200 characters");

void printUsage(const char *program)
{
    std::fprintf(stderr,
                 "usage: %s --model-dir <dir> [--voice F1] [--text <text>]\n"
                 "       [--steps 8] [--speed 1.0] [--bench] [--corpus <tsv>] [--cancel]\n",
                 program);
}

double peakRssMb()
{
    std::FILE *file = std::fopen("/proc/self/status", "r");
    if (!file)
        return 0.0;

    double kb = 0.0;
    char line[256];
    while (std::fgets(line, sizeof(line), file)) {
        if (std::strncmp(line, "VmHWM:", 6) == 0) {
            std::sscanf(line + 6, "%lf", &kb);
            break;
        }
    }
    std::fclose(file);
    return kb / 1024.0;
}

struct Result {
    bool ok = false;
    qint64 elapsedMs = 0;
    size_t samples = 0;
    int sampleRate = 0;
};

Result synthesize(SupertonicEngine *engine, const QString &text)
{
    Result result;
    std::vector<float> pcm;
    QString error;
    const auto start = std::chrono::steady_clock::now();
    const bool ok = engine->synthesize(text, &pcm, &result.sampleRate, &error);
    result.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();
    if (!ok) {
        std::fprintf(stderr, "error: Supertonic synthesis failed: %s\n",
                     error.toUtf8().constData());
        return result;
    }

    if (result.sampleRate != kWorkerSampleRate) {
        std::fprintf(stderr, "error: expected sample_rate=22050, got sample_rate=%d\n",
                     result.sampleRate);
        return result;
    }
    if (pcm.empty()) {
        std::fprintf(stderr, "error: synthesis produced no samples\n");
        return result;
    }

    result.samples = pcm.size();
    result.ok = true;
    return result;
}

QString resolveVoice(const QString &modelDir, const QString &voice)
{
    QString name = voice.trimmed();
    if (name.isEmpty())
        name = QStringLiteral("F1");
    if (!name.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
        name += QStringLiteral(".json");

    const QFileInfo direct(name);
    if (direct.isAbsolute() && direct.isFile())
        return direct.absoluteFilePath();

    return QDir(modelDir).filePath(QStringLiteral("voice_styles/") + name);
}

int runCorpus(SupertonicEngine *engine, const QString &path, int steps, double speed)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "error: could not open corpus %s\n", path.toUtf8().constData());
        return 1;
    }

    QTextStream stream(&file);
    int count = 0;
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        const QStringList fields = line.split(QLatin1Char('\t'));
        if (fields.size() < 3)
            continue;
        const QString id = fields.at(0);
        const QString text = fields.mid(2).join(QLatin1Char('\t'));
        const Result result = synthesize(engine, text);
        if (!result.ok)
            return 1;

        std::printf("corpus=%s steps=%d speed=%.2f sample_rate=%d samples=%zu "
                    "elapsed_ms=%lld chars=%d\n",
                    id.toUtf8().constData(), steps, speed, result.sampleRate,
                    result.samples, static_cast<long long>(result.elapsedMs),
                    static_cast<int>(text.size()));
        ++count;
    }

    if (count == 0) {
        std::fprintf(stderr, "error: corpus %s contained no usable rows\n",
                     path.toUtf8().constData());
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    QString modelDir;
    QString voice = QStringLiteral("F1");
    QString text = QStringLiteral("Supertonic packaging probe.");
    QString corpusPath;
    int steps = 8;
    double speed = 1.0;
    bool bench = false;
    bool cancelCheck = false;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        const bool hasValue = (i + 1) < argc;

        if (std::strcmp(arg, "--model-dir") == 0 && hasValue) {
            modelDir = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--voice") == 0 && hasValue) {
            voice = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--text") == 0 && hasValue) {
            text = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--corpus") == 0 && hasValue) {
            corpusPath = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--steps") == 0 && hasValue) {
            steps = std::atoi(argv[++i]);
            if (steps < 4 || steps > 16) {
                std::fprintf(stderr, "error: --steps must be between 4 and 16\n");
                return 2;
            }
        } else if (std::strcmp(arg, "--speed") == 0 && hasValue) {
            speed = std::atof(argv[++i]);
            if (!(speed >= 0.5 && speed <= 2.0)) {
                std::fprintf(stderr, "error: --speed must be between 0.5 and 2.0\n");
                return 2;
            }
        } else if (std::strcmp(arg, "--bench") == 0) {
            bench = true;
        } else if (std::strcmp(arg, "--cancel") == 0) {
            cancelCheck = true;
        } else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "error: unknown or incomplete argument: %s\n", arg);
            printUsage(argv[0]);
            return 2;
        }
    }

    if (modelDir.isEmpty()) {
        std::fprintf(stderr, "error: --model-dir is required\n");
        printUsage(argv[0]);
        return 2;
    }
    if (bench && !corpusPath.isEmpty()) {
        std::fprintf(stderr, "error: --bench and --corpus are mutually exclusive\n");
        return 2;
    }

    /* A cancel that lands before synthesis must abort without touching the
     * model.  The test passes a nonexistent directory on purpose. */
    if (cancelCheck) {
        const QString voicePath = resolveVoice(modelDir, voice);
        auto api = std::make_unique<LibSupertonicApi>(modelDir, voicePath, QStringLiteral("en"),
                                                      4);
        SupertonicEngine engine(std::move(api));
        engine.cancel();

        std::vector<float> pcm;
        int sampleRate = -1;
        QString error;
        if (engine.synthesize(QStringLiteral("Cancellation probe."), &pcm, &sampleRate, &error)) {
            std::fprintf(stderr, "error: cancelled synthesis unexpectedly succeeded\n");
            return 1;
        }
        if (!error.contains(QStringLiteral("cancelled"))) {
            std::fprintf(stderr, "error: expected a 'cancelled' error, got: %s\n",
                         error.toUtf8().constData());
            return 1;
        }
        if (!pcm.empty() || sampleRate != 0) {
            std::fprintf(stderr, "error: cancelled synthesis produced output (%zu samples, %d Hz)\n",
                         pcm.size(), sampleRate);
            return 1;
        }
        std::printf("cancel=ok error=%s\n", error.toUtf8().constData());
        return 0;
    }

    const QString voicePath = resolveVoice(modelDir, voice);
    if (!QFileInfo::exists(voicePath)) {
        std::fprintf(stderr, "error: voice style is missing: %s\n", voicePath.toUtf8().constData());
        return 1;
    }

    auto api = std::make_unique<LibSupertonicApi>(modelDir, voicePath, QStringLiteral("en"), 4);
    SupertonicEngine engine(std::move(api));
    engine.setSteps(steps);
    engine.setSpeed(static_cast<float>(speed));

    std::printf("model_dir=%s voice=%s steps=%d speed=%.2f\n",
                modelDir.toUtf8().constData(), QFileInfo(voicePath).fileName().toUtf8().constData(),
                steps, speed);

    if (!corpusPath.isEmpty())
        return runCorpus(&engine, corpusPath, steps, speed);

    /* The first synthesis loads the model, so it is reported separately from
     * the steady-state benchmark runs. */
    const Result first = synthesize(&engine, text);
    if (!first.ok)
        return 1;
    std::printf("synthesis=1 sample_rate=%d samples=%zu elapsed_ms=%lld chars=%d\n",
                first.sampleRate, first.samples, static_cast<long long>(first.elapsedMs),
                static_cast<int>(text.size()));

    if (bench) {
        const QString benchText = QString::fromUtf8(kBenchText);
        if (benchText.size() != kBenchChars) {
            std::fprintf(stderr, "error: benchmark phrase is %d characters, expected %d\n",
                         static_cast<int>(benchText.size()), kBenchChars);
            return 1;
        }

        /* bench=0 is warm-up; the three measured runs follow. */
        for (int run = 0; run <= kBenchRuns; ++run) {
            const Result result = synthesize(&engine, benchText);
            if (!result.ok)
                return 1;
            std::printf("bench=%d sample_rate=%d samples=%zu elapsed_ms=%lld chars=%d\n", run,
                        result.sampleRate, result.samples,
                        static_cast<long long>(result.elapsedMs), kBenchChars);
        }
    }

    std::printf("peak_rss_mb=%.1f\n", peakRssMb());
    return 0;
}
