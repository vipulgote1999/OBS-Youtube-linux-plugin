#pragma once

#include <QString>

/**
 * Resolves a path relative to the plugin's installed data directory.
 *
 * Tries, in order: obs_module_file(), the Flatpak extension prefix, and the
 * sibling share/obs/obs-plugins/yt-chat-tts directory next to the executable
 * (a host/portable install layout).  Returns an empty string if no candidate
 * exists.
 */
QString resolveBundledPath(const QString &relative);
