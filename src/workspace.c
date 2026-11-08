/* Virtual workspaces (Alt+1..8): per-workspace layout snapshots and the top-bar workspace minimap. */
#include "workspace.h"
#include "pdf.h"
#include "workspace_menu.h"
#include "nav_history.h"
#include "fs_watch.h"
#include "panels.h"
#include "app_events.h"
#include "tree_nav.h"
#include "audio.h"
#include "editor.h"
#include "file_list.h"
#include "minimap.h"
#include "terminal.h"
#include "video.h"
#include "layout_store.h"
#include "palette.h"
#include "project_search.h"
#include "help_guide.h"
#include "browser.h"
#include "canvas_view.h"
#include "video_tab.h"
#include "games.h"

#define WS_CELL_W   32
#define WS_ADD_W    22
#define WS_CELL_H   20
#define WS_CELL_GAP 5
#define WS_PAD_X    2
#define WS_PAD_Y    2

static GHashTable* clone_ws_paths(GHashTable* src) {
    GHashTable* copy = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (src != NULL) {
        GHashTableIter iter;
        gpointer key;
        g_hash_table_iter_init(&iter, src);
        while (g_hash_table_iter_next(&iter, &key, NULL)) {
            g_hash_table_add(copy, g_strdup((const gchar*)key));
        }
    }
    return copy;
}

static gboolean ws_paths_equal(GHashTable* a, GHashTable* b) {
    if (a == b) {
        return TRUE;
    }
    if (a == NULL || b == NULL) {
        return FALSE;
    }
    if (g_hash_table_size(a) != g_hash_table_size(b)) {
        return FALSE;
    }
    GHashTableIter iter;
    gpointer key;
    g_hash_table_iter_init(&iter, a);
    while (g_hash_table_iter_next(&iter, &key, NULL)) {
        if (!g_hash_table_contains(b, key)) {
            return FALSE;
        }
    }
    return TRUE;
}

static void ws_minimap_update_size(AppData* app) {
    if (app == NULL || app->ws_minimap == NULL) {
        return;
    }
    gboolean has_add = (app->num_workspaces < MAX_VIRTUAL_LAYOUTS);
    gint total_w = WS_PAD_X * 2 + app->num_workspaces * WS_CELL_W +
                   (app->num_workspaces - 1) * WS_CELL_GAP +
                   (has_add ? (WS_CELL_GAP + WS_ADD_W) : 0);
    gint total_h = WS_CELL_H + WS_PAD_Y * 2;
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(app->ws_minimap), total_w);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(app->ws_minimap), total_h);
    gtk_widget_queue_draw(app->ws_minimap);
}

/* Default view of a workspace: files view of `dir` (hidden files off, sorted
 * by name), explorer and preview shown, no terminal, no editor */
static void init_workspace_slot(AppData* app, gint idx, const gchar* dir) {
    VirtualLayout* ws = &app->workspaces[idx];
    memset(ws, 0, sizeof(VirtualLayout));
    ws->files_vis = TRUE;
    ws->preview_vis = TRUE;
    ws->term_vis = FALSE;
    ws->right_orient = GTK_ORIENTATION_HORIZONTAL;
    ws->main_frac = 0.25;
    ws->right_frac = 0.70;
    ws->base_dir = g_strdup(dir != NULL ? dir : app->base_dir);
    ws->expanded_paths = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    ws->selected_path = NULL;
    ws->show_hidden = FALSE;
    ws->show_bookmarks = FALSE;
    ws->sidebar_view = SIDEBAR_VIEW_FILES;
    ws->sort_mode = SORT_NAME;
    ws->notebook_page = 0;
    ws->editors = NULL;
    ws->editor_dedicated = FALSE;
    ws->editor_frac = 0.50;
    ws->render_raw = FALSE;
#ifdef HAVE_VTE
    if (app->term_stack != NULL) {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_hexpand(box, TRUE);
        gtk_widget_set_vexpand(box, TRUE);
        gtk_stack_add_child(GTK_STACK(app->term_stack), box);
        ws->term_box = box;
    }
    ws->last_focused_vte = NULL;
#endif
}

void workspaces_init(AppData* app) {
    app->num_workspaces = 1;
    app->current_ws = 0;
    app->hover_ws = -1;
    app->switching_ws = FALSE;
    app->applying_layout = FALSE;
    app->dir_scan_pending = FALSE;
    app->layout_tick_id = 0;
    app->layout_apply_frames = 0;
    app->layout_retry_count = 0;
    init_workspace_slot(app, 0, app->base_dir);
#ifdef HAVE_VTE
    app->term_box = app->workspaces[0].term_box;
    if (app->term_stack != NULL && app->term_box != NULL) {
        gtk_stack_set_visible_child(GTK_STACK(app->term_stack), app->term_box);
    }
#endif
}

