/* Local copies of remote files (see vfs_cache.h). */
#include "core/vfs_cache.h"
#include "core/app_paths.h"
#include "core/i18n.h"
#include "core/vfs.h"

#include <glib/gstdio.h>
#include <string.h>

#define STAMP_FILE ".stamp" /* "<size> <mtime>" of the remote file copied */

gchar* vfs_cache_dir(void) {
    gchar* cache = app_cache_dir();
    gchar* dir = g_build_filename(cache, "remote-files", NULL);
    g_free(cache);
    return dir;
}

/* <cache>/<sha1 of path, 16 digits>: one folder per remote file */
static gchar* copy_dir(const gchar* path) {
    gchar* sum = g_compute_checksum_for_string(G_CHECKSUM_SHA1, path, -1);
    sum[16] = '\0';
    gchar* base = vfs_cache_dir();
    gchar* dir = g_build_filename(base, sum, NULL);
    g_free(base);
    g_free(sum);
    return dir;
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

    gchar* old = NULL;
    gboolean fresh = g_file_get_contents(stamp_path, &old, NULL, NULL) && g_strcmp0(old, stamp) == 0 &&
                     g_file_test(local, G_FILE_TEST_IS_REGULAR);
    g_free(old);
    if (fresh) {
        g_file_set_contents(stamp_path, stamp, -1, NULL); /* its date: recently used, trimmed last */
    } else {
        GBytes* bytes = vfs_read(path, max_bytes, NULL, cancel, error);
        gboolean ok = bytes != NULL && g_mkdir_with_parents(dir, 0700) == 0;
        if (ok) {
            gsize n = 0;
            const gchar* d = g_bytes_get_data(bytes, &n);
            g_unlink(stamp_path); /* no stamp while the copy changes */
            ok = g_file_set_contents(local, d, (gssize)n, error) && g_file_set_contents(stamp_path, stamp, -1, error);
        } else if (bytes != NULL) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, _("the cache folder cannot be made"));
        }
        if (bytes != NULL) g_bytes_unref(bytes);
        if (!ok) g_clear_pointer(&local, g_free);
    }
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
        remove_copy(c->dir);
        total -= c->bytes;
        removed++;
    }
    g_ptr_array_unref(copies);
    g_free(base);
    return removed;
}
