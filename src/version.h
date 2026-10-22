#pragma once

/* Plugin identity constants shared by the module code and build scripts. */

/* libobs module id; must match the .so name and the locale ini basename.
 * The module installs as yt-chat-tts.so with data under
 * share/obs/obs-plugins/yt-chat-tts/. */
#define PLUGIN_NAME "yt-chat-tts"

/* ID passed to obs_frontend_add_dock_by_id / obs_frontend_remove_dock. */
#define PLUGIN_DOCK_ID "yt-chat-tts-dock"

/* Keep in sync with the project() version in CMakeLists.txt. */
#define PLUGIN_VERSION "0.2.0"
