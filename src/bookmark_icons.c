#include "bookmark_icons.h"
#include "file_types.h"
#include "path_kind.h"
#include "tree_nav.h"

#include <string.h>

gboolean bookmark_target_is_uri(const gchar* target) {
    return target != NULL && strstr(target, "://") != NULL && g_uri_peek_scheme(target) != NULL;
}

gchar* bookmark_icon_label(const gchar* name, const gchar* target) {
    PathKind kind = path_kind_lookup(target);
    gchar* base = (kind == PATH_KIND_FILE) ? g_path_get_basename(target) : NULL;
    const gchar* icon = bookmark_target_is_uri(target) ? "🌐"
                        : kind == PATH_KIND_MISSING    ? "⚠"
                        : base != NULL                 ? file_icon(base, FALSE)
                                                       : "🔖";
    gchar* label = g_strdup_printf("%s %s", icon, name);
    g_free(base);
    return label;
}

/* Relabel a link row to path (returns FALSE: every row is visited) */
static gboolean relabel(ExplorerRow* row, gpointer path) {
    if (explorer_row_is(row, "bookmark") && g_strcmp0(row->path, path) == 0) {
        gchar* label = bookmark_icon_label(row->name, row->path);
        explorer_row_set_label(row, label);
        g_free(label);
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
