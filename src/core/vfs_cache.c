/* Local copies of remote files (see vfs_cache.h). */
#include "core/vfs_cache.h"
#include "core/app_paths.h"
#include "core/i18n.h"
#include "core/vfs.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#define STAMP_FILE ".stamp"           /* "<size> <mtime>" of the remote file copied */
#define PIECE_BYTES (8 * 1024 * 1024) /* read over SFTP and written a piece at a time */

gchar* vfs_cache_dir(void) {
    gchar* cache = app_cache_dir();
    gchar* dir = g_build_filename(cache, "remote-files", NULL);
    g_free(cache);
    return dir;
}

/* <cache>/<sha1 of path, 16 digits>: one folder per remote file */
/* The copy folders being made or checked: one copy of a file at a time
 * (two workers copying the same picture would write the same .part) */
static GMutex busy_lock;
static GCond busy_cond;
static GHashTable* busy_dirs;

static void busy_enter(const gchar* dir) {
    g_mutex_lock(&busy_lock);
    if (busy_dirs == NULL) busy_dirs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    while (g_hash_table_contains(busy_dirs, dir)) g_cond_wait(&busy_cond, &busy_lock);
    g_hash_table_add(busy_dirs, g_strdup(dir));
    g_mutex_unlock(&busy_lock);
}

static void busy_leave(const gchar* dir) {
    g_mutex_lock(&busy_lock);
    g_hash_table_remove(busy_dirs, dir);
    g_cond_broadcast(&busy_cond);
    g_mutex_unlock(&busy_lock);
}

static gboolean busy_now(const gchar* dir) {
    g_mutex_lock(&busy_lock);
    gboolean busy = busy_dirs != NULL && g_hash_table_contains(busy_dirs, dir);
    g_mutex_unlock(&busy_lock);
    return busy;
}

static gboolean local_size_is(const gchar* local, gint64 size) {
    GStatBuf st;
    return g_stat(local, &st) == 0 && S_ISREG(st.st_mode) && (gint64)st.st_size == size;
}

static gchar* copy_dir(const gchar* path) {
    gchar* sum = g_compute_checksum_for_string(G_CHECKSUM_SHA1, path, -1);
    sum[16] = '\0';
    gchar* base = vfs_cache_dir();
    gchar* dir = g_build_filename(base, sum, NULL);
    g_free(base);
    g_free(sum);
    return dir;
}

/* path copied to local, a piece at a time (never the whole file in memory) */
static gboolean copy_pieces(const gchar* path, guint64 size, const gchar* local, GCancellable* cancel,
                            GError** error) {
    gchar* part = g_strconcat(local, ".part", NULL);
    FILE* f = g_fopen(part, "wb");
    if (f == NULL) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("the cache folder cannot be made"));
        g_free(part);
        return FALSE;
    }
    gboolean ok = TRUE;
    for (guint64 off = 0; ok && off < size;) {
        GBytes* piece = vfs_read_range(path, off, PIECE_BYTES, NULL, cancel, error);
        gsize n = 0;
        const guint8* d = piece != NULL ? g_bytes_get_data(piece, &n) : NULL;
        ok = piece != NULL && (n == 0 || fwrite(d, 1, n, f) == n);
        if (piece != NULL && n == 0) size = off; /* shorter now: the end */
        if (piece != NULL && !ok) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, _("the copy cannot be written"));
        }
        if (piece != NULL) g_bytes_unref(piece);
        off += n;
    }
    ok = fclose(f) == 0 && ok;
    if (ok && g_rename(part, local) != 0) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("the copy cannot be written"));
        ok = FALSE;
    }
    if (!ok) g_unlink(part);
    g_free(part);
    return ok;
}

