#include "paths.hpp"

#include <obs-module.h>

#include <QCoreApplication>
#include <QFileInfo>

QString resolveBundledPath(const QString &relative)
{
    if (relative.isEmpty())
        return {};

    /* obs_module_file() is only meaningful while libobs has a current
     * module; outside OBS (host unit tests) obs_current_module() is null. */
    if (obs_current_module()) {
        char *moduleFile = obs_module_file(relative.toUtf8().constData());
        if (moduleFile) {
            const QString candidate = QString::fromUtf8(moduleFile);
            bfree(moduleFile);
            if (QFileInfo::exists(candidate))
                return candidate;
        }
    }

    const QString flatpakPath = QStringLiteral(
                                    "/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/") +
                                relative;
    if (QFileInfo::exists(flatpakPath))
        return flatpakPath;

    const QString portablePath =
        QCoreApplication::applicationDirPath() +
        QStringLiteral("/../share/obs/obs-plugins/yt-chat-tts/") + relative;
    if (QFileInfo::exists(portablePath))
        return portablePath;

    return {};
}
