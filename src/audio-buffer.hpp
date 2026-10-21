#pragma once

#include <cstddef>
#include <deque>
#include <mutex>

/**
 * Thread-safe mono float PCM buffer between the speech producer and the OBS
 * audio feeder.
 *
 * push() stores raw samples and drops the oldest samples once maxSamples is
 * exceeded.  pop() applies the current volume, clamps each value to [-1, 1],
 * copies up to maxCount samples and zero-fills the rest, so the feeder can
 * always output a full chunk, including silence when idle.
 *
 * The default capacity is 120 seconds at 22,050 Hz (~10.6 MB of float data),
 * which gives the producer a generous cushion without unbounded growth.
 */
class AudioBuffer {
public:
    static constexpr size_t kDefaultMaxSamples = 120 * 22050;

    AudioBuffer() = default;

    void setMaxSamples(size_t maxSamples);
    void push(const float *samples, size_t count);
    size_t pop(float *out, size_t maxCount);
    void clear();
    size_t available() const;
    void setVolume(float volume);
    float volume() const;

private:
    /* Lock held by the caller. */
    void trimLocked();

    mutable std::mutex mutex_;
    std::deque<float> samples_;
    size_t maxSamples_ = kDefaultMaxSamples;
    float volume_ = 1.0f;
};
