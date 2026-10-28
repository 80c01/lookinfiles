/* Explorer tree navigation: visible-row traversal, selection moves (j/k, gg/G, Ctrl+F/B, zz/zt/zb) and lookup by name or path. */
#include "tree_nav.h"

/* Total top-level entries in the model */
gint list_count(AppData* app) {
    return gtk_tree_model_iter_n_children(app->model, NULL);
}

/* Return path of the first visible row */
GtkTreePath* get_first_visible_path(GtkTreeView* view) {
    GtkTreeModel* model = gtk_tree_view_get_model(view);
    GtkTreeIter it;
    if (gtk_tree_model_get_iter_first(model, &it)) {
        return gtk_tree_model_get_path(model, &it);
    }
    return NULL;
}

/* Return next visible row path (following expansions) or NULL if at end */
GtkTreePath* get_next_visible_path(GtkTreeView* view, GtkTreePath* path) {
    if (path == NULL) {
        return NULL;
    }
    GtkTreeModel* model = gtk_tree_view_get_model(view);
    GtkTreePath* curr = gtk_tree_path_copy(path);

    /* 1. If row is expanded, next visible row is its first child */
    if (gtk_tree_view_row_expanded(view, curr)) {
        GtkTreePath* child = gtk_tree_path_copy(curr);
        gtk_tree_path_down(child);
        GtkTreeIter it;
        if (gtk_tree_model_get_iter(model, &it, child)) {
            gtk_tree_path_free(curr);
            return child;
        }
        gtk_tree_path_free(child);
    }

    /* 2. Try next sibling at current level, or walk up to ancestors */
    while (gtk_tree_path_get_depth(curr) > 0) {
        GtkTreePath* next = gtk_tree_path_copy(curr);
        gtk_tree_path_next(next);
        GtkTreeIter it;
        if (gtk_tree_model_get_iter(model, &it, next)) {
            gtk_tree_path_free(curr);
            return next;
        }
        gtk_tree_path_free(next);
        if (!gtk_tree_path_up(curr)) {
            break;
        }
    }

    gtk_tree_path_free(curr);
    return NULL;
}

/* Return previous visible row path (following expansions) or NULL if at top */
GtkTreePath* get_prev_visible_path(GtkTreeView* view, GtkTreePath* path) {
    if (path == NULL) {
        return NULL;
    }
    GtkTreeModel* model = gtk_tree_view_get_model(view);
    GtkTreePath* curr = gtk_tree_path_copy(path);

    /* 1. Try previous sibling */
    if (gtk_tree_path_prev(curr)) {
        /* If that sibling is expanded, step to its last visible descendant */
        while (gtk_tree_view_row_expanded(view, curr)) {
            GtkTreeIter parent_it;
            if (!gtk_tree_model_get_iter(model, &parent_it, curr)) {
                break;
            }
            gint n = gtk_tree_model_iter_n_children(model, &parent_it);
            if (n <= 0) {
                break;
            }
            gtk_tree_path_down(curr);
            for (gint i = 1; i < n; i++) {
                gtk_tree_path_next(curr);
            }
        }
        return curr;
    }

    /* 2. No previous sibling: if we have a parent, parent is previous visible */
    if (gtk_tree_path_get_depth(curr) > 1) {
        gtk_tree_path_up(curr);
        return curr;
    }

    gtk_tree_path_free(curr);
    return NULL;
}

/* Return path of the last visible row */
GtkTreePath* get_last_visible_path(GtkTreeView* view) {
    GtkTreePath* cur = get_first_visible_path(view);
    if (cur == NULL) {
        return NULL;
    }
    while (TRUE) {
        GtkTreePath* next = get_next_visible_path(view, cur);
        if (next == NULL) {
            return cur;
        }
        gtk_tree_path_free(cur);
        cur = next;
    }
}