void workspace_save_current(AppData* app) {
    if (app == NULL || app->switching_ws ||
        app->current_ws < 0 || app->current_ws >= app->num_workspaces ||
        app->left_vbox == NULL || app->viewer_box == NULL || app->term_scroll == NULL) {
        return;
    }
    VirtualLayout* ws = &app->workspaces[app->current_ws];
    ws->files_vis = gtk_widget_get_visible(app->left_vbox);
    ws->preview_vis = gtk_widget_get_visible(app->viewer_box);
    ws->term_vis = gtk_widget_get_visible(app->term_scroll);
    ws->show_hidden = app->show_hidden;
    ws->show_bookmarks = app->show_bookmarks;
    ws->sidebar_view = app->sidebar_view;
    ws->games_unlocked = app->game_mode_unlocked;
    ws->sort_mode = app->sort_mode;
    ws->render_raw = app->render_raw;
    ws->editor_dedicated = app->editor_dedicated;
    ws->editor_frac = app->editor_frac;
    if (app->editor_dedicated && app->editor_notebook != NULL) {
        gint cur_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->editor_notebook));
        if (cur_page >= 0) {
            ws->notebook_page = cur_page;
        }
    } else if (app->notebook != NULL) {
        gint cur_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
        if (cur_page >= 0) {
            ws->notebook_page = cur_page;
        }
    }
    ws->editors = app->editors;

    g_free(ws->remote_id);
    ws->remote_id = g_strdup(app->current_remote_id);

    g_free(ws->base_dir);
    ws->base_dir = g_strdup(app->base_dir);

    if (ws->expanded_paths != NULL) {
        g_hash_table_destroy(ws->expanded_paths);
    }
    ws->expanded_paths = clone_ws_paths(app->expanded_paths);

    if (app->tree != NULL && !app->dir_scan_pending) {
        ExplorerRow* row = explorer_tree_get_selected(app->tree);
        g_free(ws->selected_path);
        ws->selected_path = (row != NULL) ? g_strdup(row->path) : NULL;
    }

#ifdef HAVE_VTE
    ws->last_focused_vte = app->last_focused_vte;
#endif

    if (app->right_paned != NULL) {
        ws->right_orient =
            gtk_orientable_get_orientation(GTK_ORIENTABLE(app->right_paned));
    }

    if (app->ws_minimap != NULL) {
        gtk_widget_queue_draw(app->ws_minimap);
    }
}

/* The splits of the terminal panes and of the editor panel keep their share */
static void apply_paned_fractions_recursive(GtkWidget* widget) {
    if (widget == NULL || !gtk_widget_get_visible(widget)) {
        return;
    }
    if (GTK_IS_PANED(widget)) {
        gdouble* frac = (gdouble*)g_object_get_data(G_OBJECT(widget), "term-paned-frac");
        if (frac != NULL) {
            GtkOrientation o = gtk_orientable_get_orientation(GTK_ORIENTABLE(widget));
            gint len = (o == GTK_ORIENTATION_VERTICAL)
                           ? gtk_widget_get_height(widget)
                           : gtk_widget_get_width(widget);
            if (len > 40) {
                gtk_paned_set_position(GTK_PANED(widget),
                                       (gint)((*frac) * len + 0.5));
            }
        }
    }
    for (GtkWidget* child = gtk_widget_get_first_child(widget);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        apply_paned_fractions_recursive(child);
    }
}

/* Width of a paned's separator along its orientation (set by the theme CSS);
 * measured, so it is right even before the paned is allocated */
static gint paned_handle_size(GtkWidget* paned) {
    GtkOrientation o = gtk_orientable_get_orientation(GTK_ORIENTABLE(paned));
    for (GtkWidget* child = gtk_widget_get_first_child(paned);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        if (g_strcmp0(gtk_widget_get_css_name(child), "separator") == 0) {
            gint min = 0;
            gtk_widget_measure(child, o, -1, &min, NULL, NULL, NULL);
            return min;
        }
    }
    return 0;
}

static gboolean apply_ws_positions_now(AppData* app) {
    if (app == NULL || app->current_ws < 0 || app->current_ws >= app->num_workspaces) {
        return TRUE;
    }
    VirtualLayout* ws = &app->workspaces[app->current_ws];
    gboolean ready = TRUE;
    gint mw = (app->main_paned != NULL) ? gtk_widget_get_width(app->main_paned) : 0;
    gint mh = (app->main_paned != NULL) ? gtk_widget_get_height(app->main_paned) : 0;
    gint total_w = get_workspace_total_width(app);
    if (total_w <= 0) {
        total_w = mw;
    }

    gint target_file_w = (gint)(ws->main_frac * total_w + 0.5);
    if (target_file_w > mw - 40 && mw > 80) {
        target_file_w = mw - 40;
    }
    /* Width the right side gets next to the explorer: computed rather than
     * read, since its allocation is the previous workspace's until the next
     * layout (a wrong guess makes the split jump on the second frame) */
    gint right_w = mw;
    if (ws->files_vis && mw > 60 && app->main_paned != NULL) {
        right_w = mw - target_file_w - paned_handle_size(app->main_paned);
    }

    if (app->main_paned != NULL && ws->files_vis && (ws->preview_vis || ws->editor_dedicated || ws->term_vis)) {
        if (mw > 60) {
            gtk_paned_set_position(GTK_PANED(app->main_paned), target_file_w);
        } else {
            ready = FALSE;
        }
    }

    if (app->editor_paned != NULL && ws->preview_vis && ws->editor_dedicated) {
        gint ew = gtk_widget_get_width(app->editor_paned);
        if (ew <= 40) {
            if (app->right_paned != NULL && ws->term_vis && ws->right_orient == GTK_ORIENTATION_HORIZONTAL) {
                gint r_pos = gtk_paned_get_position(GTK_PANED(app->right_paned));
                if (r_pos > 40) {
                    ew = r_pos;
                }
            } else if (mw > 40) {
                ew = right_w;
            }
        }
        if (ew > 40) {
            gtk_paned_set_position(GTK_PANED(app->editor_paned),
                                   (gint)(ws->editor_frac * ew + 0.5));
        } else {
            ready = FALSE;
        }
    }

    if (app->right_paned != NULL && (ws->preview_vis || ws->editor_dedicated || ws->term_vis)) {
        gtk_paned_set_resize_start_child(GTK_PANED(app->right_paned), FALSE);
        gtk_paned_set_resize_end_child(GTK_PANED(app->right_paned), TRUE);

        gint len = (ws->right_orient == GTK_ORIENTATION_VERTICAL)
                       ? gtk_widget_get_height(app->right_paned)
                       : gtk_widget_get_width(app->right_paned);
        if (ws->right_orient == GTK_ORIENTATION_HORIZONTAL) {
            if (app->layout_apply_frames <= 1 || len <= 40) {
                len = right_w;
            }
        } else if (len <= 40 && mh > 40) {
            len = mh;
        }

        if (len > 40) {
            gtk_paned_set_position(GTK_PANED(app->right_paned),
                                   (gint)(ws->right_frac * len + 0.5));
        } else {
            ready = FALSE;
        }
    }

#ifdef HAVE_VTE
    if (ws->term_vis && ws->term_box != NULL) {
        apply_paned_fractions_recursive(ws->term_box);
    }
#endif
    if (ws->editor_dedicated && app->editor_box != NULL) {
        apply_paned_fractions_recursive(app->editor_box);
    }
    return ready;
}

