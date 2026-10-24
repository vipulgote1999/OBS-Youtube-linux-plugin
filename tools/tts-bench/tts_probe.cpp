// Benchmark probe for the TTS engine bake-off.
//
// Runs Piper (VITS), Kokoro and Pocket TTS through the same sherpa-onnx
// runtime so the only variable is the model. Emits one JSON object per
// utterance and a human summary on stderr.
//
// This is a development tool. It is built only with -DBUILD_TTS_PROBE=ON and
// is never part of the shipped OBS plugin.

#include <sherpa-onnx/c-api/cxx-api.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <sys/resource.h>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

constexpr int kPocketSampleRate = 24000;

struct Audio {
    std::vector<float> samples;
    int sampleRate = 0;
};

struct CorpusLine {
    std::string id;
    std::string category;
    std::string text;
};

struct Options {
    std::string engine;      // pocket | kokoro | vits
    std::string modelDir;
    std::string voice;       // Pocket: wav path/name. Kokoro: speaker id.
    std::string textFile;
    std::string text;
    std::string wavDir;
    std::string label;
    int threads = 2;
    int numSteps = 1;
    int runs = 1;
    int idleSeconds = 0;
    int refMaxSeconds = 10;
    double maxAudioSeconds = 20.0;
    double maxComputeSeconds = 20.0;
    bool warmup = true;
};

struct RunStats {
    double ttfaMs = -1.0;
    double computeMs = 0.0;
    int callbackCalls = 0;
    bool aborted = false;
    std::string abortReason;
};

double msSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

double peakRssMb()
{
    struct rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        return 0.0;
    // Linux reports ru_maxrss in kilobytes.
    return static_cast<double>(usage.ru_maxrss) / 1024.0;
}

std::string jsonEscape(const std::string &in)
{
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c & 0xff);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------- WAV input

uint32_t readU32(const unsigned char *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t readU16(const unsigned char *p)
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

bool readWav(const std::string &path, Audio *out, std::string *error)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        *error = "cannot open " + path;
        return false;
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        *error = path + ": not a RIFF/WAVE file";
        return false;
    }

    uint16_t format = 0;
    uint16_t channels = 0;
    uint32_t rate = 0;
    uint16_t bits = 0;
    const unsigned char *data = nullptr;
    size_t dataSize = 0;

    size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        const unsigned char *chunk = bytes.data() + pos;
        const uint32_t size = readU32(chunk + 4);
        const size_t body = pos + 8;
        if (body + size > bytes.size())
            break;
        if (std::memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
            format = readU16(bytes.data() + body);
            channels = readU16(bytes.data() + body + 2);
            rate = readU32(bytes.data() + body + 4);
            bits = readU16(bytes.data() + body + 14);
            if (format == 0xFFFE && size >= 26) {
                // WAVE_FORMAT_EXTENSIBLE: the real format is the first two
                // bytes of the sub-format GUID.
                format = readU16(bytes.data() + body + 24);
            }
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            data = bytes.data() + body;
            dataSize = size;
        }
        pos = body + size + (size & 1u);
    }

    if (!data || channels == 0 || rate == 0) {
        *error = path + ": missing fmt/data chunk";
        return false;
    }

    const size_t frames = dataSize / (channels * (bits / 8u));
    out->sampleRate = static_cast<int>(rate);
    out->samples.assign(frames, 0.0f);

    for (size_t i = 0; i < frames; ++i) {
        double acc = 0.0;
        for (uint16_t c = 0; c < channels; ++c) {
            const size_t off = (i * channels + c) * (bits / 8u);
            if (format == 3 && bits == 32) {
                float v = 0.0f;
                std::memcpy(&v, data + off, sizeof(v));
                acc += v;
            } else if (format == 1 && bits == 16) {
                int16_t v = 0;
                std::memcpy(&v, data + off, sizeof(v));
                acc += static_cast<double>(v) / 32768.0;
            } else if (format == 1 && bits == 32) {
                int32_t v = 0;
                std::memcpy(&v, data + off, sizeof(v));
                acc += static_cast<double>(v) / 2147483648.0;
            } else {
                *error = path + ": unsupported WAV encoding (format " +
                         std::to_string(format) + ", " + std::to_string(bits) + " bit)";
                return false;
            }
        }
        out->samples[i] = static_cast<float>(acc / channels);
    }
    return true;
}

