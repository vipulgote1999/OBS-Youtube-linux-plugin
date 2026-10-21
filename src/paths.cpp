#include "paths.hpp"

#include <obs-module.h>

#include <QCoreApplication>
#include <QFileInfo>

namespace {
QString moduleFileCandidate(const QString &relative)
{
    /* obs_module_file() is only meaningful while libobs has a current
     * module; outside OBS (host unit tests) obs_current_module() is null. */
    if (!obs_current_module())
        return {};

    char *moduleFile = obs_module_file(relative.toUtf8().constData());
    if (!moduleFile)
        return {};

    const QString candidate = QString::fromUtf8(moduleFile);
    bfree(moduleFile);
    return candidate;
}
} // namespace

QStringList bundledPathCandidates(const QString &relative)
{
    QStringList candidates;
    if (relative.isEmpty())
        return candidates;

    const QString moduleFile = moduleFileCandidate(relative);
    if (!moduleFile.isEmpty())
        candidates.append(moduleFile);

    candidates.append(
        QStringLiteral("/app/plugins/YtChatTts/share/obs/obs-plugins/yt-chat-tts/") + relative);

    candidates.append(QCoreApplication::applicationDirPath() +
                      QStringLiteral("/../share/obs/obs-plugins/yt-chat-tts/") + relative);

    return candidates;
}

QString resolveBundledPath(const QString &relative)
{
    const QStringList candidates = bundledPathCandidates(relative);
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return candidate;
    }

    return {};
}
