#include <piper.h>

#include <cstdio>
#include <cstring>

namespace {

void printUsage(const char *program)
{
    std::fprintf(stderr,
                 "usage: %s --model <path> [--config <path>] [--espeak-data <dir>]\n",
                 program);
}

} // namespace

int main(int argc, char **argv)
{
    const char *modelPath = nullptr;
    const char *configPath = nullptr;
    const char *espeakDataPath = nullptr;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        const bool hasValue = (i + 1) < argc;

        if (std::strcmp(arg, "--model") == 0 && hasValue) {
            modelPath = argv[++i];
        } else if (std::strcmp(arg, "--config") == 0 && hasValue) {
            configPath = argv[++i];
        } else if (std::strcmp(arg, "--espeak-data") == 0 && hasValue) {
            espeakDataPath = argv[++i];
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

    piper_synthesizer *synth = piper_create(modelPath, configPath, espeakDataPath);
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
        piper_free(synth);
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
            piper_free(synth);
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

    piper_free(synth);

    std::printf("sample_rate=%d samples=%zu\n", sampleRate, totalSamples);

    if (sampleRate != 22050) {
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
}