static gboolean workspace_apply_positions_tick(GtkWidget* widget,
                                               GdkFrameClock* frame_clock,
                                               gpointer data) {
    (void)widget;
    (void)frame_clock;
    AppData* app = (AppData*)data;
    app->layout_apply_frames++;

    if (app->layout_apply_frames <= 3) {
        gboolean ready = apply_ws_positions_now(app);
        if (!ready && app->layout_apply_frames == 2 && app->layout_retry_count < 10) {
            app->layout_retry_count++;
            app->layout_apply_frames = 1; /* wait until widgets are allocated */
        }
        return G_SOURCE_CONTINUE;
    }

    app->applying_layout = FALSE;
    app->layout_tick_id = 0;
    pdf_preview_refit(app); /* the pane sizes are final now */
    if (app->ws_minimap != NULL) {
        gtk_widget_queue_draw(app->ws_minimap);
    }
    return G_SOURCE_REMOVE;
}

void workspace_apply_positions(AppData* app) {
    if (app == NULL || app->main_paned == NULL) {
        return;
    }
    app->applying_layout = TRUE;
    app->layout_apply_frames = 0;
    app->layout_retry_count = 0;
    apply_ws_positions_now(app);
    if (app->layout_tick_id == 0) {
        app->layout_tick_id = gtk_widget_add_tick_callback(
            app->main_paned, workspace_apply_positions_tick, app, NULL);
    }
}

/* Put the explorer rows (and git badges) of the current workspace aside and
 * show those of workspace idx. FALSE when idx had none: the tree is left empty. */
static gboolean swap_explorer_rows(AppData* app, gint idx) {
    VirtualLayout* to = &app->workspaces[idx];
    VirtualLayout* from = (app->current_ws >= 0 && app->current_ws < app->num_workspaces)
                              ? &app->workspaces[app->current_ws]
                              : NULL;
    /* Rows still waiting for their listing are not worth keeping */
    gboolean keep = (from != NULL && !app->dir_scan_pending);
    cancel_listing_scans(app);

    gboolean had_rows = (to->tree_state != NULL);
    ExplorerTreeState* old = explorer_tree_swap_state(app->tree, to->tree_state);
    to->tree_state = NULL;
    GHashTable* old_git = app->git_statuses;
    app->git_statuses = to->git_statuses;
    to->git_statuses = NULL;
    gchar* old_root = app->git_status_root;
    app->git_status_root = to->git_status_root;
    to->git_status_root = NULL;
    gboolean old_has_changes = app->git_has_changes;
    app->git_has_changes = to->git_has_changes;
    to->git_has_changes = FALSE;
    gboolean old_has_upstream = app->git_has_upstream;
    app->git_has_upstream = to->git_has_upstream;
    to->git_has_upstream = FALSE;
    guint old_ahead = app->git_ahead;
    app->git_ahead = to->git_ahead;
    to->git_ahead = 0;
    guint old_behind = app->git_behind;
    app->git_behind = to->git_behind;
    to->git_behind = 0;

    if (keep) {
        from->tree_state = old;
        from->git_statuses = old_git;
        from->git_status_root = old_root;
        from->git_has_changes = old_has_changes;
        from->git_has_upstream = old_has_upstream;
        from->git_ahead = old_ahead;
        from->git_behind = old_behind;
    } else {
        explorer_tree_state_free(old);
        if (old_git != NULL) {
            g_hash_table_destroy(old_git);
        }
        g_free(old_root);
    }
    return had_rows;
}

/* Show the panels of workspace ws as it records them: visibility and the
 * preview/terminal orientation (a shown terminal panel gets a terminal) */
static void show_ws_panels(AppData* app, const VirtualLayout* ws) {
    if (app->left_vbox != NULL) {
        gtk_widget_set_visible(app->left_vbox, ws->files_vis);
    }
    if (app->viewer_box != NULL) {
        gtk_widget_set_visible(app->viewer_box, ws->preview_vis);
    }
    if (app->editor_box != NULL) {
        gtk_widget_set_visible(app->editor_box, ws->editor_dedicated);
    }
    if (app->right_paned != NULL) {
        gtk_orientable_set_orientation(GTK_ORIENTABLE(app->right_paned), ws->right_orient);
        gtk_widget_set_visible(app->right_paned, ws->preview_vis || ws->editor_dedicated || ws->term_vis);
    }
    if (app->term_scroll != NULL) {
        gtk_widget_set_visible(app->term_scroll, ws->term_vis);
    }

#ifdef HAVE_VTE
    if (ws->term_vis && ws->term_box != NULL) {
        GPtrArray* panes = term_visible_panes(app);
        if (panes->len == 0) {
            term_spawn_pane(app, FALSE);
        }
        g_ptr_array_unref(panes);
    }
#endif
}

