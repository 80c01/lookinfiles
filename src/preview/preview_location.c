/* Path of the value at the cursor of a data preview (see preview_location.h). */
#include "preview/preview_location.h"
#include "preview/preview_state.h"
#include "core/app_events.h"
#include "core/i18n.h"

struct PreviewLocation {
    gchar* shown_path;      /* file whose paths are in shown */
    PreviewDoc* shown;      /* path points of the preview on screen (no text) */
    gchar* current;         /* path at the cursor, or NULL */
    GtkTextBuffer* tracked; /* buffer whose cursor is followed */
};

static struct PreviewLocation* location_get(AppData* app) {
    if (app->preview_location == NULL) {
        app->preview_location = g_new0(struct PreviewLocation, 1);
    }
    return app->preview_location;
}

/* Set the path at the cursor; the preview bar follows */
static void set_current(AppData* app, struct PreviewLocation* l, const gchar* path) {
    if (g_strcmp0(path, l->current) == 0) {
        return;
    }
    g_free(l->current);
    l->current = g_strdup(path);
    app_emit(app, APP_EVENT_PREVIEW_LOCATION);
}

static void update_current(AppData* app) {
    struct PreviewLocation* l = app->preview_location;
    if (l == NULL || l->shown == NULL) {
        return;
    }
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_mark(app->buffer, &iter, gtk_text_buffer_get_insert(app->buffer));
    set_current(app, l, preview_doc_path_at(l->shown, gtk_text_iter_get_offset(&iter)));
}

static void on_cursor_moved(GObject* buffer, GParamSpec* pspec, gpointer data) {
    (void)buffer;
    (void)pspec;
    update_current((AppData*)data);
}

void preview_location_shown(AppData* app, const PreviewDoc* doc) {
    struct PreviewLocation* l = location_get(app);
    g_clear_pointer(&l->shown, preview_doc_free);
    g_clear_pointer(&l->shown_path, g_free);
    if (doc == NULL || doc->paths->len == 0 || preview_state_path(app) == NULL) {
        set_current(app, l, NULL);
        return;
    }
    l->shown_path = g_strdup(preview_state_path(app));
    l->shown = preview_doc_new();
    for (guint i = 0; i < doc->paths->len; i++) {
        const PreviewPathPoint* point = &g_array_index(doc->paths, PreviewPathPoint, i);
        preview_doc_add_path(l->shown, point->start, point->path);
    }
    if (l->tracked != app->buffer) {
        if (l->tracked != NULL) {
            g_signal_handlers_disconnect_by_func(l->tracked, G_CALLBACK(on_cursor_moved), app);
            g_object_remove_weak_pointer(G_OBJECT(l->tracked), (gpointer*)&l->tracked);
        }
        l->tracked = app->buffer;
        g_object_add_weak_pointer(G_OBJECT(l->tracked), (gpointer*)&l->tracked);
        g_signal_connect(app->buffer, "notify::cursor-position", G_CALLBACK(on_cursor_moved), app);
    }
    update_current(app);
}

const gchar* preview_location_current(AppData* app) {
    struct PreviewLocation* l = app != NULL ? app->preview_location : NULL;
    if (l == NULL || l->shown == NULL || preview_state_raw(app) ||
        g_strcmp0(l->shown_path, preview_state_path(app)) != 0) {
        return NULL;
    }
    return l->current;
}

gboolean preview_location_copy(AppData* app) {
    const gchar* path = preview_location_current(app);
    if (path == NULL) {
        return FALSE;
    }
    gdk_clipboard_set_text(gtk_widget_get_clipboard(app->text_view), path);
    /* TRANSLATORS: %s is the path of a JSON value ("$.servers[2].host") */
    gchar* notice = g_strdup_printf(_("Copied %s"), path);
    app_notice(app, notice, FALSE);
    g_free(notice);
    return TRUE;
}

gboolean preview_location_key(AppData* app, guint keyval, GdkModifierType state) {
    if (keyval != GDK_KEY_y || (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK)) != 0) {
        return FALSE;
    }
    return preview_location_copy(app);
}

void preview_location_free(AppData* app) {
    struct PreviewLocation* l = app != NULL ? app->preview_location : NULL;
    if (l == NULL) {
        return;
    }
    if (l->tracked != NULL) {
        g_signal_handlers_disconnect_by_func(l->tracked, G_CALLBACK(on_cursor_moved), app);
        g_object_remove_weak_pointer(G_OBJECT(l->tracked), (gpointer*)&l->tracked);
    }
    preview_doc_free(l->shown);
    g_free(l->shown_path);
    g_free(l->current);
    g_clear_pointer(&app->preview_location, g_free);
}
