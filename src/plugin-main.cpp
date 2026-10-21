#include <obs-frontend-api.h>
#include <obs-module.h>
#include <QWidget>

#include "version.h"
#include "yt-chat-tts-dock.hpp"

#include "audio-source.hpp"
#include "paths.hpp"
#include "plugin-context.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

/* The context owns the chat helper, the TTS worker thread and the settings.
 * It is deliberately not a QObject child of the dock: plugin-main deletes it
 * after the dock is gone.  The dock only holds a non-owning pointer. */
static PluginContext *pluginContext = nullptr;

/* Plugin-owned widget, valid only when obs_frontend_add_dock_by_id() failed
 * (for example because the dock id is already taken) so the frontend never
 * took ownership of the widget.  When registration succeeds the widget lives
 * inside the frontend's OBSDock wrapper, which deletes it exactly once on
 * obs_frontend_remove_dock() or at window teardown; manual deletion of a
 * registered widget would double-free, so it is never done here. */
static YtChatTtsDock *unregisteredDock = nullptr;

bool obs_module_load(void)
{
    blog(LOG_INFO, "[%s] loaded (version %s)", PLUGIN_NAME, PLUGIN_VERSION);

    register_audio_source();

    pluginContext = new PluginContext;

    /* Startup diagnostics: one line per resolved resource.  When a resource is
     * missing, list the exact candidate paths the single resolver tried. */
    const ChatConfig chatConfig = pluginContext->settings().toChatConfig();
    if (chatConfig.helperPath.isEmpty()) {
        blog(LOG_WARNING, "[%s] ytchat-helper not found; tried: %s", PLUGIN_NAME,
             bundledPathCandidates(QStringLiteral("bin/ytchat-helper"))
                 .join(QStringLiteral(", "))
                 .toUtf8()
                 .constData());
    } else {
        blog(LOG_INFO, "[%s] ytchat-helper: %s", PLUGIN_NAME,
             chatConfig.helperPath.toUtf8().constData());
    }

    const QString espeakPath =
        pluginContext->settings().espeakPath.isEmpty()
            ? resolveBundledPath(QStringLiteral("bin/espeak-ng"))
            : pluginContext->settings().espeakPath;
    if (espeakPath.isEmpty()) {
        blog(LOG_WARNING, "[%s] espeak-ng not found; tried: %s", PLUGIN_NAME,
             bundledPathCandidates(QStringLiteral("bin/espeak-ng"))
                 .join(QStringLiteral(", "))
                 .toUtf8()
                 .constData());
    } else {
        blog(LOG_INFO, "[%s] espeak-ng: %s", PLUGIN_NAME,
             espeakPath.toUtf8().constData());
    }

    const QString espeakData = resolveBundledPath(QStringLiteral("espeak-ng-data"));
    if (espeakData.isEmpty()) {
        blog(LOG_WARNING, "[%s] espeak-ng data directory not found; using the system default",
             PLUGIN_NAME);
    } else {
        blog(LOG_INFO, "[%s] espeak-ng data: %s", PLUGIN_NAME,
             espeakData.toUtf8().constData());
    }

    QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
    auto *dockWidget = new YtChatTtsDock(pluginContext, mainWindow);

    if (obs_frontend_add_dock_by_id(PLUGIN_DOCK_ID, obs_module_text("Dock.Title"), dockWidget)) {
        unregisteredDock = nullptr;
    } else {
        unregisteredDock = dockWidget;
        blog(LOG_WARNING, "[%s] dock '%s' could not be registered by the frontend", PLUGIN_NAME,
             PLUGIN_DOCK_ID);
    }

    return true;
}

void obs_module_unload(void)
{
    /* Stop the chat helper and the TTS worker before any widget can reference
     * the context, then destroy the dock, then the context.  This ordering is
     * what guarantees the dock never sees a dangling context and no helper or
     * espeak-ng child outlives the module. */
    if (pluginContext) {
        pluginContext->disconnectChat();
        blog(LOG_INFO, "[%s] chat helper and TTS worker stopped", PLUGIN_NAME);
    }

    /* Deletes the OBSDock wrapper and our widget when the frontend still has
     * the dock registered; a harmless no-op after the frontend API shut down
     * (the callbacks guard it and OBS cleans up the window tree itself). */
    obs_frontend_remove_dock(PLUGIN_DOCK_ID);

    if (unregisteredDock) {
        delete unregisteredDock;
        unregisteredDock = nullptr;
    }

    delete pluginContext;
    pluginContext = nullptr;

    blog(LOG_INFO, "[%s] unloaded", PLUGIN_NAME);
}