void workspace_switch(AppData* app, gint idx) {
    if (app == NULL || idx < 0 || idx >= app->num_workspaces) {
        return;
    }
    gboolean ws_changed = (idx != app->current_ws);
    gboolean restored_rows = FALSE;
    if (ws_changed && app->current_ws >= 0) {
        workspace_save_current(app);
    }
    app->switching_ws = TRUE;
    app->applying_layout = TRUE;

    if (ws_changed) {
        audio_preview_stop(app);
        video_preview_stop(app);
        GtkWidget* panel = editor_detach_tabs(app);
        if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
            app->workspaces[app->current_ws].editor_panel = panel;
            project_search_workspace_hidden(app, &app->workspaces[app->current_ws]);
            help_guide_workspace_hidden(app, &app->workspaces[app->current_ws]);
            browser_workspace_hidden(app, &app->workspaces[app->current_ws]);
            canvas_view_workspace_hidden(app, &app->workspaces[app->current_ws]);
            video_tab_workspace_hidden(app, &app->workspaces[app->current_ws]);
            games_workspace_hidden(app, &app->workspaces[app->current_ws]);
#ifdef HAVE_VTE
            terminal_workspace_hidden(app, &app->workspaces[app->current_ws]);
#endif
        } else if (panel != NULL) {
            g_object_unref(panel);
        }
        restored_rows = swap_explorer_rows(app, idx);
    }

    app->current_ws = idx;

    VirtualLayout* ws = &app->workspaces[idx];
    gchar* target_path = g_strdup(ws->selected_path);
    app->right_frac = ws->right_frac;
    app->editor_dedicated = ws->editor_dedicated;
    app->editor_frac = ws->editor_frac;

    if (ws_changed) {
        GtkWidget* panel = ws->editor_panel;
        ws->editor_panel = NULL;
        editor_attach_tabs(app, ws->editors, panel);
        project_search_workspace_shown(app, ws);
        help_guide_workspace_shown(app, ws);
        browser_workspace_shown(app, ws);
        canvas_view_workspace_shown(app, ws);
        video_tab_workspace_shown(app, ws);
        games_workspace_shown(app, ws);
#ifdef HAVE_VTE
        terminal_workspace_shown(app, ws);
#endif
    }

#ifdef HAVE_VTE
    app->term_box = ws->term_box;
    app->last_focused_vte = ws->last_focused_vte;
    if (app->term_stack != NULL && ws->term_box != NULL) {
        gtk_stack_set_visible_child(GTK_STACK(app->term_stack), ws->term_box);
    }
#endif

    show_ws_panels(app, ws);

    gboolean need_rescan = app->dir_scan_pending ||
                           (g_strcmp0(app->base_dir, ws->base_dir) != 0) ||
                           (app->show_hidden != ws->show_hidden) ||
                           (app->show_bookmarks != ws->show_bookmarks) ||
                           (app->sidebar_view != ws->sidebar_view) ||
                           (app->sort_mode != ws->sort_mode) ||
                           !ws_paths_equal(app->expanded_paths, ws->expanded_paths);

    app->show_hidden = ws->show_hidden;
    app->show_bookmarks = ws->show_bookmarks;
    app->sidebar_view = ws->sidebar_view;
    app->game_mode_unlocked = ws->games_unlocked;
    app->game_mode_just_unlocked = FALSE;
    app->sort_mode = ws->sort_mode;
    g_free(app->current_remote_id);
    app->current_remote_id = g_strdup(ws->remote_id);
    if (ws->base_dir != NULL && g_strcmp0(app->base_dir, ws->base_dir) != 0) {
        g_free(app->base_dir);
        app->base_dir = g_strdup(ws->base_dir);
    }
    if (app->expanded_paths != NULL) {
        g_hash_table_destroy(app->expanded_paths);
    }
    app->expanded_paths = clone_ws_paths(ws->expanded_paths);
    sync_directory_monitors(app);

    if (app->render_raw != ws->render_raw) {
        app->render_raw = ws->render_raw;
        app->file_dirty = TRUE;
    }
    if (app->editor_dedicated) {
        /* The dedicated panel's notebooks kept their own current tab */
        if (app->notebook != NULL) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
        }
    } else if (app->notebook != NULL) {
        gint n_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
        gint target_page = (ws->notebook_page >= 0 && ws->notebook_page < n_pages)
                               ? ws->notebook_page
                               : 0;
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), target_page);
    }

    app_emit(app, APP_EVENT_CHROME_CHANGED);

    gboolean files_view = (app->sidebar_view == SIDEBAR_VIEW_FILES && !app->show_bookmarks);
    if (restored_rows && files_view) {
        /* The rows are this workspace's own, as it was left: preview its
         * selection now, then check the directory quietly in the background */
        gint page = (app->notebook != NULL && !app->editor_dedicated)
                        ? gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook))
                        : -1;
        if (tree_nav_selected(app) != NULL) {
            g_signal_emit_by_name(app->tree, "selection-changed");
        } else if (target_path == NULL || !select_by_path(app, target_path)) {
            select_first_row(app);
        }
        /* The preview update goes back to the Preview tab: keep the
         * workspace's editor tab in front */
        if (page > 0) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), page);
        }
        check_listing(app);
    } else if (need_rescan || ws_changed) {
        refresh_listing_with_path(app, target_path);
        if (app->sidebar_view != SIDEBAR_VIEW_FILES) {
            /* Bookmarks, notes and remote views are rebuilt at once, keeping the
             * row selected before the switch (or none): select this workspace's
             * row instead, which also refreshes the preview */
            gboolean found = (target_path != NULL && target_path[0] != '\0' &&
                              select_by_path(app, target_path));
            if (!found && tree_nav_selected(app) == NULL) {
                select_first_row(app);
            }
        }
    } else if (target_path != NULL) {
        if (!select_by_path(app, target_path)) {
            select_first_row(app);
        }
    } else {
        select_first_row(app);
    }

    minimap_update_visibility(app);
    workspace_apply_positions(app);
    app->switching_ws = FALSE;
    workspace_save_current(app);
    g_free(target_path);

    if (ws->files_vis && app->tree_view != NULL) {
        gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
    } else if (ws->notebook_page > 0 && app->editors != NULL) {
        EditorTab* cur_tab = editor_current_tab(app);
        if (cur_tab != NULL && cur_tab->view != NULL) {
            gtk_widget_grab_focus(cur_tab->view);
        } else if (ws->preview_vis && app->text_view != NULL) {
            gtk_widget_grab_focus(app->text_view);
        }
    } else if (ws->preview_vis && app->text_view != NULL) {
        gtk_widget_grab_focus(app->text_view);
    }
}

