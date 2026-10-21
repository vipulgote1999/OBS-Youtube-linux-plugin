#include "audio-source.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <util/platform.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include "audio-bus.hpp"
#include "version.h"

namespace {
constexpr const char *kSourceId = "yt_chat_tts_source";
constexpr const char *kSourceName = "YouTube Chat TTS";

constexpr uint32_t kSampleRate = 22050;
constexpr uint32_t kChunkMs = 50;
constexpr size_t kFrames = 1102;
constexpr uint64_t kTimestampResetNs = 500000000ULL; /* 500 ms */

/* Bounded sleep so destroy() never waits a whole chunk for the feeder to
 * notice running == false. */
constexpr auto kMaxSleep =
    std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(5));

struct AudioSourceData {
    obs_source_t *source = nullptr;
    std::thread feeder;
    std::atomic<bool> running{true};
    uint64_t nextTsNs = 0;
};

const char *audio_source_get_name(void *typeData)
{
    UNUSED_PARAMETER(typeData);
    return kSourceName;
}

void audio_source_feeder(AudioSourceData *data)
{
    AudioBuffer &buffer = AudioBus::instance().buffer();
    std::vector<float> frames(kFrames, 0.0f);

    const auto chunk = std::chrono::milliseconds(kChunkMs);
    auto nextTick = std::chrono::steady_clock::now();

    while (data->running.load(std::memory_order_relaxed)) {
        nextTick += chunk;

        while (data->running.load(std::memory_order_relaxed)) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= nextTick)
                break;

            const auto remaining = nextTick - now;
            std::this_thread::sleep_for(std::min(remaining, kMaxSleep));
        }
        if (!data->running.load(std::memory_order_relaxed))
            break;

        /* After a long stall, resynchronise instead of emitting a burst of
         * catch-up chunks. */
        const auto afterSleep = std::chrono::steady_clock::now();
        if (afterSleep > nextTick + kMaxSleep)
            nextTick = afterSleep;

        const uint64_t nowNs = os_gettime_ns();
        if (data->nextTsNs == 0 || nowNs > data->nextTsNs + kTimestampResetNs)
            data->nextTsNs = nowNs;

        /* pop() applies volume and clamping and zero-fills the rest, so an
         * idle buffer still outputs a complete silence chunk. */
        buffer.pop(frames.data(), frames.size());

        obs_source_audio audio{};
        audio.data[0] = reinterpret_cast<const uint8_t *>(frames.data());
        audio.frames = static_cast<uint32_t>(frames.size());
        audio.speakers = SPEAKERS_MONO;
        audio.format = AUDIO_FORMAT_FLOAT;
        audio.samples_per_sec = kSampleRate;
        audio.timestamp = data->nextTsNs;
        obs_source_output_audio(data->source, &audio);

        data->nextTsNs += static_cast<uint64_t>(kFrames) * 1000000000ULL / kSampleRate;
    }
}

void *audio_source_create(obs_data_t *settings, obs_source_t *source)
{
    UNUSED_PARAMETER(settings);

    auto *data = new AudioSourceData;
    data->source = source;
    try {
        data->feeder = std::thread(audio_source_feeder, data);
    } catch (...) {
        blog(LOG_ERROR, "[%s] could not start the audio feeder thread", PLUGIN_NAME);
        delete data;
        return nullptr;
    }
    return data;
}

void audio_source_destroy(void *ptr)
{
    auto *data = static_cast<AudioSourceData *>(ptr);
    if (!data)
        return;

    /* destroy() is never called from the feeder thread itself, so joining is
     * safe here and guarantees no obs_source_output_audio() call happens after
     * this function returns. */
    data->running.store(false, std::memory_order_relaxed);
    if (data->feeder.joinable())
        data->feeder.join();
    delete data;
}

void audio_source_update(void *ptr, obs_data_t *settings)
{
    UNUSED_PARAMETER(ptr);
    UNUSED_PARAMETER(settings);
}

