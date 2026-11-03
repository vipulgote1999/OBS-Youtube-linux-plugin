#include "libsupertonic-api.hpp"

#include "supertonic-text.hpp"

#include <onnxruntime_cxx_api.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <numeric>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
constexpr int kSilenceMs = 300;
constexpr int kMaxChunkLength = 300;
constexpr int kMaxShortLanguageChunkLength = 120;
constexpr int kExpectedModelSampleRate = 44100;

struct ModelConfig {
    int sampleRate = 0;
    int baseChunkSize = 0;
    int chunkCompressFactor = 0;
    int latentDim = 0;
};

struct StyleData {
    std::vector<float> ttlData;
    std::vector<float> dpData;
    std::vector<int64_t> ttlShape;
    std::vector<int64_t> dpShape;
};

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

QString onnxDirectory(const QString &modelDir)
{
    const QString nested = QDir(modelDir).filePath(QStringLiteral("onnx"));
    return QFileInfo(nested).isDir() ? nested : modelDir;
}

bool readJson(const QString &path, QJsonDocument *document, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(error, QStringLiteral("could not open %1: %2").arg(path, file.errorString()));
    }

    QJsonParseError parseError;
    *document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return fail(error, QStringLiteral("invalid JSON in %1: %2")
                               .arg(path, parseError.errorString()));
    }
    return true;
}

bool readIntArray(const QJsonValue &value, std::vector<int64_t> *dims, QString *error)
{
    if (!value.isArray())
        return fail(error, QStringLiteral("expected a dimensions array"));
    const QJsonArray array = value.toArray();
    dims->clear();
    dims->reserve(array.size());
    for (const QJsonValue &entry : array) {
        if (!entry.isDouble())
            return fail(error, QStringLiteral("dimension is not a number"));
        dims->push_back(int64_t(entry.toDouble()));
    }
    if (dims->size() != 3 || (*dims)[0] <= 0 || (*dims)[1] <= 0 || (*dims)[2] <= 0)
        return fail(error, QStringLiteral("expected a positive three-dimensional shape"));
    return true;
}

bool flattenNumbers(const QJsonValue &value, std::vector<float> *numbers, QString *error)
{
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &entry : array) {
            if (!flattenNumbers(entry, numbers, error))
                return false;
        }
        return true;
    }
    if (value.isDouble()) {
        numbers->push_back(float(value.toDouble()));
        return true;
    }
    return fail(error, QStringLiteral("voice style data contains a non-number"));
}

bool loadModelConfig(const QString &onnxDir, ModelConfig *config, QString *error)
{
    const QString path = QDir(onnxDir).filePath(QStringLiteral("tts.json"));
    QJsonDocument document;
    if (!readJson(path, &document, error))
        return false;
    if (!document.isObject())
        return fail(error, path + QStringLiteral(" is not a JSON object"));

    const QJsonObject root = document.object();
    const QJsonObject ae = root.value(QStringLiteral("ae")).toObject();
    const QJsonObject ttl = root.value(QStringLiteral("ttl")).toObject();

    config->sampleRate = ae.value(QStringLiteral("sample_rate")).toInt();
    config->baseChunkSize = ae.value(QStringLiteral("base_chunk_size")).toInt();
    config->chunkCompressFactor = ttl.value(QStringLiteral("chunk_compress_factor")).toInt();
    config->latentDim = ttl.value(QStringLiteral("latent_dim")).toInt();

    if (config->sampleRate <= 0 || config->baseChunkSize <= 0 ||
        config->chunkCompressFactor <= 0 || config->latentDim <= 0) {
        return fail(error, path + QStringLiteral(" is missing required model dimensions"));
    }
    return true;
}

