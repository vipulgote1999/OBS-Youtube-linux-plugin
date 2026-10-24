#include "wav.hpp"

#include <QtEndian>

#include <cstring>

namespace {
constexpr quint32 kUnknownDataSize = 0xFFFFFFFFu;
constexpr quint32 kMinimumFmtSize = 16;
constexpr int kRiffHeaderSize = 12;
constexpr quint32 kPcmFormat = 1;
constexpr quint16 kMonoChannels = 1;
constexpr quint32 kSampleRate = 22050;
constexpr quint16 kBitsPerSample = 16;

quint16 readU16(const QByteArray &bytes, int offset)
{
    return qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar *>(bytes.constData()) + offset);
}

quint32 readU32(const QByteArray &bytes, int offset)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(bytes.constData()) + offset);
}

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}
} // namespace

bool parseWav(const QByteArray &bytes, std::vector<float> *pcm, int *sampleRate,
              QString *error)
{
    if (error)
        error->clear();
    if (!pcm || !sampleRate)
        return fail(error, QStringLiteral("invalid WAV output buffer"));
    pcm->clear();
    *sampleRate = 0;

    if (bytes.size() < kRiffHeaderSize)
        return fail(error, QStringLiteral("truncated WAV header"));
    if (bytes.left(4) != QByteArrayLiteral("RIFF") ||
        bytes.mid(8, 4) != QByteArrayLiteral("WAVE")) {
        return fail(error, QStringLiteral("not a RIFF/WAVE file"));
    }

    bool haveFmt = false;
    quint16 audioFormat = 0;
    quint16 channels = 0;
    quint16 bitsPerSample = 0;
    quint32 rate = 0;
    qint64 dataOffset = -1;
    qint64 dataSize = 0;

    int offset = kRiffHeaderSize;
    while (offset + 8 <= bytes.size()) {
        const QByteArray chunkId = bytes.mid(offset, 4);
        const quint32 declaredSize = readU32(bytes, offset + 4);
        const int payloadOffset = offset + 8;
        const qint64 remaining = bytes.size() - payloadOffset;

        if (chunkId == QByteArrayLiteral("fmt ")) {
            if (declaredSize < kMinimumFmtSize) {
                return fail(error, QStringLiteral("invalid WAV fmt chunk size %1")
                                       .arg(declaredSize));
            }
            if (qint64(declaredSize) > remaining)
                return fail(error, QStringLiteral("truncated WAV fmt chunk"));

            audioFormat = readU16(bytes, payloadOffset);
            channels = readU16(bytes, payloadOffset + 2);
            rate = readU32(bytes, payloadOffset + 4);
            bitsPerSample = readU16(bytes, payloadOffset + 14);
            haveFmt = true;
        } else if (chunkId == QByteArrayLiteral("data")) {
            if (dataOffset < 0) {
                dataOffset = payloadOffset;
                if (declaredSize == 0 || declaredSize == kUnknownDataSize ||
                    qint64(declaredSize) > remaining) {
                    dataSize = remaining;
                } else {
                    dataSize = declaredSize;
                }
            }

            /* With an unknown/garbage length the following chunk boundary
             * cannot be trusted, so this chunk consumes the rest. */
            if (declaredSize == 0 || declaredSize == kUnknownDataSize ||
                qint64(declaredSize) > remaining) {
                break;
            }
        }

        qint64 next = qint64(payloadOffset) + qint64(declaredSize);
        if (declaredSize & 1)
            ++next;
        if (next <= offset || next > bytes.size())
            break;
        offset = int(next);
    }

    if (!haveFmt)
        return fail(error, QStringLiteral("missing WAV fmt chunk"));
    if (audioFormat != kPcmFormat) {
        return fail(error, QStringLiteral("unsupported WAV format %1: expected PCM")
                               .arg(audioFormat));
    }
    if (channels != kMonoChannels) {
        return fail(error, QStringLiteral("unsupported WAV channel count %1: expected mono")
                               .arg(channels));
    }
    if (rate != kSampleRate) {
        return fail(error, QStringLiteral("unsupported WAV sample rate %1: expected 22050 Hz")
                               .arg(rate));
    }
    if (bitsPerSample != kBitsPerSample) {
        return fail(error, QStringLiteral("unsupported WAV bit depth %1: expected 16-bit")
                               .arg(bitsPerSample));
    }
    if (dataOffset < 0)
        return fail(error, QStringLiteral("missing WAV data chunk"));

    if (dataOffset > bytes.size())
        dataSize = 0;
    else if (dataSize > bytes.size() - dataOffset)
        dataSize = bytes.size() - dataOffset;

    if (dataSize <= 0)
        return fail(error, QStringLiteral("WAV data chunk is empty"));
    if (dataSize % 2 != 0)
        return fail(error, QStringLiteral("truncated WAV data chunk: odd byte count"));

    pcm->reserve(size_t(dataSize / 2));
    const uchar *data = reinterpret_cast<const uchar *>(bytes.constData()) + dataOffset;
    for (qint64 i = 0; i < dataSize; i += 2) {
        const quint16 raw = qFromLittleEndian<quint16>(data + i);
        pcm->push_back(float(static_cast<qint16>(raw)) / 32768.0f);
    }

    *sampleRate = int(kSampleRate);
    return true;
}