gchar* vfs_cache_copy(const gchar* path, guint64 max_bytes, GCancellable* cancel, GError** error) {
    VfsEntry e;
    if (!vfs_stat(path, &e, cancel, error)) return NULL;
    if (e.is_dir) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY, _("a folder, not a file"));
        return NULL;
    }
    if ((guint64)e.size > max_bytes) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, _("the file is too large to copy"));
        return NULL;
    }
    gchar* dir = copy_dir(path);
    gchar* name = g_path_get_basename(path);
    gchar* local = g_build_filename(dir, name, NULL);
    gchar* stamp_path = g_build_filename(dir, STAMP_FILE, NULL);
    gchar* stamp = g_strdup_printf("%" G_GINT64_FORMAT " %" G_GINT64_FORMAT, e.size, e.mtime);
    g_free(name);

    busy_enter(dir);
    gchar* old = NULL;
    gboolean fresh = g_file_get_contents(stamp_path, &old, NULL, NULL) && g_strcmp0(old, stamp) == 0 &&
                     local_size_is(local, e.size); /* a copy cut short is made again */
    g_free(old);
    if (fresh) {
        g_file_set_contents(stamp_path, stamp, -1, NULL); /* its date: recently used, trimmed last */
    } else {
        gboolean ok = g_mkdir_with_parents(dir, 0700) == 0;
        if (!ok) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("the cache folder cannot be made"));
        } else {
            g_unlink(stamp_path); /* no stamp while the copy changes */
            ok = copy_pieces(path, (guint64)e.size, local, cancel, error) &&
                 g_file_set_contents(stamp_path, stamp, -1, error);
        }
        if (!ok) g_clear_pointer(&local, g_free);
    }
    busy_leave(dir);
    g_free(stamp);
    g_free(stamp_path);
    g_free(dir);
    return local;
}

typedef struct {
    gchar* dir;
    gint64 used; /* the stamp's time: when last used */
    guint64 bytes;
} Copy;

static gint by_age(gconstpointer a, gconstpointer b) {
    const Copy* x = *(Copy* const*)a;
    const Copy* y = *(Copy* const*)b;
    return x->used < y->used ? -1 : x->used > y->used ? 1 : 0;
}

static void copy_free(gpointer data) {
    Copy* c = data;
    g_free(c->dir);
    g_free(c);
}

/* Remove a copy's folder (its files; never deeper) */
static void remove_copy(const gchar* dir) {
    GDir* d = g_dir_open(dir, 0, NULL);
    if (d != NULL) {
        const gchar* name;
        while ((name = g_dir_read_name(d)) != NULL) {
            gchar* f = g_build_filename(dir, name, NULL);
            g_unlink(f);
            g_free(f);
        }
        g_dir_close(d);
    }
    g_rmdir(dir);
}

guint vfs_cache_trim(guint64 max_bytes) {
    gchar* base = vfs_cache_dir();
    GDir* d = g_dir_open(base, 0, NULL);
    if (d == NULL) {
        g_free(base);
        return 0;
    }
    GPtrArray* copies = g_ptr_array_new_with_free_func(copy_free);
    guint64 total = 0;
    const gchar* name;
    while ((name = g_dir_read_name(d)) != NULL) {
        Copy* c = g_new0(Copy, 1);
        c->dir = g_build_filename(base, name, NULL);
        GDir* in = g_dir_open(c->dir, 0, NULL);
        const gchar* f;
        while (in != NULL && (f = g_dir_read_name(in)) != NULL) {
            gchar* full = g_build_filename(c->dir, f, NULL);
            GStatBuf st;
            if (g_stat(full, &st) == 0) {
                c->bytes += (guint64)st.st_size;
                if (strcmp(f, STAMP_FILE) == 0) c->used = (gint64)st.st_mtime;
            }
            g_free(full);
        }
        if (in != NULL) g_dir_close(in);
        total += c->bytes;
        g_ptr_array_add(copies, c);
    }
    g_dir_close(d);
    g_ptr_array_sort(copies, by_age);
    guint removed = 0;
    for (guint i = 0; i < copies->len && total > max_bytes; i++) {
        Copy* c = copies->pdata[i];
        if (busy_now(c->dir)) continue; /* being made: kept */
        remove_copy(c->dir);
        total -= c->bytes;
        removed++;
    }
    g_ptr_array_unref(copies);
    g_free(base);
    return removed;
}