bool loadIndexer(const QString &onnxDir, std::vector<int64_t> *indexer, QString *error)
{
    const QString path = QDir(onnxDir).filePath(QStringLiteral("unicode_indexer.json"));
    QJsonDocument document;
    if (!readJson(path, &document, error))
        return false;
    if (!document.isArray())
        return fail(error, path + QStringLiteral(" is not a JSON array"));

    const QJsonArray array = document.array();
    indexer->clear();
    indexer->reserve(array.size());
    for (const QJsonValue &entry : array) {
        if (!entry.isDouble()) {
            return fail(error, path + QStringLiteral(" contains a non-numeric entry"));
        }
        indexer->push_back(int64_t(entry.toDouble()));
    }
    if (indexer->empty())
        return fail(error, path + QStringLiteral(" is empty"));
    return true;
}

bool loadStylePart(const QJsonObject &root, const QString &key, const QString &path,
                   std::vector<float> *data, std::vector<int64_t> *shape, QString *error)
{
    const QJsonObject part = root.value(key).toObject();
    if (!readIntArray(part.value(QStringLiteral("dims")), shape,
                      error)) {
        if (error)
            *error = path + QStringLiteral(": ") + *error;
        return false;
    }

    if (!flattenNumbers(part.value(QStringLiteral("data")), data, error)) {
        if (error)
            *error = path + QStringLiteral(": ") + *error;
        return false;
    }

    const int64_t expected = (*shape)[0] * (*shape)[1] * (*shape)[2];
    if (int64_t(data->size()) != expected) {
        return fail(error, QStringLiteral("%1: %2 has %3 values, expected %4")
                               .arg(path, key)
                               .arg(data->size())
                               .arg(expected));
    }
    if ((*shape)[0] != 1)
        return fail(error, path + QStringLiteral(": only single-speaker styles are supported"));
    return true;
}

bool loadStyle(const QString &voicePath, StyleData *style, QString *error)
{
    QJsonDocument document;
    if (!readJson(voicePath, &document, error))
        return false;
    if (!document.isObject())
        return fail(error, voicePath + QStringLiteral(" is not a JSON object"));

    const QJsonObject root = document.object();
    if (!loadStylePart(root, QStringLiteral("style_ttl"), voicePath, &style->ttlData,
                       &style->ttlShape, error))
        return false;
    if (!loadStylePart(root, QStringLiteral("style_dp"), voicePath, &style->dpData,
                       &style->dpShape, error))
        return false;
    return true;
}

bool fileExists(const QString &path, QString *error)
{
    if (QFileInfo::exists(path))
        return true;
    return fail(error, QStringLiteral("Supertonic model file is missing: ") + path);
}

float clampedSpeed(float speed)
{
    if (!std::isfinite(speed) || speed <= 0.0f)
        return 1.0f;
    return std::clamp(speed, 0.5f, 2.0f);
}

int clampedSteps(int steps)
{
    return std::clamp(steps, 4, 16);
}
} // namespace

struct LibSupertonicApi::Impl {
    Impl(QString modelDirIn, QString voicePathIn, QString languageIn, int threadsIn)
        : modelDir(std::move(modelDirIn)),
          voicePath(std::move(voicePathIn)),
          language(languageIn.isEmpty() ? QStringLiteral("en") : languageIn),
          threads(std::clamp(threadsIn, 1, 8)),
          env(ORT_LOGGING_LEVEL_WARNING, "yt-chat-tts-supertonic"),
          rng(std::random_device{}())
    {
    }

