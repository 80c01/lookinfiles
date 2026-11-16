/* Copying a file's name or path (see path_copy.h) */
#include "core/path_copy.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "git/git_utils.h"

gboolean path_copy_has_relative(const gchar* path) {
    gchar* rel = git_relative_path(path);
    gboolean found = (rel != NULL);
    g_free(rel);
    return found;
}

void path_copy(AppData* app, const gchar* path, PathCopyKind kind) {
    if (app == NULL || path == NULL || path[0] == '\0') {
        return;
    }
    gchar* text = NULL;
    switch (kind) {
    case PATH_COPY_NAME:     text = g_path_get_basename(path); break;
    case PATH_COPY_RELATIVE: text = git_relative_path(path); break;
    case PATH_COPY_ABSOLUTE: text = g_strdup(path); break;
    }
    if (text == NULL) {
        return;
    }
    gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(app->window)), text);
    gchar* msg = g_strdup_printf(_("Copied “%s”"), text);
    app_notice(app, msg, FALSE);
    g_free(msg);
    g_free(text);
}