void audio_source_get_defaults(obs_data_t *settings)
{
    UNUSED_PARAMETER(settings);
}

bool enum_source_by_id(void *param, obs_source_t *source)
{
    auto *found = static_cast<bool *>(param);
    const char *id = obs_source_get_id(source);
    if (id && std::strcmp(id, kSourceId) == 0) {
        *found = true;
        return false;
    }
    return true;
}
} // namespace

void register_audio_source()
{
    /* obs_module_load() runs on the UI thread and only once per process, but a
     * guard keeps an accidental second call from double-registering. */
    static bool registered = false;
    if (registered)
        return;

    /* Zero-initialise first, then assign only the callbacks this source uses;
     * the struct must stay alive for the lifetime of the module. */
    static obs_source_info info = {};
    info.id = kSourceId;
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_AUDIO;
    info.get_name = audio_source_get_name;
    info.create = audio_source_create;
    info.destroy = audio_source_destroy;
    info.update = audio_source_update;
    info.get_defaults = audio_source_get_defaults;

    obs_register_source(&info);
    registered = true;
}

bool audio_source_exists()
{
    bool found = false;
    obs_enum_sources(enum_source_by_id, &found);
    return found;
}

void create_audio_source(bool allScenes)
{
    if (audio_source_exists()) {
        blog(LOG_WARNING, "[%s] audio source '%s' already exists; not creating a duplicate",
             PLUGIN_NAME, kSourceName);
        return;
    }

    obs_source_t *source = nullptr;

    if (allScenes) {
        obs_frontend_source_list scenes = {};
        obs_frontend_get_scenes(&scenes);

        if (scenes.sources.num == 0) {
            blog(LOG_WARNING, "[%s] no scenes available; cannot add audio source", PLUGIN_NAME);
            obs_frontend_source_list_free(&scenes);
            return;
        }

        source = obs_source_create(kSourceId, kSourceName, nullptr, nullptr);
        if (source) {
            size_t added = 0;
            for (size_t i = 0; i < scenes.sources.num; ++i) {
                obs_source_t *sceneSource = scenes.sources.array[i];
                obs_scene_t *scene = obs_scene_from_source(sceneSource);
                if (!scene) {
                    blog(LOG_WARNING, "[%s] skipping an entry that is not a scene", PLUGIN_NAME);
                    continue;
                }
                if (obs_scene_add(scene, source))
                    ++added;
                else
                    blog(LOG_WARNING, "[%s] could not add audio source to a scene", PLUGIN_NAME);
            }
            blog(LOG_INFO, "[%s] added audio source '%s' to %zu scene(s)", PLUGIN_NAME, kSourceName,
                 added);
        } else {
            blog(LOG_ERROR, "[%s] could not create audio source '%s'", PLUGIN_NAME, kSourceName);
        }

        obs_frontend_source_list_free(&scenes);
    } else {
        obs_source_t *sceneSource = obs_frontend_get_current_scene();
        if (!sceneSource) {
            blog(LOG_WARNING, "[%s] no current scene; cannot add audio source", PLUGIN_NAME);
            return;
        }

        source = obs_source_create(kSourceId, kSourceName, nullptr, nullptr);
        if (source) {
            obs_scene_t *scene = obs_scene_from_source(sceneSource);
            if (!scene)
                blog(LOG_WARNING, "[%s] current scene source is not a scene", PLUGIN_NAME);
            else if (obs_scene_add(scene, source))
                blog(LOG_INFO, "[%s] added audio source '%s' to the current scene", PLUGIN_NAME,
                     kSourceName);
            else
                blog(LOG_WARNING, "[%s] could not add audio source to the current scene",
                     PLUGIN_NAME);
        } else {
            blog(LOG_ERROR, "[%s] could not create audio source '%s'", PLUGIN_NAME, kSourceName);
        }

        obs_source_release(sceneSource);
    }

    /* Each obs_scene_add() took its own reference; drop the creation reference
     * exactly once so the source lives only inside the scenes. */
    if (source)
        obs_source_release(source);
}