    bool ensureLoaded(QString *error)
    {
        if (loaded)
            return true;
        if (loadFailed_.load()) {
            if (error)
                *error = loadError;
            return false;
        }

        try {
            const QString onnxDir = onnxDirectory(modelDir);
            const QStringList required = {
                QStringLiteral("duration_predictor.onnx"),
                QStringLiteral("text_encoder.onnx"),
                QStringLiteral("vector_estimator.onnx"),
                QStringLiteral("vocoder.onnx"),
                QStringLiteral("tts.json"),
                QStringLiteral("unicode_indexer.json"),
            };
            for (const QString &file : required) {
                if (!fileExists(QDir(onnxDir).filePath(file), error))
                    return failLoad(error);
            }
            if (!fileExists(voicePath, error))
                return failLoad(error);

            if (!loadModelConfig(onnxDir, &config, error))
                return failLoad(error);
            if (config.sampleRate != kExpectedModelSampleRate) {
                if (error)
                    *error = QStringLiteral("Supertonic model sample rate is %1 Hz; "
                                            "expected %2 Hz")
                                 .arg(config.sampleRate)
                                 .arg(kExpectedModelSampleRate);
                return failLoad(error);
            }
            if (!loadIndexer(onnxDir, &indexer, error))
                return failLoad(error);
            if (!loadStyle(voicePath, &style, error))
                return failLoad(error);

            Ort::SessionOptions options;
            options.SetIntraOpNumThreads(threads);
            options.SetInterOpNumThreads(1);
            options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

            durationPredictor = std::make_unique<Ort::Session>(
                env, QDir(onnxDir).filePath(QStringLiteral("duration_predictor.onnx"))
                         .toUtf8()
                         .constData(),
                options);
            textEncoder = std::make_unique<Ort::Session>(
                env,
                QDir(onnxDir).filePath(QStringLiteral("text_encoder.onnx")).toUtf8().constData(),
                options);
            vectorEstimator = std::make_unique<Ort::Session>(
                env,
                QDir(onnxDir).filePath(QStringLiteral("vector_estimator.onnx"))
                    .toUtf8()
                    .constData(),
                options);
            vocoder = std::make_unique<Ort::Session>(
                env, QDir(onnxDir).filePath(QStringLiteral("vocoder.onnx")).toUtf8().constData(),
                options);

            loaded = true;
            if (error)
                error->clear();
            return true;
        } catch (const std::exception &exception) {
            if (error)
                *error = QStringLiteral("could not load Supertonic model: ") +
                         QString::fromUtf8(exception.what());
            return failLoad(error);
        }
    }

    bool failLoad(QString *error)
    {
        if (error && error->isEmpty())
            *error = QStringLiteral("could not load Supertonic model");
        loadError = error ? *error : QStringLiteral("could not load Supertonic model");
        loadFailed_.store(true);
        return false;
    }

