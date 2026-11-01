/* The session between launches (see session.h) */
#include "session.h"
#include "app_events.h"
#include "editor_session.h"
#include "layout_store.h"
#include "session_store.h"
#include "terminal.h"
#include "workspace.h"

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

/* The panels of a workspace, its split sizes and terminal panes */
static SavedLayout* capture_panels(const VirtualLayout* ws) {
    SavedLayout* l = saved_layout_new("Session");
    l->files_vis = ws->files_vis;
    l->preview_vis = ws->preview_vis;
    l->term_vis = ws->term_vis;
    l->editor_dedicated = ws->editor_dedicated;
    l->stacked = (ws->right_orient == GTK_ORIENTATION_VERTICAL);
    l->main_frac = ws->main_frac;
    l->right_frac = ws->right_frac;
    l->editor_frac = ws->editor_frac;
#ifdef HAVE_VTE
    g_free(l->terminals);
    l->terminals = term_layout_capture_box(ws->term_box);
#endif
    return l;
}

/* Give the current workspace (its editor tabs reopened) these panels */
static void restore_panels(AppData* app, const SavedLayout* l) {
    VirtualLayout* ws = &app->workspaces[app->current_ws];
    ws->files_vis = l->files_vis;
    ws->preview_vis = l->preview_vis;
    ws->term_vis = l->term_vis;
    ws->right_orient = l->stacked ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;
    ws->main_frac = l->main_frac;
    ws->right_frac = l->right_frac;
    ws->editor_frac = l->editor_frac;
    if (!ws->files_vis && !ws->preview_vis && !ws->term_vis && !ws->editor_dedicated) {
        ws->preview_vis = TRUE; /* never leave the window empty */
    }
    workspace_switch(app, app->current_ws); /* re-applies the current workspace */
#ifdef HAVE_VTE
    /* New shells in the saved panes (the switch opened one) */
    TermLayoutNode* tree = l->term_vis ? term_layout_parse(l->terminals) : NULL;
    if (term_layout_count(tree) > 1) {
        term_layout_apply(app, l->terminals);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    }
    term_layout_free(tree);
#endif
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
        GPtrArray* tabs = g_ptr_array_new_with_free_func(g_free);
        gchar* preview_tabs = NULL;
        gchar* editor_tabs = NULL;
        editor_session_capture(app, i, tabs, &preview_tabs, &editor_tabs);
        g_ptr_array_add(tabs, NULL);
        /* The bookmarks, notes and remote views keep no file selection */
        const gchar* selected = (ws->sidebar_view == SIDEBAR_VIEW_FILES && !ws->show_bookmarks)
                                    ? ws->selected_path : NULL;
        SessionWorkspace* sw = session_add_workspace(s, ws->base_dir, (const gchar* const*)expanded, selected,
                                                     ws->show_hidden, ws->sort_mode,
                                                     (const gchar* const*)tabs->pdata);
        sw->preview_tabs = preview_tabs;
        sw->editor_tabs = editor_tabs;
        sw->panels = capture_panels(ws);
        if (i == app->current_ws) {
            s->current = (gint)s->workspaces->len - 1;
        }
        g_strfreev(expanded);
        g_ptr_array_unref(tabs);
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
        editor_session_restore(app, (const gchar* const*)sw->tabs, sw->preview_tabs, sw->editor_tabs);
        if (sw->panels != NULL) {
            restore_panels(app, sw->panels);
        }
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