std::vector<float> resampleLinear(const std::vector<float> &in, int inRate, int outRate)
{
    if (inRate == outRate || in.empty())
        return in;
    const double ratio = static_cast<double>(outRate) / inRate;
    const size_t n = static_cast<size_t>(in.size() * ratio);
    std::vector<float> out(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        const double src = i / ratio;
        const size_t i0 = static_cast<size_t>(src);
        const size_t i1 = std::min(i0 + 1, in.size() - 1);
        const double frac = src - i0;
        out[i] = static_cast<float>(in[i0] * (1.0 - frac) + in[i1] * frac);
    }
    return out;
}

bool writeWav16(const std::string &path, const std::vector<float> &samples, int rate)
{
    std::error_code ec;
    const fs::path parent = fs::path(path).parent_path();
    if (!parent.empty())
        fs::create_directories(parent, ec);

    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;

    const uint32_t dataSize = static_cast<uint32_t>(samples.size() * 2);
    const uint32_t riffSize = 36 + dataSize;

    auto u32 = [&f](uint32_t v) { f.write(reinterpret_cast<const char *>(&v), 4); };
    auto u16 = [&f](uint16_t v) { f.write(reinterpret_cast<const char *>(&v), 2); };

    f.write("RIFF", 4);
    u32(riffSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    u32(16);
    u16(1); // PCM
    u16(1); // mono
    u32(static_cast<uint32_t>(rate));
    u32(static_cast<uint32_t>(rate * 2));
    u16(2);
    u16(16);
    f.write("data", 4);
    u32(dataSize);

    for (float s : samples) {
        const float clamped = std::max(-1.0f, std::min(1.0f, s));
        const int16_t v = static_cast<int16_t>(std::lround(clamped * 32767.0f));
        f.write(reinterpret_cast<const char *>(&v), 2);
    }
    return static_cast<bool>(f);
}

// Largest sample-to-sample jump in the first 40 ms, divided by the 99th
// percentile jump in the rest of the clip. A clean utterance sits near 1; the
// chunk-start click from pocket-tts#171 shows up as a large ratio.
double firstClickRatio(const std::vector<float> &x, int rate)
{
    const size_t window = std::min<size_t>(x.size(), static_cast<size_t>(rate * 40 / 1000));
    if (window < 8 || x.size() <= window + 8)
        return 0.0;

    double head = 0.0;
    for (size_t i = 1; i < window; ++i)
        head = std::max(head, std::fabs(static_cast<double>(x[i]) - x[i - 1]));

    std::vector<double> tail;
    tail.reserve(x.size() - window);
    for (size_t i = window; i < x.size(); ++i)
        tail.push_back(std::fabs(static_cast<double>(x[i]) - x[i - 1]));
    if (tail.empty())
        return 0.0;

    const size_t k = static_cast<size_t>(tail.size() * 0.99);
    std::nth_element(tail.begin(), tail.begin() + k, tail.end());
    const double p99 = tail[k];
    return p99 > 1e-9 ? head / p99 : head;
}

// ---------------------------------------------------------------- corpus

std::vector<CorpusLine> loadCorpus(const std::string &path, std::string *error)
{
    std::vector<CorpusLine> lines;
    std::ifstream f(path);
    if (!f) {
        *error = "cannot open corpus " + path;
        return lines;
    }

    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;

        const size_t first = line.find('\t');
        const size_t second = first == std::string::npos ? std::string::npos
                                                        : line.find('\t', first + 1);
        if (second == std::string::npos) {
            *error = "corpus line is not id<TAB>category<TAB>text: " + line;
            return {};
        }
        lines.push_back({line.substr(0, first), line.substr(first + 1, second - first - 1),
                         line.substr(second + 1)});
    }
    return lines;
}

// ---------------------------------------------------------------- model setup

std::string pick(const fs::path &dir, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        const fs::path p = dir / name;
        if (fs::exists(p))
            return p.string();
    }
    return {};
}

bool requirePath(const std::string &path, const char *what, std::string *error)
{
    if (path.empty() || !fs::exists(path)) {
        *error = std::string("missing ") + what + " (looked under the model directory)";
        return false;
    }
    return true;
}

