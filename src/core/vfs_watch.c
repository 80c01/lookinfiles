/* Polled stamps of remote files (see vfs_watch.h). */
#include "core/vfs_watch.h"
#include "core/vfs.h"

typedef struct {
    gboolean exists;
    gint64 size;
    gint64 mtime;
} Known;

static GMutex lock;
static GHashTable* known; /* path -> Known: the last readings */

struct VfsWatch {
    gint refs; /* the caller, a reading running */
    gchar* path;
    VfsWatchFunc changed;
    gpointer data;
    guint timer;
    gboolean busy; /* a reading runs */
    gboolean dead; /* freed by the caller */
};

/* Store a reading; TRUE when it differs from the one before (none before:
 * FALSE, nothing to compare with) */
static gboolean remember(const gchar* path, const Known* k) {
    g_mutex_lock(&lock);
    if (known == NULL) known = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    Known* old = g_hash_table_lookup(known, path);
    gboolean differs = old != NULL && (old->exists != k->exists || old->size != k->size || old->mtime != k->mtime);
    g_hash_table_replace(known, g_strdup(path), g_memdup2(k, sizeof *k));
    g_mutex_unlock(&lock);
    return differs;
}

/* A reading over SFTP; FALSE when the session could not tell */
static gboolean read_stamp(const gchar* path, Known* k) {
    VfsEntry e;
    GError* error = NULL;
    k->exists = vfs_stat(path, &e, NULL, &error);
    k->size = k->exists ? e.size : 0;
    k->mtime = k->exists ? e.mtime : 0;
    gboolean told = k->exists || !vfs_error_is_connection(error);
    g_clear_error(&error);
    return told;
}

gboolean vfs_watch_stamp(const gchar* path, gboolean* exists, gint64* size, gint64* mtime) {
    Known k = { 0 };
    g_mutex_lock(&lock);
    const Known* old = known != NULL ? g_hash_table_lookup(known, path) : NULL;
    gboolean have = old != NULL;
    if (have) k = *old;
    g_mutex_unlock(&lock);
    if (!have) {
        if (!read_stamp(path, &k)) return FALSE;
        remember(path, &k);
    }
    if (exists != NULL) *exists = k.exists;
    if (size != NULL) *size = k.size;
    if (mtime != NULL) *mtime = k.mtime;
    return TRUE;
}

void vfs_watch_forget(const gchar* path) {
    g_mutex_lock(&lock);
    if (known != NULL && path != NULL) g_hash_table_remove(known, path);
    g_mutex_unlock(&lock);
}

static void watch_unref(VfsWatch* w) {
    if (--w->refs > 0) return;
    g_free(w->path);
    g_free(w);
}

typedef struct {
    VfsWatch* w;
    gchar* path;
    Known k;
    gboolean told;
} Reading;

static void reading_free(gpointer data) {
    Reading* r = data;
    g_free(r->path);
    g_free(r);
}

static void reading_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancel) {
    (void)source, (void)cancel;
    Reading* r = data;
    r->told = read_stamp(r->path, &r->k);
    g_task_return_boolean(task, TRUE);
}

static void on_reading(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source, (void)data;
    Reading* r = g_task_get_task_data(G_TASK(res));
    VfsWatch* w = r->w;
    w->busy = FALSE;
    gboolean changed = r->told && remember(r->path, &r->k);
    if (changed && !w->dead && w->changed != NULL) w->changed(w->data);
    watch_unref(w);
}

void vfs_watch_poll(VfsWatch* w) {
    if (w == NULL || w->busy || w->dead) return;
    w->busy = TRUE;
    w->refs++;
    Reading* r = g_new0(Reading, 1);
    r->w = w;
    r->path = g_strdup(w->path);
    GTask* task = g_task_new(NULL, NULL, on_reading, NULL);
    g_task_set_task_data(task, r, reading_free);
    g_task_run_in_thread(task, reading_thread);
    g_object_unref(task);
}

static gboolean on_tick(gpointer data) {
    vfs_watch_poll(data);
    return G_SOURCE_CONTINUE;
}

VfsWatch* vfs_watch_new(const gchar* path, VfsWatchFunc changed, gpointer data) {
    VfsWatch* w = g_new0(VfsWatch, 1);
    w->refs = 1;
    w->path = g_strdup(path);
    w->changed = changed;
    w->data = data;
    w->timer = g_timeout_add_seconds(VFS_WATCH_SECONDS, on_tick, w);
    vfs_watch_poll(w); /* a first reading to compare with */
    return w;
}

void vfs_watch_free(VfsWatch* w) {
    if (w == NULL) return;
    if (w->timer != 0) g_source_remove(w->timer);
    w->timer = 0;
    w->dead = TRUE;
    watch_unref(w);
}
