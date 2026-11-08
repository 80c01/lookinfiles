#include "bookmark_icons.h"
#include "core/path_kind.h"
#include "session_store.h"
#include "tree_nav.h"

#include <string.h>

gboolean bookmark_target_is_uri(const gchar* target) {
    return target != NULL && strstr(target, "://") != NULL && g_uri_peek_scheme(target) != NULL;
}

void bookmark_icon_apply(ExplorerRow* row) {
    const gchar* target = row->path;
    gboolean is_uri = bookmark_target_is_uri(target);
    PathKind kind = is_uri ? PATH_KIND_UNKNOWN : path_kind_lookup(target);
    gchar* base = (kind == PATH_KIND_FILE) ? g_path_get_basename(target) : NULL;
    explorer_row_set_icon_name(row, base);
    explorer_row_set_glyph(row, session_target_id(target) != NULL ? "view-dual"
                                : is_uri                   ? "link"
                                : kind == PATH_KIND_MISSING ? "missing"
                                : kind == PATH_KIND_DIR     ? "folder"
                                                            : "unknown");
    explorer_row_set_label(row, row->name);
    g_free(base);
}

/* Relabel a link row to path (returns FALSE: every row is visited) */
static gboolean relabel(ExplorerRow* row, gpointer path) {
    if (explorer_row_is(row, "bookmark") && g_strcmp0(row->path, path) == 0) {
        bookmark_icon_apply(row);
    }
    return FALSE;
}

static void on_checked(const gchar* path, PathKind old_kind, PathKind new_kind, gpointer data) {
    AppData* app = data;
    if (old_kind == new_kind || app->tree == NULL || app->sidebar_view != SIDEBAR_VIEW_BOOKMARKS) {
        return;
    }
    explorer_row_find(explorer_tree_get_root(app->tree), relabel, (gpointer)path);
    ExplorerRow* sel = tree_nav_selected(app);
    if (sel != NULL && explorer_row_is(sel, "bookmark") && g_strcmp0(sel->path, path) == 0) {
        g_signal_emit_by_name(app->tree, "selection-changed");
    }
}

void bookmark_icon_check(AppData* app, const gchar* target) {
    if (app != NULL && !bookmark_target_is_uri(target)) {
        path_kind_check(target, on_checked, app);
    }
}
