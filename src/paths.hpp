#pragma once

#include <QString>
#include <QStringList>

/**
 * Resolves a path relative to the plugin's installed data directory.
 *
 * Tries, in order: obs_module_file(), the Flatpak extension prefix, and the
 * sibling share/obs/obs-plugins/yt-chat-tts directory next to the executable
 * (a host/portable install layout).  Returns an empty string if no candidate
 * exists.
 */
QString resolveBundledPath(const QString &relative);

/**
 * Returns the exact candidate paths resolveBundledPath() would try, in order.
 * Used for startup diagnostics (for example when the helper is missing) so
 * the log does not duplicate the resolver's layout rules.
 */
QStringList bundledPathCandidates(const QString &relative);
