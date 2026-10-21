#pragma once

/** Registers the yt_chat_tts_source input with libobs.  Call from
 * obs_module_load() before any source is created. */
void register_audio_source();

/** Creates the TTS audio source and adds it to the current scene or to every
 * scene, then releases its local reference.  Does nothing when a source with
 * this ID already exists. */
void create_audio_source(bool allScenes);

/** True when a source with ID yt_chat_tts_source exists in the current scene
 * collection. */
bool audio_source_exists();