void workspace_show_panels(AppData* app) {
    if (app == NULL || app->current_ws < 0 || app->current_ws >= app->num_workspaces) {
        return;
    }
    VirtualLayout* ws = &app->workspaces[app->current_ws];
    app->right_frac = ws->right_frac;
    app->editor_frac = ws->editor_frac;
    app->switching_ws = TRUE;
    show_ws_panels(app, ws);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    minimap_update_visibility(app);
    workspace_apply_positions(app);
    app->switching_ws = FALSE;
    workspace_save_current(app);
}

/* Default layout of new workspaces (layouts.ini, built-in if none). Caller frees. */
static SavedLayout* load_default_layout(void) {
    gchar* path = layout_store_default_path();
    SavedLayout* def = layout_store_load_default(path);
    g_free(path);
    return def;
}

/* Panels of the default layout on a workspace (its folder and view are kept) */
static void set_default_panels(VirtualLayout* ws, const SavedLayout* def) {
    ws->files_vis = def->files_vis;
    ws->preview_vis = def->preview_vis;
    ws->term_vis = def->term_vis;
    ws->right_orient = def->stacked ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;
    ws->main_frac = def->main_frac;
    ws->right_frac = def->right_frac;
    ws->editor_frac = def->editor_frac; /* no editor yet: the dedicated panel opens with them */
    if (!ws->files_vis && !ws->preview_vis && !ws->term_vis) {
        ws->preview_vis = TRUE;
    }
}

/* After the switch to the workspace (which opens one terminal): its terminal panes */
static void apply_default_terminals(AppData* app, const SavedLayout* def) {
#ifdef HAVE_VTE
    TermLayoutNode* tree = def->term_vis ? term_layout_parse(def->terminals) : NULL;
    if (term_layout_count(tree) > 1) {
        term_layout_apply(app, def->terminals);
        workspace_apply_positions(app);
    }
    term_layout_free(tree);
#else
    (void)app;
    (void)def;
#endif
}

void workspace_add(AppData* app) {
    if (app == NULL || app->num_workspaces >= MAX_VIRTUAL_LAYOUTS) {
        return;
    }
    /* Save the current workspace intact before creating the new one */
    workspace_save_current(app);

    /* A new workspace starts from the default view (start directory, files
     * view) with the panels of the default layout, never from a copy of the
     * current workspace */
    gint new_idx = app->num_workspaces;
    init_workspace_slot(app, new_idx, app->start_dir);
    SavedLayout* def = load_default_layout();
    set_default_panels(&app->workspaces[new_idx], def);
    app->num_workspaces++;
    ws_minimap_update_size(app);
    workspace_switch(app, new_idx); /* opens one terminal if term_vis */
    apply_default_terminals(app, def);
    saved_layout_free(def);
}

void workspace_restore_state(AppData* app, gint idx, const gchar* dir, const gchar* const* expanded,
                             const gchar* selected, gboolean show_hidden, gint sort_mode, gint view) {
    if (app == NULL || idx < 0 || idx >= app->num_workspaces || dir == NULL) {
        return;
    }
    if (idx == app->current_ws) {
        workspace_save_current(app); /* keep its panels */
    }
    VirtualLayout* ws = &app->workspaces[idx];
    g_free(ws->base_dir);
    ws->base_dir = g_strdup(dir);
    g_hash_table_remove_all(ws->expanded_paths);
    for (const gchar* const* p = expanded; p != NULL && *p != NULL; p++) {
        if (g_str_has_prefix(*p, dir)) {
            g_hash_table_add(ws->expanded_paths, g_strdup(*p));
        }
    }
    g_free(ws->selected_path);
    ws->selected_path = g_strdup(selected);
    ws->show_hidden = show_hidden;
    ws->sort_mode = (sort_mode >= 0 && sort_mode < SORT_COUNT) ? sort_mode : SORT_NAME;
    /* The games view needs its unlock, never restored */
    gboolean known = (view == SIDEBAR_VIEW_BOOKMARKS || view == SIDEBAR_VIEW_NOTES || view == SIDEBAR_VIEW_REMOTE);
    ws->sidebar_view = known ? view : SIDEBAR_VIEW_FILES;
    ws->show_bookmarks = (ws->sidebar_view == SIDEBAR_VIEW_BOOKMARKS);
    workspace_switch(app, idx); /* lists the folder with that state */
}

