#include <obs-frontend-api.h>
#include <obs-module.h>
#include <QWidget>

#include "version.h"
#include "yt-chat-tts-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

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

    QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
    auto *dockWidget = new YtChatTtsDock(mainWindow);

    if (obs_frontend_add_dock_by_id(PLUGIN_DOCK_ID, obs_module_text("Dock.Title"), dockWidget)) {
        unregisteredDock = nullptr;
    } else {
        unregisteredDock = dockWidget;
        blog(LOG_WARNING, "[%s] dock '%s' could not be registered by the frontend", PLUGIN_NAME, PLUGIN_DOCK_ID);
    }

    return true;
}

void obs_module_unload(void)
{
    /* Deletes the OBSDock wrapper and our widget when the frontend still has
     * the dock registered; a harmless no-op after the frontend API shut down
     * (the callbacks guard it and OBS cleans up the window tree itself). */
    obs_frontend_remove_dock(PLUGIN_DOCK_ID);

    if (unregisteredDock) {
        delete unregisteredDock;
        unregisteredDock = nullptr;
    }
}