    bool inferChunk(const QString &text, float speed, int steps, std::vector<float> *wav,
                    QString *error)
    {
        wav->clear();

        const QString processed = SupertonicText::preprocess(text, language);
        const std::vector<uint16_t> values = SupertonicText::unicodeValues(processed);
        if (values.empty())
            return true;

        std::vector<int64_t> textIds(values.size());
        for (size_t i = 0; i < values.size(); ++i) {
            const uint16_t value = values[i];
            textIds[i] = value < indexer.size() ? indexer[value] : 0;
        }

        std::vector<int64_t> textIdsShape = {1, int64_t(textIds.size())};
        std::vector<float> textMask(textIds.size(), 1.0f);
        std::vector<int64_t> textMaskShape = {1, 1, int64_t(textMask.size())};

        Ort::MemoryInfo memory =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::RunOptions runOptions{nullptr};
        const auto runSession = [&](Ort::Session &session, const std::vector<Ort::Value> &inputs,
                                    const char *const *inputNames, const char *const *outputNames,
                                    size_t outputCount) {
            return session.Run(runOptions, inputNames, inputs.data(), inputs.size(),
                               outputNames, outputCount);
        };

        const size_t batch = 1;
        const std::vector<int64_t> batchShape = {int64_t(batch)};
        std::vector<float> totalStep(batch, float(steps));

        /* Duration predictor. */
        std::vector<Ort::Value> dpInputs;
        dpInputs.push_back(Ort::Value::CreateTensor<int64_t>(
            memory, textIds.data(), textIds.size(), textIdsShape.data(), textIdsShape.size()));
        dpInputs.push_back(Ort::Value::CreateTensor<float>(
            memory, style.dpData.data(), style.dpData.size(), style.dpShape.data(),
            style.dpShape.size()));
        dpInputs.push_back(Ort::Value::CreateTensor<float>(
            memory, textMask.data(), textMask.size(), textMaskShape.data(),
            textMaskShape.size()));

        static const char *dpInputNames[] = {"text_ids", "style_dp", "text_mask"};
        static const char *dpOutputNames[] = {"duration"};
        auto dpOutputs = runSession(*durationPredictor, dpInputs, dpInputNames, dpOutputNames, 1);
        if (dpOutputs.empty() || dpOutputs[0].GetTensorTypeAndShapeInfo().GetElementCount() < 1)
            return fail(error, QStringLiteral("Supertonic duration predictor returned no duration"));

        const float predicted = dpOutputs[0].GetTensorMutableData<float>()[0];
        if (!std::isfinite(predicted) || predicted <= 0.0f)
            return fail(error, QStringLiteral("Supertonic duration predictor returned %1")
                                   .arg(predicted));
        const float duration = predicted / speed;

        /* Text encoder. */
        std::vector<Ort::Value> encoderInputs;
        encoderInputs.push_back(Ort::Value::CreateTensor<int64_t>(
            memory, textIds.data(), textIds.size(), textIdsShape.data(), textIdsShape.size()));
        encoderInputs.push_back(Ort::Value::CreateTensor<float>(
            memory, style.ttlData.data(), style.ttlData.size(), style.ttlShape.data(),
            style.ttlShape.size()));
        encoderInputs.push_back(Ort::Value::CreateTensor<float>(
            memory, textMask.data(), textMask.size(), textMaskShape.data(),
            textMaskShape.size()));

        static const char *encoderInputNames[] = {"text_ids", "style_ttl", "text_mask"};
        static const char *encoderOutputNames[] = {"text_emb"};
        auto encoderOutputs =
            runSession(*textEncoder, encoderInputs, encoderInputNames, encoderOutputNames, 1);
        if (encoderOutputs.empty())
            return fail(error, QStringLiteral("Supertonic text encoder returned no embedding"));

        auto embeddingInfo = encoderOutputs[0].GetTensorTypeAndShapeInfo();
        const size_t embeddingCount = embeddingInfo.GetElementCount();
        std::vector<float> embedding(embeddingCount);
        std::copy_n(encoderOutputs[0].GetTensorMutableData<float>(), embeddingCount,
                    embedding.begin());
        const std::vector<int64_t> embeddingShape = embeddingInfo.GetShape();

        /* Latent noise.  With a single style and a single sentence there is no
         * padding, so the latent mask is all ones. */
        const int64_t wavLength = int64_t(std::llround(double(duration) * config.sampleRate));
        const int latentSize = config.baseChunkSize * config.chunkCompressFactor;
        const int latentLength = int((std::max<int64_t>(wavLength, 1) + latentSize - 1) / latentSize);
        const int latentDim = config.latentDim * config.chunkCompressFactor;
        if (latentLength <= 0 || latentDim <= 0)
            return fail(error, QStringLiteral("Supertonic produced an invalid latent shape"));

        std::vector<float> latent(size_t(latentDim) * size_t(latentLength));
        std::normal_distribution<float> normal(0.0f, 1.0f);
        for (float &value : latent)
            value = normal(rng);

        std::vector<float> latentMask(latentLength, 1.0f);
        std::vector<int64_t> latentShape = {1, int64_t(latentDim), int64_t(latentLength)};
        std::vector<int64_t> latentMaskShape = {1, 1, int64_t(latentLength)};

        /* Flow-matching denoising. */
        static const char *estimatorInputNames[] = {
            "noisy_latent", "text_emb", "style_ttl", "text_mask",
            "latent_mask",  "total_step", "current_step",
        };
        static const char *estimatorOutputNames[] = {"denoised_latent"};
        std::vector<float> currentStep(batch);

        for (int step = 0; step < steps; ++step) {
            if (cancelRequested.load())
                return fail(error, QStringLiteral("cancelled"));

            currentStep[0] = float(step);
            std::vector<Ort::Value> estimatorInputs;
            estimatorInputs.push_back(Ort::Value::CreateTensor<float>(
                memory, latent.data(), latent.size(), latentShape.data(), latentShape.size()));
            estimatorInputs.push_back(Ort::Value::CreateTensor<float>(
                memory, embedding.data(), embedding.size(), embeddingShape.data(),
                embeddingShape.size()));
            estimatorInputs.push_back(Ort::Value::CreateTensor<float>(
                memory, style.ttlData.data(), style.ttlData.size(), style.ttlShape.data(),
                style.ttlShape.size()));
            estimatorInputs.push_back(Ort::Value::CreateTensor<float>(
                memory, textMask.data(), textMask.size(), textMaskShape.data(),
                textMaskShape.size()));
            estimatorInputs.push_back(Ort::Value::CreateTensor<float>(
                memory, latentMask.data(), latentMask.size(), latentMaskShape.data(),
                latentMaskShape.size()));
            estimatorInputs.push_back(Ort::Value::CreateTensor<float>(
                memory, totalStep.data(), totalStep.size(), batchShape.data(), batchShape.size()));
            estimatorInputs.push_back(Ort::Value::CreateTensor<float>(
                memory, currentStep.data(), currentStep.size(), batchShape.data(),
                batchShape.size()));

            auto estimatorOutputs = runSession(*vectorEstimator, estimatorInputs,
                                               estimatorInputNames, estimatorOutputNames, 1);
            if (estimatorOutputs.empty())
                return fail(error, QStringLiteral("Supertonic denoiser returned no latent"));

            auto info = estimatorOutputs[0].GetTensorTypeAndShapeInfo();
            if (info.GetElementCount() != latent.size()) {
                return fail(error, QStringLiteral("Supertonic denoiser returned %1 values; "
                                                  "expected %2")
                                       .arg(info.GetElementCount())
                                       .arg(latent.size()));
            }
            std::copy_n(estimatorOutputs[0].GetTensorMutableData<float>(), latent.size(),
                        latent.begin());
        }

        if (cancelRequested.load())
            return fail(error, QStringLiteral("cancelled"));

        /* Vocoder. */
        std::vector<Ort::Value> vocoderInputs;
        vocoderInputs.push_back(Ort::Value::CreateTensor<float>(
            memory, latent.data(), latent.size(), latentShape.data(), latentShape.size()));
        static const char *vocoderInputNames[] = {"latent"};
        static const char *vocoderOutputNames[] = {"wav_tts"};
        auto vocoderOutputs =
            runSession(*vocoder, vocoderInputs, vocoderInputNames, vocoderOutputNames, 1);
        if (vocoderOutputs.empty())
            return fail(error, QStringLiteral("Supertonic vocoder returned no audio"));

        auto wavInfo = vocoderOutputs[0].GetTensorTypeAndShapeInfo();
        const size_t wavCount = wavInfo.GetElementCount();
        wav->resize(wavCount);
        std::copy_n(vocoderOutputs[0].GetTensorMutableData<float>(), wavCount, wav->begin());
        return true;
    }