void workspace_apply_default_layout(AppData* app) {
    if (app == NULL || app->current_ws < 0 || app->current_ws >= app->num_workspaces) {
        return;
    }
    SavedLayout* def = load_default_layout();
    if (layout_store_is_customized(def)) {
        set_default_panels(&app->workspaces[app->current_ws], def);
        workspace_switch(app, app->current_ws); /* re-applies the current workspace */
        apply_default_terminals(app, def);
    }
    saved_layout_free(def);
}

void workspace_remove(AppData* app, gint idx) {
    if (app == NULL || app->num_workspaces <= 1 ||
        idx < 0 || idx >= app->num_workspaces) {
        return;
    }
    VirtualLayout* rm = &app->workspaces[idx];
    if (rm->editors != NULL && !editor_confirm_discard_workspace(app, rm->editors)) {
        return;
    }

    canvas_view_workspace_removed(app, rm); /* saved before the panel goes */
    video_tab_workspace_removed(app, rm);
    games_workspace_removed(app, rm);
    if (idx == app->current_ws) {
        GtkWidget* panel = editor_detach_tabs(app);
        if (panel != NULL) {
            g_object_unref(panel);
        }
    } else {
        workspace_save_current(app);
    }

    /* The teardown below emits events whose handlers save the current
     * workspace (maybe this one, half freed): no saving until the switch */
    app->switching_ws = TRUE;
    g_clear_pointer(&rm->base_dir, g_free);
    g_clear_pointer(&rm->expanded_paths, g_hash_table_destroy);
    g_clear_pointer(&rm->selected_path, g_free);
    g_clear_pointer(&rm->tree_state, explorer_tree_state_free);
    g_clear_pointer(&rm->git_statuses, g_hash_table_destroy);
    g_clear_pointer(&rm->git_status_root, g_free);
    g_clear_pointer(&rm->remote_id, g_free);
    nav_history_workspace_removed(rm);
    project_search_workspace_removed(app, rm);
    help_guide_workspace_removed(app, rm);
    browser_workspace_removed(app, rm);
#ifdef HAVE_VTE
    terminal_workspace_removed(app, rm);
    if (app->term_stack != NULL && rm->term_box != NULL) {
        gtk_stack_remove(GTK_STACK(app->term_stack), rm->term_box);
    }
#endif

    editor_free_tabs(app, rm->editors, FALSE);
    rm->editors = NULL;
    g_clear_object(&rm->editor_panel);

    for (gint i = idx; i < app->num_workspaces - 1; i++) {
        app->workspaces[i] = app->workspaces[i + 1];
    }
    memset(&app->workspaces[app->num_workspaces - 1], 0, sizeof(VirtualLayout));
    app->num_workspaces--;

    gint target = app->current_ws;
    if (target >= app->num_workspaces) {
        target = app->num_workspaces - 1;
    } else if (idx < app->current_ws) {
        target = app->current_ws - 1;
    }
    app->current_ws = -1;
    app->switching_ws = FALSE;
    ws_minimap_update_size(app);
    workspace_switch(app, target);
}

/* Palette colour a, mixed with t of colour b, at alpha */
static void set_source_mix(cairo_t* cr, PaletteRole a, PaletteRole b, gdouble t, gdouble alpha) {
    double ar, ag, ab, br, bg, bb;
    palette_rgb(a, &ar, &ag, &ab);
    palette_rgb(b, &br, &bg, &bb);
    cairo_set_source_rgba(cr, ar + (br - ar) * t, ag + (bg - ag) * t, ab + (bb - ab) * t, alpha);
}

static void set_source_role(cairo_t* cr, PaletteRole role, gdouble alpha) {
    set_source_mix(cr, role, role, 0.0, alpha);
}

static void draw_rounded_rect(cairo_t* cr, gdouble x, gdouble y,
                              gdouble w, gdouble h, gdouble r) {
    if (w <= 0.0 || h <= 0.0) {
        return;
    }
    if (r > w * 0.5) r = w * 0.5;
    if (r > h * 0.5) r = h * 0.5;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI * 0.5, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, G_PI * 0.5);
    cairo_arc(cr, x + r, y + h - r, r, G_PI * 0.5, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, G_PI * 1.5);
    cairo_close_path(cr);
}

