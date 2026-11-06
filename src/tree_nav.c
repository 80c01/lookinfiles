/* Explorer tree navigation: visible-row moves (j/k, gg/G, Ctrl+F/B, zz/zt/zb) and lookup by name or path. */
#include "tree_nav.h"

ExplorerRow* tree_nav_selected(AppData* app) {
    return (app != NULL && app->tree != NULL) ? explorer_tree_get_selected(app->tree) : NULL;
}

void select_row(AppData* app, ExplorerRow* row) {
    if (row != NULL) {
        explorer_tree_select(app->tree, row);
    }
}

static void select_position(AppData* app, gint pos) {
    gint n = (gint)explorer_tree_n_visible(app->tree);
    if (n > 0) {
        select_row(app, explorer_tree_visible_row(app->tree, (guint)CLAMP(pos, 0, n - 1)));
    }
}

/* Position of the selected row, -1 if none */
static gint selected_position(AppData* app) {
    ExplorerRow* row = tree_nav_selected(app);
    return row != NULL ? explorer_tree_position(app->tree, row) : -1;
}

void select_first_row(AppData* app) {
    select_position(app, 0);
}

void select_last_row(AppData* app) {
    select_position(app, (gint)explorer_tree_n_visible(app->tree) - 1);
}

/* Move the selection by delta visible rows (from the first row if none) */
static void move_selection(AppData* app, gint delta) {
    gint pos = selected_position(app);
    if (pos < 0) {
        select_first_row(app);
        return;
    }
    select_position(app, pos + delta);
}

void select_next_row(AppData* app) {
    move_selection(app, 1);
}

void select_prev_row(AppData* app) {
    move_selection(app, -1);
}

/* Jump by one page of visible rows (Ctrl-F / Ctrl-B) */
void page_down_rows(AppData* app) {
    move_selection(app, explorer_tree_page_rows(app->tree));
}

void page_up_rows(AppData* app) {
    move_selection(app, -explorer_tree_page_rows(app->tree));
}

/* Center the selected row vertically (vim 'zz') */
void center_selected_row(AppData* app) {
    explorer_tree_scroll_to(app->tree, tree_nav_selected(app), 0.5);
}

/* Scroll the selected row to the top (vim 'zt') */
void top_selected_row(AppData* app) {
    explorer_tree_scroll_to(app->tree, tree_nav_selected(app), 0.0);
}

/* Scroll the selected row to the bottom (vim 'zb') */
void bottom_selected_row(AppData* app) {
    explorer_tree_scroll_to(app->tree, tree_nav_selected(app), 1.0);
}

/* Select top-level row matching name (e.g. directory just left) */
gboolean select_by_name(AppData* app, const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    ExplorerRow* root = explorer_tree_get_root(app->tree);
    for (guint i = 0; i < explorer_row_n_children(root); i++) {
        ExplorerRow* row = explorer_row_child(root, i);
        if (!explorer_row_is(row, "bookmark") && g_strcmp0(row->name, name) == 0) {
            select_row(app, row);
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean match_path_not_bookmark(ExplorerRow* row, gpointer data) {
    return !explorer_row_is(row, "bookmark") && g_strcmp0(row->path, (const gchar*)data) == 0;
}

gboolean reveal_row(AppData* app, ExplorerRow* row) {
    if (row == NULL) {
        return FALSE;
    }
    g_object_ref(row);
    explorer_tree_expand_to(app->tree, row);
    gboolean shown = explorer_tree_contains(app->tree, row);
    if (shown) {
        select_row(app, row);
    }
    g_object_unref(row);
    return shown;
}

/* Select row matching full path; regular rows take precedence over bookmarks */
gboolean select_by_path(AppData* app, const gchar* full_path) {
    if (full_path == NULL) {
        return FALSE;
    }
    ExplorerRow* root = explorer_tree_get_root(app->tree);
    ExplorerRow* row = explorer_row_find(root, match_path_not_bookmark, (gpointer)full_path);
    if (row == NULL) {
        row = explorer_row_find_by_path(root, full_path);
    }
    if (row == NULL) {
        return FALSE;
    }
    /* Expanding its folders may rebuild rows (bookmarks, notes): the row
     * found may be gone after, its new copy is looked up again */
    g_object_ref(row);
    explorer_tree_expand_to(app->tree, row);
    if (!explorer_tree_contains(app->tree, row)) {
        g_object_unref(row);
        row = explorer_row_find(root, match_path_not_bookmark, (gpointer)full_path);
        if (row == NULL) {
            row = explorer_row_find_by_path(root, full_path);
        }
        if (row == NULL) {
            return FALSE;
        }
        g_object_ref(row);
    }
    gboolean selected = explorer_tree_select(app->tree, row);
    g_object_unref(row);
    return selected;
}

/* Suspend / resume the preview update on selection changes (while the tree
 * is rebuilt). No-op until main.c has connected the handler. */
void tree_nav_block_selection_handler(AppData* app) {
    if (app->tree != NULL && app->selection_changed_id != 0) {
        g_signal_handler_block(app->tree, app->selection_changed_id);
    }
}

void tree_nav_unblock_selection_handler(AppData* app) {
    if (app->tree != NULL && app->selection_changed_id != 0) {
        g_signal_handler_unblock(app->tree, app->selection_changed_id);
    }
}