bool configurePocket(const Options &opt, sherpa_onnx::cxx::OfflineTtsConfig *config,
                     std::string *error)
{
    const fs::path dir(opt.modelDir);
    auto &p = config->model.pocket;
    p.lm_flow = pick(dir, {"lm_flow.int8.onnx", "lm_flow.onnx", "flow_lm_flow_int8.onnx",
                           "flow_lm_flow.onnx"});
    p.lm_main = pick(dir, {"lm_main.int8.onnx", "lm_main.onnx", "flow_lm_main_int8.onnx",
                           "flow_lm_main.onnx"});
    p.encoder = pick(dir, {"encoder.onnx", "mimi_encoder.onnx"});
    p.decoder = pick(dir, {"decoder.int8.onnx", "decoder.onnx", "mimi_decoder_int8.onnx",
                           "mimi_decoder.onnx"});
    p.text_conditioner = pick(dir, {"text_conditioner.onnx"});
    p.vocab_json = pick(dir, {"vocab.json"});
    p.token_scores_json = pick(dir, {"token_scores.json"});

    const std::pair<const std::string *, const char *> required[] = {
        {&p.lm_flow, "lm_flow"}, {&p.lm_main, "lm_main"}, {&p.encoder, "encoder"},
        {&p.decoder, "decoder"}, {&p.text_conditioner, "text_conditioner"},
        {&p.vocab_json, "vocab.json"}, {&p.token_scores_json, "token_scores.json"},
    };
    for (const auto &r : required) {
        if (!requirePath(*r.first, r.second, error))
            return false;
    }
    return true;
}

bool configureKokoro(const Options &opt, sherpa_onnx::cxx::OfflineTtsConfig *config,
                     std::string *error)
{
    const fs::path dir(opt.modelDir);
    auto &k = config->model.kokoro;
    k.model = pick(dir, {"model.onnx", "model.int8.onnx", "kokoro.onnx"});
    k.voices = pick(dir, {"voices.bin", "voices.txt"});
    k.tokens = pick(dir, {"tokens.txt"});
    k.lexicon = pick(dir, {"lexicon-en-us.txt", "lexicon.txt"});
    const fs::path dataDir = dir / "espeak-ng-data";
    if (fs::is_directory(dataDir))
        k.data_dir = dataDir.string();
    if (!requirePath(k.model, "Kokoro model", error) ||
        !requirePath(k.voices, "Kokoro voices", error) ||
        !requirePath(k.tokens, "Kokoro tokens", error))
        return false;
    return true;
}

bool configureVits(const Options &opt, sherpa_onnx::cxx::OfflineTtsConfig *config,
                   std::string *error)
{
    const fs::path dir(opt.modelDir);
    auto &v = config->model.vits;
    v.model = pick(dir, {"model.onnx", "model.int8.onnx", "vits.onnx"});
    if (v.model.empty()) {
        // Piper bundles keep the voice name in the filename, e.g.
        // en_US-kristin-medium.onnx, so fall back to the only .onnx present and
        // prefer an int8 variant when the archive ships several.
        std::vector<std::string> candidates;
        std::error_code ec;
        for (const auto &entry : fs::directory_iterator(dir, ec)) {
            if (entry.path().extension() == ".onnx")
                candidates.push_back(entry.path().string());
        }
        std::sort(candidates.begin(), candidates.end(), [](const std::string &a,
                                                          const std::string &b) {
            const bool ai = a.find("int8") != std::string::npos;
            const bool bi = b.find("int8") != std::string::npos;
            if (ai != bi)
                return ai;
            return a < b;
        });
        if (!candidates.empty())
            v.model = candidates.front();
    }
    v.tokens = pick(dir, {"tokens.txt"});
    v.lexicon = pick(dir, {"lexicon.txt"});
    v.data_dir = pick(dir, {"espeak-ng-data"});
    if (!requirePath(v.model, "VITS model", error) || !requirePath(v.tokens, "VITS tokens", error))
        return false;
    if (v.data_dir.empty()) {
        // sherpa-onnx bundles piper voices with an espeak-ng-data directory.
        const fs::path nested = dir / "espeak-ng-data";
        if (!fs::is_directory(nested)) {
            *error = "missing espeak-ng-data under " + dir.string();
            return false;
        }
        v.data_dir = nested.string();
    }
    return true;
}

// ---------------------------------------------------------------- reporting

