#pragma once

#include "audio-buffer.hpp"

/**
 * Process-wide PCM buffer shared by the future speech producer and the OBS
 * audio source feeder.  Function-local static construction is thread-safe in
 * C++11 and later, so the feeder may call instance() from its own thread.
 */
class AudioBus {
public:
    static AudioBus &instance();

    AudioBuffer &buffer();

private:
    AudioBus() = default;

    AudioBuffer buffer_;
};
