#include "path_kind.h"

#include <gio/gio.h>
#include <glib/gstdio.h>

static GHashTable* kinds;   /* path -> PathKind */
static GHashTable* running; /* paths being checked */

typedef struct {
    gchar* path;
    PathKind kind;
    PathKindFunc done;
    gpointer data;
} Check;

static void check_free(gpointer p) {
    Check* c = p;
    g_free(c->path);
    g_free(c);
}

static void ensure_tables(void) {
    if (kinds == NULL) {
        kinds = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        running = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    }
}

PathKind path_kind_lookup(const gchar* path) {
    if (path == NULL || kinds == NULL) {
        return PATH_KIND_UNKNOWN;
    }
    return (PathKind)GPOINTER_TO_INT(g_hash_table_lookup(kinds, path));
}

/* One stat, following symlinks */
static void check_thread(GTask* task, gpointer source, gpointer task_data, GCancellable* cancellable) {
    (void)source;
    (void)cancellable;
    Check* c = task_data;
    GStatBuf st;
    if (g_stat(c->path, &st) != 0) {
        c->kind = PATH_KIND_MISSING;
    } else {
        c->kind = S_ISDIR(st.st_mode) ? PATH_KIND_DIR : PATH_KIND_FILE;
    }
    g_task_return_boolean(task, TRUE);
}

static void check_ready(GObject* source, GAsyncResult* res, gpointer user_data) {
    (void)source;
    (void)user_data;
    Check* c = g_task_get_task_data(G_TASK(res));
    ensure_tables();
    PathKind old_kind = path_kind_lookup(c->path);
    g_hash_table_insert(kinds, g_strdup(c->path), GINT_TO_POINTER(c->kind));
    g_hash_table_remove(running, c->path);
    if (c->done != NULL) {
        c->done(c->path, old_kind, c->kind, c->data);
    }
}

gboolean path_kind_check(const gchar* path, PathKindFunc done, gpointer data) {
    if (path == NULL || path[0] == '\0') {
        return FALSE;
    }
    ensure_tables();
    if (g_hash_table_contains(running, path)) {
        return FALSE;
    }
    g_hash_table_add(running, g_strdup(path));
    Check* c = g_new0(Check, 1);
    c->path = g_strdup(path);
    c->done = done;
    c->data = data;
    GTask* task = g_task_new(NULL, NULL, check_ready, NULL);
    g_task_set_task_data(task, c, check_free);
    g_task_run_in_thread(task, check_thread);
    g_object_unref(task);
    return TRUE;
}

void path_kind_clear(void) {
    if (kinds != NULL) {
        g_hash_table_remove_all(kinds);
    }
}