void printJson(const Options &opt, const CorpusLine &line, int run, double loadMs,
               double warmupMs, const sherpa_onnx::cxx::GeneratedAudio &audio,
               const RunStats &stats, const std::string &wavPath, double clickRatio)
{
    const double audioMs = audio.sample_rate > 0
                               ? audio.samples.size() * 1000.0 / audio.sample_rate
                               : 0.0;
    const double rtf = audioMs > 0.0 ? stats.computeMs / audioMs : 0.0;

    double sumSquares = 0.0;
    double peak = 0.0;
    for (float s : audio.samples) {
        sumSquares += static_cast<double>(s) * s;
        peak = std::max(peak, std::fabs(static_cast<double>(s)));
    }
    const double rms = audio.samples.empty()
                           ? 0.0
                           : std::sqrt(sumSquares / audio.samples.size());

    std::printf(
        "{\"engine\":\"%s\",\"voice\":\"%s\",\"label\":\"%s\",\"text_id\":\"%s\","
        "\"category\":\"%s\",\"run\":%d,\"chars\":%zu,"
        "\"load_ms\":%.1f,\"warmup_ms\":%.1f,\"ttfa_ms\":%.1f,\"compute_ms\":%.1f,"
        "\"audio_ms\":%.1f,\"rtf\":%.4f,\"x_realtime\":%.2f,"
        "\"peak_rss_mb\":%.1f,\"sample_rate\":%d,\"samples\":%zu,"
        "\"click_ratio\":%.2f,\"callback_calls\":%d,\"aborted\":%s,\"abort_reason\":\"%s\","
        "\"rms\":%.5f,\"peak\":%.4f,\"wav\":\"%s\"}\n",
        opt.engine.c_str(), jsonEscape(opt.voice).c_str(), jsonEscape(opt.label).c_str(),
        jsonEscape(line.id).c_str(), jsonEscape(line.category).c_str(), run, line.text.size(),
        loadMs, warmupMs, stats.ttfaMs, stats.computeMs, audioMs, rtf,
        rtf > 0.0 ? 1.0 / rtf : 0.0, peakRssMb(), audio.sample_rate, audio.samples.size(),
        clickRatio, stats.callbackCalls, stats.aborted ? "true" : "false",
        stats.abortReason.c_str(), rms, peak, jsonEscape(wavPath).c_str());
    std::fflush(stdout);
}

void printUsage(const char *program)
{
    std::fprintf(
        stderr,
        "usage: %s --engine pocket|kokoro|vits --model-dir DIR [options]\n"
        "  --voice NAME|PATH|SID   Pocket reference wav / Kokoro speaker id\n"
        "  --text-file FILE        TSV: id<TAB>category<TAB>text\n"
        "  --text STRING           single utterance instead of a corpus\n"
        "  --runs N                repeats per utterance (default 1)\n"
        "  --threads N             inference threads (default 2)\n"
        "  --num-steps N           Pocket flow steps (default 1)\n"
        "  --wav-dir DIR           write 16-bit wavs here\n"
        "  --label NAME            free-form label recorded in the JSON\n"
        "  --idle-seconds N        re-run the first short line after N seconds idle\n"
        "  --ref-max-seconds N     truncate the Pocket reference clip (default 10)\n"
        "  --max-audio-seconds N   abort a runaway generation above N s (default 20)\n"
        "  --max-compute-seconds N abort a generation slower than N s (default 20)\n"
        "  --no-warmup             skip the discarded warm-up utterance\n",
        program);
}

} // namespace

