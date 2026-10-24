#pragma once

#include <vector>

/**
 * Windowed-sinc sample-rate conversion.
 *
 * The local TTS server returns 24 kHz audio while the OBS source runs at
 * 22,050 Hz.  Linear interpolation would alias everything above 11 kHz back
 * into the speech band, which is audible as a metallic edge; this uses a
 * 33-tap Blackman-windowed sinc instead.
 */
class AudioResampler {
public:
    static std::vector<float> resample(const std::vector<float> &input, int inputRate,
                                       int outputRate);
};
