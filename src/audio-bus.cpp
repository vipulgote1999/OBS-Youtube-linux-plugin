#include "audio-bus.hpp"

AudioBus &AudioBus::instance()
{
    static AudioBus bus;
    return bus;
}

AudioBuffer &AudioBus::buffer()
{
    return buffer_;
}