int main(int argc, char **argv)
{
    Options opt;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: %s needs a value\n", arg.c_str());
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--engine")
            opt.engine = value();
        else if (arg == "--model-dir")
            opt.modelDir = value();
        else if (arg == "--voice")
            opt.voice = value();
        else if (arg == "--text-file")
            opt.textFile = value();
        else if (arg == "--text")
            opt.text = value();
        else if (arg == "--wav-dir")
            opt.wavDir = value();
        else if (arg == "--label")
            opt.label = value();
        else if (arg == "--runs")
            opt.runs = std::max(1, std::atoi(value().c_str()));
        else if (arg == "--threads")
            opt.threads = std::max(1, std::atoi(value().c_str()));
        else if (arg == "--num-steps")
            opt.numSteps = std::max(1, std::atoi(value().c_str()));
        else if (arg == "--idle-seconds")
            opt.idleSeconds = std::max(0, std::atoi(value().c_str()));
        else if (arg == "--ref-max-seconds")
            opt.refMaxSeconds = std::max(1, std::atoi(value().c_str()));
        else if (arg == "--max-audio-seconds")
            opt.maxAudioSeconds = std::max(1.0, std::atof(value().c_str()));
        else if (arg == "--max-compute-seconds")
            opt.maxComputeSeconds = std::max(1.0, std::atof(value().c_str()));
        else if (arg == "--no-warmup")
            opt.warmup = false;
        else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "error: unknown argument %s\n", arg.c_str());
            printUsage(argv[0]);
            return 2;
        }
    }

    if (opt.engine.empty() || opt.modelDir.empty()) {
        std::fprintf(stderr, "error: --engine and --model-dir are required\n");
        printUsage(argv[0]);
        return 2;
    }

    std::vector<CorpusLine> corpus;
    if (!opt.textFile.empty()) {
        std::string error;
        corpus = loadCorpus(opt.textFile, &error);
        if (corpus.empty()) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 2;
        }
    } else if (!opt.text.empty()) {
        corpus.push_back({"adhoc", "adhoc", opt.text});
    } else {
        std::fprintf(stderr, "error: pass --text-file or --text\n");
        return 2;
    }

    // Reference audio is only needed by Pocket.
    Audio reference;
    if (opt.engine == "pocket") {
        if (opt.voice.empty()) {
            std::fprintf(stderr, "error: Pocket TTS requires --voice <reference.wav>\n");
            return 2;
        }
        std::string path = opt.voice;
        if (!fs::exists(path)) {
            const fs::path inBundle = fs::path(opt.modelDir) / "test_wavs" / (opt.voice + ".wav");
            if (fs::exists(inBundle))
                path = inBundle.string();
        }
        std::string error;
        if (!readWav(path, &reference, &error)) {
            std::fprintf(stderr, "error: reference audio: %s\n", error.c_str());
            return 2;
        }
        reference.samples = resampleLinear(reference.samples, reference.sampleRate,
                                           kPocketSampleRate);
        reference.sampleRate = kPocketSampleRate;
        // Voice conditioning cost scales with the prompt.  Sherpa's own Pocket
        // example caps the reference at 10 s; a 45 s clip makes every first
        // utterance slow without improving the cloned voice.
        const size_t maxSamples = static_cast<size_t>(opt.refMaxSeconds) * kPocketSampleRate;
        if (reference.samples.size() > maxSamples)
            reference.samples.resize(maxSamples);
        std::fprintf(stderr, "reference: %s (%.2f s at %d Hz)\n", path.c_str(),
                     reference.samples.size() / static_cast<double>(kPocketSampleRate),
                     kPocketSampleRate);
    }

    sherpa_onnx::cxx::OfflineTtsConfig config;
    config.model.num_threads = opt.threads;
    config.model.debug = false;

    std::string error;
    bool configured = false;
    if (opt.engine == "pocket")
        configured = configurePocket(opt, &config, &error);
    else if (opt.engine == "kokoro")
        configured = configureKokoro(opt, &config, &error);
    else if (opt.engine == "vits")
        configured = configureVits(opt, &config, &error);
    else
        error = "unknown engine " + opt.engine;

    if (!configured) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 2;
    }

    const auto loadStart = Clock::now();
    auto tts = sherpa_onnx::cxx::OfflineTts::Create(config);
    const double loadMs = msSince(loadStart);
    if (tts.Get() == nullptr) {
        std::fprintf(stderr, "error: OfflineTts::Create failed for %s\n", opt.engine.c_str());
        return 3;
    }

    sherpa_onnx::cxx::GenerationConfig gen;
    gen.speed = 1.0f;
    gen.num_steps = opt.numSteps;
    if (opt.engine == "pocket") {
        gen.reference_audio = reference.samples;
        gen.reference_sample_rate = reference.sampleRate;
        gen.extra["max_reference_audio_len"] = "10";
    } else if (opt.engine == "kokoro") {
        gen.sid = opt.voice.empty() ? 0 : std::atoi(opt.voice.c_str());
    }

    std::fprintf(stderr, "loaded %s in %.0f ms (output %d Hz, %d speakers)\n",
                 opt.engine.c_str(), loadMs, tts.SampleRate(), tts.NumSpeakers());

    // pocket-tts#91: the first utterance after model load is often garbled.
    // Discard one short generation before measuring anything.
    double warmupMs = 0.0;
    if (opt.warmup) {
        const auto start = Clock::now();
        auto audio = tts.Generate("Warm up.", gen);
        warmupMs = msSince(start);
        std::fprintf(stderr, "warm-up: %.0f ms, %zu samples\n", warmupMs,
                     audio.samples.size());
    }

    int failures = 0;
    for (const CorpusLine &line : corpus) {
        for (int run = 1; run <= opt.runs; ++run) {
            RunStats stats;
            const auto start = Clock::now();

            // The callback only needs a timestamp; computeMs is not yet known
            // when it fires, so record the absolute start time instead.
            struct Ctx {
                Clock::time_point start;
                double *ttfa;
                int *calls;
                int64_t samples;
                int rate;
                double maxAudioMs;
                double maxComputeMs;
                bool aborted;
                std::string *reason;
            } ctx{start,          &stats.ttfaMs,      &stats.callbackCalls, 0,
                  tts.SampleRate(), opt.maxAudioSeconds * 1000.0,
                  opt.maxComputeSeconds * 1000.0, false, &stats.abortReason};

            // pocket-tts#113: the model occasionally never emits EOS and
            // generates until a hard cap.  Returning 0 stops generation and
            // marks the utterance as failed instead of burning 40 s of audio.
            auto cb = [](const float *, int32_t n, float, void *arg) -> int32_t {
                auto *c = static_cast<Ctx *>(arg);
                ++(*c->calls);
                if (*c->ttfa < 0.0)
                    *c->ttfa = msSince(c->start);
                c->samples += n;
                if (c->rate > 0 && c->samples * 1000.0 / c->rate > c->maxAudioMs) {
                    c->aborted = true;
                    *c->reason = "audio";
                    return 0;
                }
                if (msSince(c->start) > c->maxComputeMs) {
                    c->aborted = true;
                    *c->reason = "compute";
                    return 0;
                }
                return 1;
            };

            auto audio = tts.Generate(line.text, gen, cb, &ctx);
            stats.computeMs = msSince(start);
            stats.aborted = ctx.aborted;
            if (stats.ttfaMs < 0.0)
                stats.ttfaMs = stats.computeMs;

            if (audio.samples.empty()) {
                std::fprintf(stderr, "error: empty audio for %s\n", line.id.c_str());
                ++failures;
                continue;
            }

            const double click = firstClickRatio(audio.samples, audio.sample_rate);
            std::string wavPath;
            if (!opt.wavDir.empty()) {
                wavPath = (fs::path(opt.wavDir) /
                           (opt.label + "__" + line.id + "__" + std::to_string(run) + ".wav"))
                              .string();
                if (!writeWav16(wavPath, audio.samples, audio.sample_rate))
                    std::fprintf(stderr, "warning: cannot write %s\n", wavPath.c_str());
            }

            printJson(opt, line, run, loadMs, warmupMs, audio, stats, wavPath, click);
        }
    }

    if (opt.idleSeconds > 0 && !corpus.empty()) {
        std::fprintf(stderr, "idling %d s, then re-running %s\n", opt.idleSeconds,
                     corpus.front().id.c_str());
        std::this_thread::sleep_for(std::chrono::seconds(opt.idleSeconds));

        CorpusLine line = corpus.front();
        line.category = "after_idle";
        line.id += "_idle";
        RunStats stats;
        const auto start = Clock::now();
        struct Ctx {
            Clock::time_point start;
            double *ttfa;
            int64_t samples;
            int rate;
            double maxAudioMs;
            bool aborted;
        } ctx{start, &stats.ttfaMs, 0, tts.SampleRate(), opt.maxAudioSeconds * 1000.0, false};
        auto cb = [](const float *, int32_t n, float, void *arg) -> int32_t {
            auto *c = static_cast<Ctx *>(arg);
            if (*c->ttfa < 0.0)
                *c->ttfa = msSince(c->start);
            c->samples += n;
            if (c->rate > 0 && c->samples * 1000.0 / c->rate > c->maxAudioMs) {
                c->aborted = true;
                return 0;
            }
            return 1;
        };
        auto audio = tts.Generate(line.text, gen, cb, &ctx);
        stats.computeMs = msSince(start);
        stats.aborted = ctx.aborted;
        std::string wavPath;
        if (!opt.wavDir.empty()) {
            wavPath = (fs::path(opt.wavDir) / (opt.label + "__" + line.id + "__1.wav")).string();
            writeWav16(wavPath, audio.samples, audio.sample_rate);
        }
        printJson(opt, line, 1, loadMs, warmupMs, audio, stats, wavPath,
                  firstClickRatio(audio.samples, audio.sample_rate));
    }

    std::fprintf(stderr, "done: %zu utterances, %d failures\n",
                 corpus.size() * static_cast<size_t>(opt.runs), failures);
    return failures == 0 ? 0 : 1;
}