/* Select row at path and scroll it into view */
void select_path(AppData* app, GtkTreePath* path) {
    if (path == NULL) {
        return;
    }
    gboolean was_selected = gtk_tree_selection_path_is_selected(app->selection, path);
    gtk_tree_selection_select_path(app->selection, path);
    gtk_tree_view_set_cursor(app->tree_view, path, NULL, FALSE);
    gtk_tree_view_scroll_to_cell(app->tree_view, path, NULL, FALSE, 0.0, 0.0);
    if (was_selected) {
        /* Re-selecting the same row emits nothing: notify listeners (the viewer
         * preview) explicitly so the action still refreshes the right pane. */
        g_signal_emit_by_name(app->selection, "changed");
    }
}

/* Select the first visible row */
void select_first_row(AppData* app) {
    GtkTreePath* p = get_first_visible_path(app->tree_view);
    if (p != NULL) {
        select_path(app, p);
        gtk_tree_path_free(p);
    }
}

/* Select the last visible row */
void select_last_row(AppData* app) {
    GtkTreePath* p = get_last_visible_path(app->tree_view);
    if (p != NULL) {
        select_path(app, p);
        gtk_tree_path_free(p);
    }
}

/* Select the next visible row */
void select_next_row(AppData* app) {
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        select_first_row(app);
        return;
    }
    GtkTreePath* path = gtk_tree_model_get_path(app->model, &it);
    GtkTreePath* next = get_next_visible_path(app->tree_view, path);
    if (next != NULL) {
        select_path(app, next);
        gtk_tree_path_free(next);
    }
    gtk_tree_path_free(path);
}

/* Select the previous visible row */
void select_prev_row(AppData* app) {
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        select_first_row(app);
        return;
    }
    GtkTreePath* path = gtk_tree_model_get_path(app->model, &it);
    GtkTreePath* prev = get_prev_visible_path(app->tree_view, path);
    if (prev != NULL) {
        select_path(app, prev);
        gtk_tree_path_free(prev);
    }
    gtk_tree_path_free(path);
}

/* Number of visible rows in tree view (for Ctrl-F / Ctrl-B) */
gint count_visible_rows(AppData* app) {
    GtkTreePath* start = NULL;
    GtkTreePath* end = NULL;
    if (!gtk_tree_view_get_visible_range(app->tree_view, &start, &end)) {
        return 10;
    }
    gint count = 1;
    GtkTreePath* cur = gtk_tree_path_copy(start);
    while (gtk_tree_path_compare(cur, end) != 0) {
        GtkTreePath* next = get_next_visible_path(app->tree_view, cur);
        if (next == NULL) {
            break;
        }
        gtk_tree_path_free(cur);
        cur = next;
        count++;
    }
    gtk_tree_path_free(cur);
    gtk_tree_path_free(start);
    gtk_tree_path_free(end);
    return (count > 0) ? count : 10;
}

/* Jump forward by one page of visible rows */
void page_down_rows(AppData* app) {
    gint count = count_visible_rows(app);
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        select_first_row(app);
        return;
    }
    GtkTreePath* cur = gtk_tree_model_get_path(app->model, &it);
    for (gint i = 0; i < count; i++) {
        GtkTreePath* next = get_next_visible_path(app->tree_view, cur);
        if (next == NULL) {
            break;
        }
        gtk_tree_path_free(cur);
        cur = next;
    }
    select_path(app, cur);
    gtk_tree_path_free(cur);
}

/* Jump backward by one page of visible rows */
void page_up_rows(AppData* app) {
    gint count = count_visible_rows(app);
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        select_first_row(app);
        return;
    }
    GtkTreePath* cur = gtk_tree_model_get_path(app->model, &it);
    for (gint i = 0; i < count; i++) {
        GtkTreePath* prev = get_prev_visible_path(app->tree_view, cur);
        if (prev == NULL) {
            break;
        }
        gtk_tree_path_free(cur);
        cur = prev;
    }
    select_path(app, cur);
    gtk_tree_path_free(cur);
}

