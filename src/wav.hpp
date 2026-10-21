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
