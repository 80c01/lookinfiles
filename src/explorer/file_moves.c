/* Moves of files and folders in the files view, and undo of the last one. */
#include "explorer/file_moves.h"
#include "core/app_events.h"
#include "editor/editor_modes.h"
#include "core/i18n.h"
#include "explorer/trash_restore.h"

#include <errno.h>
#include <string.h>

typedef enum { UNDO_TRASH, UNDO_MOVE } UndoKind;

typedef struct FileUndo {
    UndoKind kind;
    gchar* from; /* trashed path, or the path before the move */
    gchar* to;   /* the path after the move (NULL for a trash) */
} FileUndo;

static void file_undo_free(FileUndo* u) {
    if (u != NULL) {
        g_free(u->from);
        g_free(u->to);
        g_free(u);
    }
}

static void remember(AppData* app, UndoKind kind, const gchar* from, const gchar* to) {
    file_undo_free(app->file_undo);
    FileUndo* u = g_new0(FileUndo, 1);
    u->kind = kind;
    u->from = g_strdup(from);
    u->to = g_strdup(to);
    app->file_undo = u;
}

static void forget(AppData* app) {
    file_undo_free(app->file_undo);
    app->file_undo = NULL;
}

/* old_path became new_path: retarget the editor tabs on it (or inside it) */
static void retarget_editor_tabs(AppData* app, const gchar* old_path, const gchar* new_path) {
    gsize old_len = strlen(old_path);
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* tab = (EditorTab*)l->data;
        if (tab == NULL || tab->path == NULL) {
            continue;
        }
        gboolean same = (g_strcmp0(tab->path, old_path) == 0);
        gboolean inside = g_str_has_prefix(tab->path, old_path) &&
                          (tab->path[old_len] == '/' || tab->path[old_len] == '\\');
        if (!same && !inside) {
            continue;
        }
        gchar* moved = g_strconcat(new_path, tab->path + old_len, NULL);
        g_free(tab->path);
        tab->path = moved;
        g_free(tab->name);
        tab->name = g_path_get_basename(moved);
        editor_tab_label_update(tab);
    }
}

/* Plain move, the tabs and the expansion following */
static gboolean move_path(AppData* app, const gchar* from, const gchar* to, GError** error) {
    if (g_file_test(to, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK)) {
        gchar* name = g_path_get_basename(to);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS, "“%s” already exists", name);
        g_free(name);
        return FALSE;
    }
    if (g_rename(from, to) != 0) {
        int saved = errno;
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(saved), "%s", g_strerror(saved));
        return FALSE;
    }
    retarget_editor_tabs(app, from, to);
    if (app->expanded_paths != NULL && g_hash_table_contains(app->expanded_paths, from)) {
        g_hash_table_add(app->expanded_paths, g_strdup(to));
    }
    return TRUE;
}

gboolean file_moves_rename(AppData* app, const gchar* from, const gchar* to, GError** error) {
    if (!move_path(app, from, to, error)) {
        return FALSE;
    }
    remember(app, UNDO_MOVE, from, to);
    gchar* old_name = g_path_get_basename(from);
    gchar* new_name = g_path_get_basename(to);
    gchar* msg = g_strdup_printf(_("Renamed “%1$s” to “%2$s”"), old_name, new_name);
    app_notice(app, msg, TRUE);
    g_free(msg);
    g_free(new_name);
    g_free(old_name);
    return TRUE;
}

gboolean file_moves_trash(AppData* app, const gchar* path, GError** error) {
    GFile* file = g_file_new_for_path(path);
    gboolean ok = g_file_trash(file, NULL, error);
    g_object_unref(file);
    if (!ok) {
        return FALSE;
    }
    remember(app, UNDO_TRASH, path, NULL);
    gchar* name = g_path_get_basename(path);
    gchar* msg = g_strdup_printf(_("Moved “%s” to the trash"), name);
    app_notice(app, msg, TRUE);
    g_free(msg);
    g_free(name);
    return TRUE;
}

gboolean file_moves_can_undo(AppData* app) {
    return app != NULL && app->file_undo != NULL;
}

gchar* file_moves_undo(AppData* app) {
    if (!file_moves_can_undo(app)) {
        app_notice(app, _("Nothing to undo"), FALSE);
        return NULL;
    }
    FileUndo* u = app->file_undo;
    GError* err = NULL;
    gboolean ok = (u->kind == UNDO_TRASH) ? trash_restore(u->from, &err)
                                          : move_path(app, u->to, u->from, &err);
    gchar* name = g_path_get_basename(u->from);
    gchar* msg = ok ? g_strdup_printf(u->kind == UNDO_TRASH ? _("Restored “%s” from the trash")
                                                            : _("Renamed back to “%s”"), name)
                    : g_strdup_printf(_("Cannot undo: %s"), err != NULL ? err->message : _("unknown error"));
    gchar* select = ok ? g_strdup(u->from) : NULL;
    if (ok && app->expanded_paths != NULL) {
        /* Show the restored entry even if its folder was collapsed */
        gchar* parent = g_path_get_dirname(u->from);
        if (g_strcmp0(parent, app->base_dir) != 0) {
            g_hash_table_add(app->expanded_paths, parent);
        } else {
            g_free(parent);
        }
    }
    forget(app); /* one step only: a failed undo is not retried either */
    app_notice(app, msg, FALSE);
    g_clear_error(&err);
    g_free(msg);
    g_free(name);
    return select;
}
