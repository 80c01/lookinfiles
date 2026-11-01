/* The session between launches (see session.h) */
#include "session.h"
#include "app_events.h"
#include "editor.h"
#include "session_store.h"
#include "workspace.h"

/* Files of the editor tabs (not the notes' buffers), in order */
static gchar** tab_paths(GList* editors) {
    GPtrArray* out = g_ptr_array_new();
    for (GList* l = editors; l != NULL; l = l->next) {
        EditorTab* tab = l->data;
        gboolean listed = FALSE; /* the views of a split file: once */
        for (guint i = 0; tab != NULL && i < out->len && !listed; i++) {
            listed = g_strcmp0(g_ptr_array_index(out, i), tab->path) == 0;
        }
        if (tab != NULL && !listed && !tab->is_buffer && tab->path != NULL && g_path_is_absolute(tab->path)) {
            g_ptr_array_add(out, g_strdup(tab->path));
        }
    }
    g_ptr_array_add(out, NULL);
    return (gchar**)g_ptr_array_free(out, FALSE);
}

static gchar** set_to_strv(GHashTable* set) {
    GPtrArray* out = g_ptr_array_new();
    if (set != NULL) {
        GHashTableIter it;
        gpointer key;
        g_hash_table_iter_init(&it, set);
        while (g_hash_table_iter_next(&it, &key, NULL)) {
            g_ptr_array_add(out, g_strdup(key));
        }
    }
    g_ptr_array_sort_values(out, (GCompareFunc)g_strcmp0);
    g_ptr_array_add(out, NULL);
    return (gchar**)g_ptr_array_free(out, FALSE);
}

void session_save_now(AppData* app) {
    if (app == NULL || app->num_workspaces <= 0) {
        return;
    }
    app_emit(app, APP_EVENT_LAYOUT_CHANGED); /* snapshot the current workspace */
    Session* s = session_new();
    for (gint i = 0; i < app->num_workspaces; i++) {
        VirtualLayout* ws = &app->workspaces[i];
        if (ws->base_dir == NULL) {
            continue;
        }
        gchar** expanded = set_to_strv(ws->expanded_paths);
        gchar** tabs = tab_paths(i == app->current_ws ? app->editors : ws->editors);
        /* The bookmarks, notes and remote views keep no file selection */
        const gchar* selected = (ws->sidebar_view == SIDEBAR_VIEW_FILES && !ws->show_bookmarks)
                                    ? ws->selected_path : NULL;
        session_add_workspace(s, ws->base_dir, (const gchar* const*)expanded, selected, ws->show_hidden,
                              ws->sort_mode, (const gchar* const*)tabs);
        if (i == app->current_ws) {
            s->current = (gint)s->workspaces->len - 1;
        }
        g_strfreev(expanded);
        g_strfreev(tabs);
    }
    gchar* path = session_default_path();
    GError* err = NULL;
    if (s->workspaces->len > 0 && !session_save(s, path, &err)) {
        g_printerr("[ Could not save the session to %s: %s ]\n", path, err != NULL ? err->message : "?");
    }
    g_clear_error(&err);
    g_free(path);
    session_free(s);
}

static void open_tabs(AppData* app, gchar** tabs) {
    for (gchar** p = tabs; p != NULL && *p != NULL; p++) {
        if (g_file_test(*p, G_FILE_TEST_IS_REGULAR)) {
            gchar* name = g_path_get_basename(*p);
            editor_open_file(app, name, *p);
            g_free(name);
        }
    }
}

gboolean session_restore(AppData* app) {
    if (app == NULL) {
        return FALSE;
    }
    gchar* path = session_default_path();
    Session* s = session_load(path);
    g_free(path);
    if (s == NULL) {
        return FALSE;
    }
    gint shown = -1;
    gint current = 0;
    for (guint i = 0; i < s->workspaces->len && (gint)i < MAX_VIRTUAL_LAYOUTS; i++) {
        SessionWorkspace* sw = g_ptr_array_index(s->workspaces, i);
        if (!g_file_test(sw->dir, G_FILE_TEST_IS_DIR)) {
            continue; /* folder gone, or a remote machine no longer mounted */
        }
        if (shown >= 0) {
            workspace_add(app);
        }
        shown++;
        if ((gint)i == s->current) {
            current = app->current_ws;
        }
        const gchar* selected = (sw->selected != NULL && g_file_test(sw->selected, G_FILE_TEST_EXISTS))
                                    ? sw->selected : NULL;
        workspace_restore_state(app, app->current_ws, sw->dir, (const gchar* const*)sw->expanded, selected,
                                sw->show_hidden, sw->sort_mode);
        open_tabs(app, sw->tabs);
    }
    session_free(s);
    if (shown < 0) {
        return FALSE;
    }
    if (current != app->current_ws) {
        workspace_switch(app, current);
    } else if (app->tree_view != NULL && gtk_widget_get_visible(app->left_vbox)) {
        gtk_widget_grab_focus(app->tree_view); /* the tabs took it */
    }
    return TRUE;
}