static void on_ws_minimap_draw(GtkDrawingArea* area, cairo_t* cr,
                               int width, int height, gpointer data) {
    (void)area;
    (void)width;
    AppData* app = (AppData*)data;
    gdouble start_y = (height - WS_CELL_H) * 0.5;

    for (gint i = 0; i < app->num_workspaces; i++) {
        VirtualLayout* ws = &app->workspaces[i];
        gboolean is_active = (i == app->current_ws);
        gboolean is_hover = (i == app->hover_ws);
        gdouble bx = WS_PAD_X + i * (WS_CELL_W + WS_CELL_GAP);
        gdouble by = start_y;

        /* Outer desktop frame */
        draw_rounded_rect(cr, bx, by, WS_CELL_W, WS_CELL_H, 3.0);
        if (is_active) {
            set_source_mix(cr, PAL_BG_BAR, PAL_BG_SELECTED, 0.4, 1.0);
        } else if (is_hover) {
            set_source_role(cr, PAL_BG_HOVER, 1.0);
        } else {
            set_source_role(cr, PAL_BG_VIEW, 1.0);
        }
        cairo_fill_preserve(cr);

        if (is_active) {
            set_source_role(cr, PAL_ACCENT, 1.0);
            cairo_set_line_width(cr, 1.5);
        } else if (is_hover) {
            set_source_role(cr, PAL_FG_SUBTLE, 1.0);
            cairo_set_line_width(cr, 1.0);
        } else {
            set_source_role(cr, PAL_BORDER_STRONG, 1.0);
            cairo_set_line_width(cr, 1.0);
        }
        cairo_stroke(cr);

        /* Inner miniature panes */
        gdouble ix = bx + 2.5;
        gdouble iy = by + 2.5;
        gdouble iw = WS_CELL_W - 5.0;
        gdouble ih = WS_CELL_H - 5.0;
        gdouble alpha = is_active ? 0.82 : (is_hover ? 0.58 : 0.38);

        gdouble rx = ix;
        gdouble rw = iw;

        gboolean center_vis = ws->preview_vis || ws->editor_dedicated;

        if (ws->files_vis) {
            gdouble fw = (center_vis || ws->term_vis)
                             ? (iw * CLAMP(ws->main_frac, 0.22, 0.72) - 0.8)
                             : iw;
            set_source_role(cr, PAL_ACCENT_FG, alpha); /* Files */
            cairo_rectangle(cr, ix, iy, fw, ih);
            cairo_fill(cr);
            rx = ix + fw + 1.5;
            rw = iw - fw - 1.5;
        }

        if (rw > 1.0 && (center_vis || ws->term_vis)) {
            if (center_vis && ws->term_vis) {
                if (ws->right_orient == GTK_ORIENTATION_VERTICAL) {
                    gdouble ph = ih * CLAMP(ws->right_frac, 0.25, 0.75) - 0.8;
                    /* Top: Center area */
                    if (ws->preview_vis && ws->editor_dedicated) {
                        gdouble ew = rw * CLAMP(ws->editor_frac, 0.20, 0.80) - 0.8;
                        set_source_role(cr, PAL_FG, alpha); /* Preview */
                        cairo_rectangle(cr, rx, iy, ew, ph);
                        cairo_fill(cr);
                        set_source_role(cr, PAL_SYN_ESCAPE, alpha); /* Editor */
                        cairo_rectangle(cr, rx + ew + 1.5, iy, rw - ew - 1.5, ph);
                        cairo_fill(cr);
                    } else if (ws->preview_vis) {
                        set_source_role(cr, PAL_FG, alpha); /* Preview */
                        cairo_rectangle(cr, rx, iy, rw, ph);
                        cairo_fill(cr);
                    } else {
                        set_source_role(cr, PAL_SYN_ESCAPE, alpha); /* Editor */
                        cairo_rectangle(cr, rx, iy, rw, ph);
                        cairo_fill(cr);
                    }
                    /* Bottom: Terminal */
                    set_source_role(cr, PAL_ANSI_BRIGHT_GREEN, alpha); /* Terminal */
                    cairo_rectangle(cr, rx, iy + ph + 1.5, rw, ih - ph - 1.5);
                    cairo_fill(cr);
                } else {
                    gdouble pw = rw * CLAMP(ws->right_frac, 0.25, 0.75) - 0.8;
                    /* Left: Center area */
                    if (ws->preview_vis && ws->editor_dedicated) {
                        gdouble ew = pw * CLAMP(ws->editor_frac, 0.20, 0.80) - 0.8;
                        set_source_role(cr, PAL_FG, alpha); /* Preview */
                        cairo_rectangle(cr, rx, iy, ew, ih);
                        cairo_fill(cr);
                        set_source_role(cr, PAL_SYN_ESCAPE, alpha); /* Editor */
                        cairo_rectangle(cr, rx + ew + 1.5, iy, pw - ew - 1.5, ih);
                        cairo_fill(cr);
                    } else if (ws->preview_vis) {
                        set_source_role(cr, PAL_FG, alpha); /* Preview */
                        cairo_rectangle(cr, rx, iy, pw, ih);
                        cairo_fill(cr);
                    } else {
                        set_source_role(cr, PAL_SYN_ESCAPE, alpha); /* Editor */
                        cairo_rectangle(cr, rx, iy, pw, ih);
                        cairo_fill(cr);
                    }
                    /* Right: Terminal */
                    set_source_role(cr, PAL_ANSI_BRIGHT_GREEN, alpha); /* Terminal */
                    cairo_rectangle(cr, rx + pw + 1.5, iy, rw - pw - 1.5, ih);
                    cairo_fill(cr);
                }
            } else if (center_vis) {
                if (ws->preview_vis && ws->editor_dedicated) {
                    gdouble ew = rw * CLAMP(ws->editor_frac, 0.20, 0.80) - 0.8;
                    set_source_role(cr, PAL_FG, alpha); /* Preview */
                    cairo_rectangle(cr, rx, iy, ew, ih);
                    cairo_fill(cr);
                    set_source_role(cr, PAL_SYN_ESCAPE, alpha); /* Editor */
                    cairo_rectangle(cr, rx + ew + 1.5, iy, rw - ew - 1.5, ih);
                    cairo_fill(cr);
                } else if (ws->preview_vis) {
                    set_source_role(cr, PAL_FG, alpha); /* Preview */
                    cairo_rectangle(cr, rx, iy, rw, ih);
                    cairo_fill(cr);
                } else {
                    set_source_role(cr, PAL_SYN_ESCAPE, alpha); /* Editor */
                    cairo_rectangle(cr, rx, iy, rw, ih);
                    cairo_fill(cr);
                }
            } else if (ws->term_vis) {
                set_source_role(cr, PAL_ANSI_BRIGHT_GREEN, alpha); /* Terminal */
                cairo_rectangle(cr, rx, iy, rw, ih);
                cairo_fill(cr);
            }
        }
    }

    /* Draw the '+' Add Workspace button at the right end (Ubuntu style) */
    if (app->num_workspaces < MAX_VIRTUAL_LAYOUTS) {
        gboolean add_hover = (app->hover_ws == app->num_workspaces);
        gdouble ax = WS_PAD_X + app->num_workspaces * (WS_CELL_W + WS_CELL_GAP);
        gdouble ay = start_y;

        draw_rounded_rect(cr, ax, ay, WS_ADD_W, WS_CELL_H, 3.0);
        if (add_hover) {
            set_source_mix(cr, PAL_BG_BAR, PAL_BG_SELECTED, 0.4, 1.0);
        } else {
            set_source_role(cr, PAL_BG_WINDOW, 1.0);
        }
        cairo_fill_preserve(cr);

        if (add_hover) {
            set_source_role(cr, PAL_ACCENT, 1.0);
            cairo_set_line_width(cr, 1.3);
        } else {
            set_source_role(cr, PAL_BORDER_STRONG, 1.0);
            cairo_set_line_width(cr, 1.0);
        }
        cairo_stroke(cr);

        /* Plus '+' icon */
        gdouble cx = ax + WS_ADD_W * 0.5;
        gdouble cy = ay + WS_CELL_H * 0.5;
        gdouble arm = 4.0;
        if (add_hover) {
            set_source_role(cr, PAL_FG_BRIGHT, 1.0);
        } else {
            set_source_role(cr, PAL_FG_MUTED, 1.0);
        }
        cairo_set_line_width(cr, 1.6);
        cairo_move_to(cr, cx - arm, cy);
        cairo_line_to(cr, cx + arm, cy);
        cairo_move_to(cr, cx, cy - arm);
        cairo_line_to(cr, cx, cy + arm);
        cairo_stroke(cr);
    }
}