    bool run(const QString &text, float speedIn, int stepsIn, std::vector<float> *pcm,
             const ChunkSink *sink, int *sampleRate, QString *error)
    {
        if (error)
            error->clear();
        if (pcm)
            pcm->clear();
        if (sampleRate)
            *sampleRate = 0;
        if (!pcm && !sink)
            return fail(error, QStringLiteral("invalid TTS output buffer"));
        if (cancelRequested.load())
            return fail(error, QStringLiteral("cancelled"));

        std::lock_guard<std::mutex> lock(mutex);
        if (!ensureLoaded(error))
            return false;
        if (cancelRequested.load())
            return fail(error, QStringLiteral("cancelled"));

        const float speed = clampedSpeed(speedIn);
        const int steps = clampedSteps(stepsIn);
        const int maxChunkLength =
            (language == QLatin1String("ko") || language == QLatin1String("ja"))
                ? kMaxShortLanguageChunkLength
                : kMaxChunkLength;
        const QStringList chunks = SupertonicText::chunk(text, maxChunkLength);

        if (sampleRate)
            *sampleRate = config.sampleRate;
        if (chunks.isEmpty())
            return true;

        const size_t silenceLength =
            size_t(std::max(0, (config.sampleRate * kSilenceMs) / 1000));

        for (int index = 0; index < chunks.size(); ++index) {
            if (cancelRequested.load()) {
                if (pcm)
                    pcm->clear();
                if (sampleRate)
                    *sampleRate = 0;
                return fail(error, QStringLiteral("cancelled"));
            }

            std::vector<float> wav;
            try {
                if (!inferChunk(chunks.at(index), speed, steps, &wav, error)) {
                    if (pcm)
                        pcm->clear();
                    if (sampleRate)
                        *sampleRate = 0;
                    return false;
                }
            } catch (const std::exception &exception) {
                if (pcm)
                    pcm->clear();
                if (sampleRate)
                    *sampleRate = 0;
                return fail(error, QStringLiteral("Supertonic inference failed: ") +
                                       QString::fromUtf8(exception.what()));
            }

            const bool isLast = index + 1 == chunks.size();
            if (sink) {
                std::vector<float> chunk;
                if (index > 0)
                    chunk.assign(silenceLength, 0.0f);
                chunk.insert(chunk.end(), wav.begin(), wav.end());
                if (!chunk.empty() && !(*sink)(chunk.data(), chunk.size(), isLast)) {
                    if (sampleRate)
                        *sampleRate = 0;
                    return fail(error, QStringLiteral("cancelled"));
                }
            } else {
                if (index > 0)
                    pcm->insert(pcm->end(), silenceLength, 0.0f);
                pcm->insert(pcm->end(), wav.begin(), wav.end());
            }
        }

        if (cancelRequested.load()) {
            if (pcm)
                pcm->clear();
            if (sampleRate)
                *sampleRate = 0;
            return fail(error, QStringLiteral("cancelled"));
        }

        return true;
    }

