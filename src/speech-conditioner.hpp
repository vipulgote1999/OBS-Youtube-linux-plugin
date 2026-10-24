#pragma once

#include <vector>

/**
 * Removes the two artifacts every TTS backend leaves at utterance boundaries:
 * a DC offset and a click when the buffer stops mid-waveform.
 *
 * The worker has the whole utterance, so the stage is offline and keeps the
 * sample count unchanged.  A measured Pocket TTS clip ended on +0.303 full
 * scale; the next buffer is silence, so that step is an audible pop.  Pocket
 * also never pads silence, unlike Kokoro, which is why it showed the artifact
 * most often in the bake-off.
 *
 * Deliberately limited to a DC blocker and fades: the bake-off showed no
 * broadband hiss in the quiet regions (tail/lead silence measured 0.0), so a
 * noise gate would add pumping without removing anything real.
 */
class SpeechConditioner {
public:
    struct Config {
        int sampleRate = 22050;
        /* Raised-cosine ramp at each end.  5 ms moves the step's energy below
         * ~200 Hz, where it is inaudible, and is short enough not to clip a
         * plosive onset. */
        float fadeMs = 5.0f;
        /* One-pole high-pass.  Low enough to leave speech untouched, high
         * enough to remove the DC offset some engines leave (Kokoro measured
         * -0.0014). */
        float dcBlockHz = 20.0f;
    };

    static void process(std::vector<float> *pcm);
    static void process(std::vector<float> *pcm, const Config &config);

    /**
     * Stateful variant for chunked synthesis.
     *
     * The DC blocker keeps its state across chunks, the fade-in spans chunk
     * boundaries, and the fade-out is applied when the caller marks the last
     * chunk.  Used when the low-latency streaming option is enabled.
     */
    class Stream {
    public:
        explicit Stream(const Config &config);
        void process(std::vector<float> *pcm, bool first, bool last);

    private:
        Config config_;
        float dcA_ = 0.0f;
        float xPrev_ = 0.0f;
        float yPrev_ = 0.0f;
        size_t fadeSamples_ = 0;
        size_t fadeInRemaining_ = 0;
        bool started_ = false;
    };
};
