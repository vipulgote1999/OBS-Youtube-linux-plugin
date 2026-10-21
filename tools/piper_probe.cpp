#include <piper.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <vector>

#include <QString>

#include "libpiper-api.hpp"
#include "piper-engine.hpp"

namespace {

constexpr int kPiperSampleRate = 22050;

enum class ProbeEngine { CApi, Backend };

void printUsage(const char *program)
{
    std::fprintf(stderr,
                 "usage: %s --model <path> [--config <path>] [--espeak-data <dir>]\n"
                 "       [--engine c-api|backend] [--rate <obs rate>] [--cancel]\n",
                 program);
}

int runCApi(const char *modelPath, const char *configPath, const char *espeakDataPath)
{
    piper_synthesizer *synth = nullptr;
    auto freeSynth = [&synth]() {
        if (!synth)
            return;
        // Null first so a throwing piper_free cannot leave a dangling handle.
        piper_synthesizer *doomed = synth;
        synth = nullptr;
        try {
            piper_free(doomed);
        } catch (...) {
        }
    };

    // The C API is not noexcept (nlohmann/json, ONNX Runtime, allocation), so
    // report a descriptive error instead of letting an exception abort the
    // probe with SIGABRT.
    try {
        synth = piper_create(modelPath, configPath, espeakDataPath);
        if (synth == nullptr) {
            std::fprintf(stderr,
                         "error: piper_create failed (model=%s config=%s espeak-data=%s)\n",
                         modelPath, configPath ? configPath : "(default)",
                         espeakDataPath ? espeakDataPath : "(default)");
            return 1;
        }

        piper_synthesize_options options = piper_default_synthesize_options(synth);
        int rc = piper_synthesize_start(synth, "Piper packaging probe.", &options);
        if (rc != PIPER_OK) {
            std::fprintf(stderr, "error: piper_synthesize_start failed (rc=%d)\n", rc);
            freeSynth();
            return 1;
        }

        std::size_t totalSamples = 0;
        int sampleRate = 0;
        bool finished = false;

        while (!finished) {
            piper_audio_chunk chunk{};
            rc = piper_synthesize_next(synth, &chunk);
            if (rc != PIPER_OK && rc != PIPER_DONE) {
                std::fprintf(stderr, "error: piper_synthesize_next failed (rc=%d)\n", rc);
                freeSynth();
                return 1;
            }

            // The final chunk is still filled with audio when PIPER_DONE is
            // returned, so consume it before stopping.
            totalSamples += chunk.num_samples;
            if (chunk.sample_rate > 0) {
                sampleRate = chunk.sample_rate;
            }
            finished = (rc == PIPER_DONE) || chunk.is_last;
        }

        freeSynth();

        std::printf("sample_rate=%d samples=%zu\n", sampleRate, totalSamples);

        if (sampleRate != kPiperSampleRate) {
            std::fprintf(stderr,
                         "error: expected sample_rate=22050, got sample_rate=%d\n",
                         sampleRate);
            return 1;
        }
        if (totalSamples == 0) {
            std::fprintf(stderr, "error: synthesis produced no samples\n");
            return 1;
        }

        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: libpiper threw: %s\n", e.what());
        freeSynth();
        return 1;
    } catch (...) {
        std::fprintf(stderr, "error: libpiper threw an unknown exception\n");
        freeSynth();
        return 1;
    }
}

int synthesizeWithEngine(PiperEngine *engine, const QString &text, int iteration)
{
    std::vector<float> pcm;
    int sampleRate = 0;
    QString error;
    const auto start = std::chrono::steady_clock::now();
    if (!engine->synthesize(text, &pcm, &sampleRate, &error)) {
        std::fprintf(stderr, "error: PiperEngine synthesis %d failed: %s\n", iteration,
                     error.toUtf8().constData());
        return 1;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();

    std::printf("synthesis=%d sample_rate=%d samples=%zu elapsed_ms=%lld\n", iteration,
                sampleRate, pcm.size(), static_cast<long long>(elapsed));

    if (sampleRate != kPiperSampleRate) {
        std::fprintf(stderr, "error: expected sample_rate=22050, got sample_rate=%d\n",
                     sampleRate);
        return 1;
    }
    if (pcm.empty()) {
        std::fprintf(stderr, "error: synthesis %d produced no samples\n", iteration);
        return 1;
    }

    return 0;
}

int runBackend(const char *modelPath, const char *configPath, const char *espeakDataPath,
               int rate, bool cancelCheck)
{
    auto api = std::make_unique<LibPiperApi>(QString::fromUtf8(modelPath),
                                             QString::fromUtf8(configPath ? configPath : ""),
                                             QString::fromUtf8(espeakDataPath ? espeakDataPath : ""));
    PiperEngine engine(std::move(api), rate);

    if (cancelCheck) {
        // cancel() must be lock-free, must not touch libpiper, and a cancel
        // that lands before synthesis starts must abort without loading the
        // model (the caller passes a nonexistent model path on purpose).
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
            std::fprintf(stderr,
                         "error: cancelled synthesis produced output (%zu samples, %d Hz)\n",
                         pcm.size(), sampleRate);
            return 1;
        }

        std::printf("cancel=ok error=%s\n", error.toUtf8().constData());
        return 0;
    }

    // Two utterances in one process prove the synthesizer is created lazily
    // once and reused instead of loading the model for every message.
    if (synthesizeWithEngine(&engine, QStringLiteral("Piper packaging probe."), 1) != 0)
        return 1;
    if (synthesizeWithEngine(&engine, QStringLiteral("Backend reuse probe."), 2) != 0)
        return 1;

    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    const char *modelPath = nullptr;
    const char *configPath = nullptr;
    const char *espeakDataPath = nullptr;
    ProbeEngine engine = ProbeEngine::CApi;
    int rate = 165;
    bool cancelCheck = false;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        const bool hasValue = (i + 1) < argc;

        if (std::strcmp(arg, "--model") == 0 && hasValue) {
            modelPath = argv[++i];
        } else if (std::strcmp(arg, "--config") == 0 && hasValue) {
            configPath = argv[++i];
        } else if (std::strcmp(arg, "--espeak-data") == 0 && hasValue) {
            espeakDataPath = argv[++i];
        } else if (std::strcmp(arg, "--engine") == 0 && hasValue) {
            const char *value = argv[++i];
            if (std::strcmp(value, "c-api") == 0) {
                engine = ProbeEngine::CApi;
            } else if (std::strcmp(value, "backend") == 0) {
                engine = ProbeEngine::Backend;
            } else {
                std::fprintf(stderr, "error: unknown engine: %s\n", value);
                printUsage(argv[0]);
                return 2;
            }
        } else if (std::strcmp(arg, "--rate") == 0 && hasValue) {
            rate = std::atoi(argv[++i]);
            if (rate <= 0) {
                std::fprintf(stderr, "error: --rate must be positive\n");
                return 2;
            }
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

    if (modelPath == nullptr) {
        std::fprintf(stderr, "error: --model is required\n");
        printUsage(argv[0]);
        return 2;
    }

    if (cancelCheck && engine != ProbeEngine::Backend) {
        std::fprintf(stderr, "error: --cancel requires --engine backend\n");
        printUsage(argv[0]);
        return 2;
    }

    if (engine == ProbeEngine::Backend)
        return runBackend(modelPath, configPath, espeakDataPath, rate, cancelCheck);

    return runCApi(modelPath, configPath, espeakDataPath);
}
