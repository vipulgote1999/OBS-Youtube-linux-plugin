#pragma once

#include <QByteArray>
#include <QString>

#include <vector>

/**
 * Parses RIFF/WAVE PCM audio into mono float samples.
 *
 * Only 16-bit PCM at 22,050 Hz mono is accepted.  Chunk walking tolerates
 * odd-sized chunks and unknown chunks.  A data chunk whose declared size is
 * zero, 0xFFFFFFFF, or larger than the remaining bytes is treated as
 * "read the rest", which is what espeak-ng --stdout emits.
 */
bool parseWav(const QByteArray &bytes, std::vector<float> *pcm, int *sampleRate,
              QString *error);

/**
 * Parses RIFF/WAVE into mono float samples at the file's own rate.
 *
 * Accepts 16-bit PCM or 32-bit IEEE float, mono or stereo, any sample rate.
 * Used by the local TTS engine, whose server may return 24 kHz stereo; callers
 * resample afterwards.  parseWav() stays strict for the 22,050 Hz mono path.
 */
bool parseWavToMono(const QByteArray &bytes, std::vector<float> *pcm, int *sampleRate,
                    QString *error);
