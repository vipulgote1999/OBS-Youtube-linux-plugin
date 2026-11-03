#include "audio-resampler.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {
constexpr int kTaps = 33;
constexpr float kPi = 3.14159265358979323846f;

float blackman(int k)
{
    const float x = float(k) / float(kTaps - 1);
    return 0.42f - 0.5f * std::cos(2.0f * kPi * x) + 0.08f * std::cos(4.0f * kPi * x);
}
} // namespace

std::vector<float> AudioResampler::resample(const std::vector<float> &input, int inputRate,
                                            int outputRate)
{
    if (input.empty() || inputRate <= 0 || outputRate <= 0 || inputRate == outputRate)
        return input;

    const double ratio = double(outputRate) / double(inputRate);
    const size_t outCount = size_t(std::llround(double(input.size()) * ratio));
    std::vector<float> output(outCount, 0.0f);

    /* Normalised cutoff: 1.0 would be the input Nyquist.  When downsampling,
     * keep everything below the *output* Nyquist so nothing aliases. */
    const double cutoff = 0.5 * std::min(1.0, ratio) * 2.0;
    const int half = kTaps / 2;

    for (size_t i = 0; i < outCount; ++i) {
        const double src = double(i) / ratio;
        const long centre = long(std::floor(src));
        double acc = 0.0;
        double weightSum = 0.0;

        for (int k = -half; k <= half; ++k) {
            const long index = centre + k;
            const double x = src - double(index);
            const double sincArg = kPi * x * cutoff;
            const double sinc = std::fabs(sincArg) < 1e-9 ? 1.0 : std::sin(sincArg) / sincArg;
            const double weight = sinc * double(blackman(k + half));
            const long clamped = std::clamp<long>(index, 0, long(input.size()) - 1);
            acc += double(input[size_t(clamped)]) * weight;
            weightSum += weight;
        }

        output[i] = float(std::fabs(weightSum) > 1e-9 ? acc / weightSum : acc);
    }
    return output;
}