static void scroll_selected_row_aligned(AppData* app, gfloat row_align) {
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        return;
    }
    GtkTreePath* path = gtk_tree_model_get_path(app->model, &it);
    if (path != NULL) {
        gtk_tree_view_scroll_to_cell(app->tree_view, path, NULL, TRUE, row_align, 0.0f);
        gtk_tree_path_free(path);
    }
}

/* Center the currently selected row vertically in the tree view (vim 'zz') */
void center_selected_row(AppData* app) {
    scroll_selected_row_aligned(app, 0.5f);
}

/* Scroll the currently selected row to the top of the tree view (vim 'zt') */
void top_selected_row(AppData* app) {
    scroll_selected_row_aligned(app, 0.0f);
}

/* Scroll the currently selected row to the bottom of the tree view (vim 'zb') */
void bottom_selected_row(AppData* app) {
    scroll_selected_row_aligned(app, 1.0f);
}

/* Select top-level row matching name (e.g. directory just left) */
gboolean select_by_name(AppData* app, const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    GtkTreeIter it;
    if (!gtk_tree_model_get_iter_first(app->model, &it)) {
        return FALSE;
    }
    do {
        gchar* row_name = NULL;
        gchar* row_type = NULL;
        gtk_tree_model_get(app->model, &it,
                           COL_NAME, &row_name,
                           COL_TYPE, &row_type, -1);
        gboolean match = (g_strcmp0(row_type, "bookmark") != 0 &&
                          g_strcmp0(row_name, name) == 0);
        g_free(row_name);
        g_free(row_type);
        if (match) {
            GtkTreePath* p = gtk_tree_model_get_path(app->model, &it);
            select_path(app, p);
            gtk_tree_path_free(p);
            return TRUE;
        }
    } while (gtk_tree_model_iter_next(app->model, &it));
    return FALSE;
}

/* Recursive helper to search for a row by absolute path */
static gboolean find_iter_by_path_recursive_internal(GtkTreeModel* model, GtkTreeIter* parent,
                                                     const gchar* full_path, GtkTreeIter* result,
                                                     gboolean allow_bookmarks) {
    GtkTreeIter child;
    if (!gtk_tree_model_iter_children(model, &child, parent)) {
        return FALSE;
    }
    do {
        gchar* path_val = NULL;
        gchar* type_val = NULL;
        gtk_tree_model_get(model, &child,
                           COL_PATH, &path_val,
                           COL_TYPE, &type_val, -1);
        gboolean is_bm = (g_strcmp0(type_val, "bookmark") == 0);
        gboolean match = (g_strcmp0(path_val, full_path) == 0);
        g_free(path_val);
        g_free(type_val);

        if (match && (allow_bookmarks || !is_bm)) {
            *result = child;
            return TRUE;
        }
        if (find_iter_by_path_recursive_internal(model, &child, full_path, result, allow_bookmarks)) {
            return TRUE;
        }
    } while (gtk_tree_model_iter_next(model, &child));
    return FALSE;
}

static gboolean find_iter_by_path_recursive(GtkTreeModel* model, GtkTreeIter* parent,
                                            const gchar* full_path, GtkTreeIter* result) {
    /* First try non-bookmark entries so regular directory rows take precedence */
    if (find_iter_by_path_recursive_internal(model, parent, full_path, result, FALSE)) {
        return TRUE;
    }
    return find_iter_by_path_recursive_internal(model, parent, full_path, result, TRUE);
}

/* Select row matching full path */
gboolean select_by_path(AppData* app, const gchar* full_path) {
    if (full_path == NULL) {
        return FALSE;
    }
    GtkTreeIter it;
    if (find_iter_by_path_recursive(app->model, NULL, full_path, &it)) {
        GtkTreePath* p = gtk_tree_model_get_path(app->model, &it);
        select_path(app, p);
        gtk_tree_path_free(p);
        return TRUE;
    }
    return FALSE;
}
