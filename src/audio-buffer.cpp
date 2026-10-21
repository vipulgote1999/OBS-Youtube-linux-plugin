#include "audio-buffer.hpp"

#include <algorithm>

namespace {
float clampSample(float value)
{
    if (value > 1.0f)
        return 1.0f;
    if (value < -1.0f)
        return -1.0f;
    return value;
}
} // namespace

void AudioBuffer::setMaxSamples(size_t maxSamples)
{
    std::lock_guard<std::mutex> lock(mutex_);
    maxSamples_ = maxSamples;
    trimLocked();
}

void AudioBuffer::push(const float *samples, size_t count)
{
    if (!samples || count == 0)
        return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (maxSamples_ == 0)
        return;

    /* A single push larger than the whole buffer only keeps its newest tail. */
    const size_t keep = std::min(count, maxSamples_);
    samples_.insert(samples_.end(), samples + (count - keep), samples + count);
    trimLocked();
}

size_t AudioBuffer::pop(float *out, size_t maxCount)
{
    if (!out || maxCount == 0)
        return 0;

    std::lock_guard<std::mutex> lock(mutex_);
    const size_t copied = std::min(maxCount, samples_.size());
    const float volume = volume_;
    for (size_t i = 0; i < copied; ++i) {
        out[i] = clampSample(samples_.front() * volume);
        samples_.pop_front();
    }
    for (size_t i = copied; i < maxCount; ++i)
        out[i] = 0.0f;
    return copied;
}

void AudioBuffer::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    samples_.clear();
}

size_t AudioBuffer::available() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return samples_.size();
}

void AudioBuffer::setVolume(float volume)
{
    std::lock_guard<std::mutex> lock(mutex_);
    volume_ = volume;
}

float AudioBuffer::volume() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return volume_;
}

void AudioBuffer::trimLocked()
{
    while (samples_.size() > maxSamples_)
        samples_.pop_front();
}
