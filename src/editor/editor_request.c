/* Files opened in an editor tab through the event bus (see editor_request.h) */
#include "editor/editor_request.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "editor/editor.h"

void editor_edit_requested(AppData* app) {
    if (app == NULL) return;
    gchar* path = g_steal_pointer(&app->edit_request_path);
    if (path == NULL) return;
    if (editor_open_at(app, path, 0, 0) == NULL) {
        gchar* name = g_path_get_basename(path);
        gchar* msg = g_strdup_printf(_("Cannot edit “%s”"), name);
        app_notice(app, msg, FALSE);
        g_free(msg);
        g_free(name);
    }
    g_free(path);
}