bool parseWavToMono(const QByteArray &bytes, std::vector<float> *pcm, int *sampleRate,
                    QString *error)
{
    if (error)
        error->clear();
    if (!pcm || !sampleRate)
        return fail(error, QStringLiteral("invalid WAV output buffer"));
    pcm->clear();
    *sampleRate = 0;

    if (bytes.size() < kRiffHeaderSize)
        return fail(error, QStringLiteral("truncated WAV header"));
    if (bytes.left(4) != QByteArrayLiteral("RIFF") ||
        bytes.mid(8, 4) != QByteArrayLiteral("WAVE")) {
        return fail(error, QStringLiteral("not a RIFF/WAVE file"));
    }

    bool haveFmt = false;
    quint16 audioFormat = 0;
    quint16 channels = 0;
    quint16 bitsPerSample = 0;
    quint32 rate = 0;
    qint64 dataOffset = -1;
    qint64 dataSize = 0;

    int offset = kRiffHeaderSize;
    while (offset + 8 <= bytes.size()) {
        const QByteArray chunkId = bytes.mid(offset, 4);
        const quint32 declaredSize = readU32(bytes, offset + 4);
        const int payloadOffset = offset + 8;
        const qint64 remaining = bytes.size() - payloadOffset;

        if (chunkId == QByteArrayLiteral("fmt ")) {
            if (declaredSize < kMinimumFmtSize || qint64(declaredSize) > remaining)
                return fail(error, QStringLiteral("invalid WAV fmt chunk"));
            audioFormat = readU16(bytes, payloadOffset);
            channels = readU16(bytes, payloadOffset + 2);
            rate = readU32(bytes, payloadOffset + 4);
            bitsPerSample = readU16(bytes, payloadOffset + 14);
            if (audioFormat == 0xFFFE && declaredSize >= 26)
                audioFormat = readU16(bytes, payloadOffset + 24);
            haveFmt = true;
        } else if (chunkId == QByteArrayLiteral("data")) {
            if (dataOffset < 0) {
                dataOffset = payloadOffset;
                dataSize = (declaredSize == 0 || declaredSize == kUnknownDataSize ||
                            qint64(declaredSize) > remaining)
                               ? remaining
                               : qint64(declaredSize);
            }
            if (declaredSize == 0 || declaredSize == kUnknownDataSize ||
                qint64(declaredSize) > remaining)
                break;
        }

        qint64 next = qint64(payloadOffset) + qint64(declaredSize);
        if (declaredSize & 1)
            ++next;
        if (next <= offset || next > bytes.size())
            break;
        offset = int(next);
    }

    if (!haveFmt)
        return fail(error, QStringLiteral("missing WAV fmt chunk"));
    if (audioFormat != 1 && audioFormat != 3) {
        return fail(error, QStringLiteral("unsupported WAV format %1").arg(audioFormat));
    }
    if (channels != 1 && channels != 2) {
        return fail(error, QStringLiteral("unsupported WAV channel count %1")
                               .arg(channels));
    }
    if ((audioFormat == 1 && bitsPerSample != 16) || (audioFormat == 3 && bitsPerSample != 32)) {
        return fail(error, QStringLiteral("unsupported WAV bit depth %1")
                               .arg(bitsPerSample));
    }
    if (dataOffset < 0)
        return fail(error, QStringLiteral("missing WAV data chunk"));

    if (dataOffset > bytes.size())
        dataSize = 0;
    else if (dataSize > bytes.size() - dataOffset)
        dataSize = bytes.size() - dataOffset;

    const int bytesPerSample = bitsPerSample / 8;
    const int frameBytes = bytesPerSample * channels;
    if (dataSize < frameBytes)
        return fail(error, QStringLiteral("WAV data chunk is empty"));

    const qint64 frames = dataSize / frameBytes;
    pcm->reserve(size_t(frames));
    const uchar *data = reinterpret_cast<const uchar *>(bytes.constData()) + dataOffset;

    for (qint64 frame = 0; frame < frames; ++frame) {
        double sum = 0.0;
        for (int channel = 0; channel < channels; ++channel) {
            const uchar *sample = data + frame * frameBytes + channel * bytesPerSample;
            if (audioFormat == 3) {
                float value = 0.0f;
                std::memcpy(&value, sample, sizeof(value));
                sum += value;
            } else {
                sum += double(qint16(qFromLittleEndian<quint16>(sample))) / 32768.0;
            }
        }
        pcm->push_back(float(sum / channels));
    }

    *sampleRate = int(rate);
    return true;
}