/* Returns 0..num_workspaces-1 for a workspace cell, num_workspaces for '+', or -1 */
static gint ws_index_at_x(AppData* app, gdouble x) {
    for (gint i = 0; i < app->num_workspaces; i++) {
        gdouble bx = WS_PAD_X + i * (WS_CELL_W + WS_CELL_GAP);
        if (x >= bx && x <= bx + WS_CELL_W) {
            return i;
        }
    }
    if (app->num_workspaces < MAX_VIRTUAL_LAYOUTS) {
        gdouble ax = WS_PAD_X + app->num_workspaces * (WS_CELL_W + WS_CELL_GAP);
        if (x >= ax && x <= ax + WS_ADD_W) {
            return app->num_workspaces;
        }
    }
    return -1;
}

static void on_ws_minimap_pressed(GtkGestureClick* gesture, gint n_press,
                                  gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    AppData* app = (AppData*)data;
    guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
    gint idx = ws_index_at_x(app, x);
    if (idx < 0) {
        return;
    }
    if (idx == app->num_workspaces) {
        if (button == GDK_BUTTON_PRIMARY) {
            workspace_add(app);
        }
        return;
    }
    if (button == GDK_BUTTON_SECONDARY) {
        workspace_menu_popup(app, app->ws_minimap, x, y, idx, workspace_remove);
    } else if (button == GDK_BUTTON_MIDDLE) {
        workspace_remove(app, idx);
    } else if (button == GDK_BUTTON_PRIMARY) {
        workspace_switch(app, idx);
    }
}

static void on_ws_minimap_motion(GtkEventControllerMotion* ctrl,
                                 gdouble x, gdouble y, gpointer data) {
    (void)ctrl;
    (void)y;
    AppData* app = (AppData*)data;
    gint idx = ws_index_at_x(app, x);
    if (idx != app->hover_ws) {
        app->hover_ws = idx;
        gtk_widget_queue_draw(app->ws_minimap);
    }
}

static void on_ws_minimap_leave(GtkEventControllerMotion* ctrl, gpointer data) {
    (void)ctrl;
    AppData* app = (AppData*)data;
    if (app->hover_ws != -1) {
        app->hover_ws = -1;
        gtk_widget_queue_draw(app->ws_minimap);
    }
}

static gboolean on_ws_minimap_scroll(GtkEventControllerScroll* ctrl,
                                     gdouble dx, gdouble dy, gpointer data) {
    (void)ctrl;
    AppData* app = (AppData*)data;
    if (app->num_workspaces <= 1) {
        return FALSE;
    }
    gint step = (dy > 0.0 || dx > 0.0) ? 1 : ((dy < 0.0 || dx < 0.0) ? -1 : 0);
    if (step != 0) {
        gint next = (app->current_ws + step + app->num_workspaces) % app->num_workspaces;
        workspace_switch(app, next);
        return TRUE;
    }
    return FALSE;
}

GtkWidget* create_workspace_minimap(AppData* app) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_valign(area, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(
        area,
        "Workspaces: left-click to switch, click '+' to add new workspace (Alt++), right-click for the menu, middle-click to close (Alt+-)");
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area),
                                   on_ws_minimap_draw, app, NULL);

    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
    g_signal_connect(click, "pressed", G_CALLBACK(on_ws_minimap_pressed), app);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));

    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_ws_minimap_motion), app);
    g_signal_connect(motion, "leave", G_CALLBACK(on_ws_minimap_leave), app);
    gtk_widget_add_controller(area, motion);

    GtkEventController* scroll =
        gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_ws_minimap_scroll), app);
    gtk_widget_add_controller(area, scroll);

    app->ws_minimap = area;
    ws_minimap_update_size(app);
    return area;
}