    QString modelDir;
    QString voicePath;
    QString language;
    int threads;

    Ort::Env env;
    std::mt19937 rng;

    ModelConfig config;
    std::vector<int64_t> indexer;
    StyleData style;

    std::unique_ptr<Ort::Session> durationPredictor;
    std::unique_ptr<Ort::Session> textEncoder;
    std::unique_ptr<Ort::Session> vectorEstimator;
    std::unique_ptr<Ort::Session> vocoder;

    std::mutex mutex;
    bool loaded = false;
    std::atomic_bool loadFailed_{false};
    QString loadError;
    std::atomic_bool cancelRequested{false};
};

LibSupertonicApi::LibSupertonicApi(QString modelDir, QString voicePath, QString language,
                                   int threads)
    : impl_(std::make_unique<Impl>(std::move(modelDir), std::move(voicePath),
                                   std::move(language), threads))
{
}

LibSupertonicApi::~LibSupertonicApi() = default;

bool LibSupertonicApi::synthesize(const QString &text, float speed, int steps,
                                  std::vector<float> *pcm, int *sampleRate, QString *error)
{
    return impl_->run(text, speed, steps, pcm, nullptr, sampleRate, error);
}

bool LibSupertonicApi::synthesizeStreaming(const QString &text, float speed, int steps,
                                           const ChunkSink &sink, int *sampleRate, QString *error)
{
    return impl_->run(text, speed, steps, nullptr, &sink, sampleRate, error);
}

void LibSupertonicApi::cancel()
{
    impl_->cancelRequested.store(true);
}

void LibSupertonicApi::resetCancel()
{
    impl_->cancelRequested.store(false);
}

bool LibSupertonicApi::initializationFailed() const
{
    return impl_->loadFailed_.load();
}
