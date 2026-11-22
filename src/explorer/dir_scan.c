/* Directory listings for the explorer tree (see dir_scan.h) */
#include "explorer/dir_scan.h"
#include "core/file_types.h"
#include "core/vfs.h"

#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>

/* Extension sort key: the extension, casefolded */
static gchar* ext_key_of(const gchar* name) {
    return g_utf8_casefold(file_extension(name), -1);
}

static gint compare_ext(const DirScanEntry* a, const DirScanEntry* b) {
    if (a->ext_key != NULL && b->ext_key != NULL) {
        return strcmp(a->ext_key, b->ext_key);
    }
    gchar* xa = ext_key_of(a->name);
    gchar* xb = ext_key_of(b->name);
    gint res = g_strcmp0(xa, xb);
    g_free(xa);
    g_free(xb);
    return res;
}

static gint compare_name(const DirScanEntry* a, const DirScanEntry* b) {
    if (a->name_key != NULL && b->name_key != NULL) {
        return strcmp(a->name_key, b->name_key);
    }
    return g_utf8_collate(a->name, b->name);
}

gint dir_scan_compare(const DirScanEntry* a, const DirScanEntry* b, DirScanSort sort) {
    if (a->is_dir != b->is_dir) {
        return a->is_dir ? -1 : 1;
    }
    gint res = 0;
    switch (sort) {
    case DIR_SCAN_SORT_DATE:
        res = (a->mtime > b->mtime) ? -1 : (a->mtime < b->mtime) ? 1 : 0;
        break;
    case DIR_SCAN_SORT_SIZE:
        res = (a->size > b->size) ? -1 : (a->size < b->size) ? 1 : 0;
        break;
    case DIR_SCAN_SORT_EXT:
        res = compare_ext(a, b);
        break;
    case DIR_SCAN_SORT_NAME:
    default:
        break;
    }
    return res != 0 ? res : compare_name(a, b);
}

static gint compare_with_data(gconstpointer a, gconstpointer b, gpointer data) {
    return dir_scan_compare(a, b, (DirScanSort)GPOINTER_TO_INT(data));
}

void dir_scan_node_free(DirScanNode* node) {
    if (node == NULL) {
        return;
    }
    g_free(node->dirname);
    if (node->entries != NULL) {
        for (guint i = 0; i < node->entries->len; i++) {
            DirScanEntry* e = &g_array_index(node->entries, DirScanEntry, i);
            g_free(e->name);
            g_free(e->name_key);
            g_free(e->ext_key);
        }
        g_array_unref(node->entries);
    }
    if (node->children != NULL) {
        for (guint i = 0; i < node->children->len; i++) {
            dir_scan_node_free(g_ptr_array_index(node->children, i));
        }
        g_ptr_array_unref(node->children);
    }
    g_free(node);
}

/* The entries of node->dirname read from the disk (or the mount); FALSE
 * when cancelled */
static gboolean read_local(DirScanNode* node, gboolean show_hidden, GCancellable* cancellable) {
    GDir* dir = g_dir_open(node->dirname, 0, NULL);
    if (dir == NULL) {
        node->unreadable = TRUE;
        return TRUE;
    }
    const gchar* name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (g_cancellable_is_cancelled(cancellable)) {
            g_dir_close(dir);
            return FALSE;
        }
        if (!show_hidden && name[0] == '.') {
            continue;
        }
        gchar* full = g_build_filename(node->dirname, name, NULL);
        GStatBuf st;
        gboolean ok = (g_stat(full, &st) == 0);
        DirScanEntry e = {
            .name = g_strdup(name),
            .is_dir = ok && S_ISDIR(st.st_mode),
            .mtime = ok ? (gint64)st.st_mtime : 0,
        };
        e.size = (ok && !e.is_dir) ? (gint64)st.st_size : 0;
        g_free(full);
        g_array_append_val(node->entries, e);
    }
    g_dir_close(dir);
    return TRUE;
}

/* The entries of a remote folder over SFTP (vfs.h). FALSE when the SFTP
 * connection cannot serve them (not open, lost): the mount is read then. */
static gboolean read_remote(DirScanNode* node, gboolean show_hidden, GCancellable* cancellable) {
    GError* error = NULL;
    GPtrArray* list = vfs_list(node->dirname, cancellable, &error);
    if (list == NULL) {
        gboolean answered = !vfs_error_is_connection(error) &&
                            !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
        if (answered) {
            node->unreadable = TRUE;
        } else {
            g_debug("SFTP listing of %s: %s; reading the mount", node->dirname, error->message);
        }
        g_error_free(error);
        return answered;
    }
    for (guint i = 0; i < list->len; i++) {
        const VfsEntry* v = list->pdata[i];
        if (!show_hidden && v->name[0] == '.') {
            continue;
        }
        DirScanEntry e = {
            .name = g_strdup(v->name),
            .is_dir = v->is_dir,
            .mtime = v->mtime,
            .size = v->is_dir ? 0 : v->size,
        };
        g_array_append_val(node->entries, e);
    }
    g_ptr_array_unref(list);
    return TRUE;
}

DirScanNode* dir_scan_read(const gchar* dirname, gboolean show_hidden, DirScanSort sort,
                           GHashTable* expanded, GCancellable* cancellable) {
    DirScanNode* node = g_new0(DirScanNode, 1);
    /* Canonical once here: an entry's path is then dirname/name as it is */
    node->dirname = g_canonicalize_filename(dirname, NULL);
    dirname = node->dirname;

    node->entries = g_array_new(FALSE, FALSE, sizeof(DirScanEntry));
    node->children = g_ptr_array_new();
    gboolean read = FALSE;
    if (vfs_is_remote(dirname)) read = read_remote(node, show_hidden, cancellable);
    if (!read && !node->unreadable && !read_local(node, show_hidden, cancellable)) {
        return node; /* cancelled: partial */
    }
    if (node->unreadable) {
        g_clear_pointer(&node->entries, g_array_unref);
        g_clear_pointer(&node->children, g_ptr_array_unref);
        return node;
    }

    /* Keys made once: a comparison is then a strcmp (no collation per call) */
    for (guint i = 0; i < node->entries->len; i++) {
        DirScanEntry* e = &g_array_index(node->entries, DirScanEntry, i);
        e->name_key = g_utf8_collate_key(e->name, -1);
        if (sort == DIR_SCAN_SORT_EXT) {
            e->ext_key = ext_key_of(e->name);
        }
    }
    g_array_sort_with_data(node->entries, compare_with_data, GINT_TO_POINTER(sort));

    for (guint i = 0; i < node->entries->len; i++) {
        const DirScanEntry* e = &g_array_index(node->entries, DirScanEntry, i);
        DirScanNode* child = NULL;
        if (e->is_dir && expanded != NULL) {
            gchar* full = g_build_filename(dirname, e->name, NULL);
            if (g_hash_table_contains(expanded, full)) {
                child = dir_scan_read(full, show_hidden, sort, expanded, cancellable);
            }
            g_free(full);
        }
        g_ptr_array_add(node->children, child);
    }
    return node;
}

gboolean dir_scan_is_self(const gchar* name) {
    return g_strcmp0(name, DIR_SCAN_SELF) == 0;
}

void dir_scan_add_self(DirScanNode* node) {
    if (node == NULL || node->unreadable || node->entries == NULL ||
        (node->entries->len > 0 && dir_scan_is_self(g_array_index(node->entries, DirScanEntry, 0).name))) {
        return;
    }
    GStatBuf st;
    DirScanEntry e = {
        .name = g_strdup(DIR_SCAN_SELF),
        .is_dir = TRUE,
        .mtime = (g_stat(node->dirname, &st) == 0) ? (gint64)st.st_mtime : 0,
        .name_key = g_utf8_collate_key(DIR_SCAN_SELF, -1),
    };
    g_array_prepend_val(node->entries, e);
    g_ptr_array_insert(node->children, 0, NULL);
}

typedef struct {
    gchar* dirname;
    gboolean show_hidden;
    DirScanSort sort;
    GHashTable* expanded;
} ReadJob;

static void read_job_free(gpointer data) {
    ReadJob* job = data;
    g_free(job->dirname);
    if (job->expanded != NULL) {
        g_hash_table_destroy(job->expanded);
    }
    g_free(job);
}

static void read_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    ReadJob* job = data;
    DirScanNode* node = dir_scan_read(job->dirname, job->show_hidden, job->sort, job->expanded,
                                      cancellable);
    g_task_return_pointer(task, node, (GDestroyNotify)dir_scan_node_free);
}

void dir_scan_read_async(const gchar* dirname, gboolean show_hidden, DirScanSort sort,
                         GHashTable* expanded, GAsyncReadyCallback callback, gpointer user_data) {
    ReadJob* job = g_new0(ReadJob, 1);
    job->dirname = g_strdup(dirname);
    job->show_hidden = show_hidden;
    job->sort = sort;
    if (expanded != NULL) {
        job->expanded = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        GHashTableIter iter;
        gpointer key;
        g_hash_table_iter_init(&iter, expanded);
        while (g_hash_table_iter_next(&iter, &key, NULL)) {
            g_hash_table_add(job->expanded, g_strdup(key));
        }
    }
    GTask* task = g_task_new(NULL, NULL, callback, user_data);
    g_task_set_task_data(task, job, read_job_free);
    g_task_run_in_thread(task, read_thread);
    g_object_unref(task);
}

DirScanNode* dir_scan_read_finish(GAsyncResult* result) {
    return g_task_propagate_pointer(G_TASK(result), NULL);
}

void dir_scan_collect_expand_all(const gchar* base, gboolean show_hidden, guint max_rows,
                                 GHashTable* out, GCancellable* cancellable) {
    GQueue queue = G_QUEUE_INIT;
    g_queue_push_tail(&queue, g_strdup(base));
    guint rows = 0;
    gchar* dirname;
    while ((dirname = g_queue_pop_head(&queue)) != NULL) {
        gboolean go = rows < max_rows && !g_cancellable_is_cancelled(cancellable);
        GPtrArray* remote = go && vfs_is_remote(dirname) ? vfs_list(dirname, cancellable, NULL) : NULL;
        GDir* dir = go && remote == NULL ? g_dir_open(dirname, 0, NULL) : NULL;
        if (remote != NULL) {
            if (rows > 0) {
                g_hash_table_add(out, g_strdup(dirname));
            }
            for (guint i = 0; i < remote->len; i++) {
                const VfsEntry* v = remote->pdata[i];
                if (!show_hidden && v->name[0] == '.') {
                    continue;
                }
                rows++;
                if (v->is_dir && !v->is_link) {
                    g_queue_push_tail(&queue, g_build_filename(dirname, v->name, NULL));
                }
            }
            g_ptr_array_unref(remote);
        } else if (dir != NULL) {
            if (rows > 0) {
                g_hash_table_add(out, g_strdup(dirname)); /* not the base folder */
            }
            const gchar* name;
            while ((name = g_dir_read_name(dir)) != NULL) {
                if (!show_hidden && name[0] == '.') {
                    continue;
                }
                rows++;
                gchar* full = g_build_filename(dirname, name, NULL);
                GStatBuf st;
                if (g_lstat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
                    g_queue_push_tail(&queue, full);
                } else {
                    g_free(full);
                }
            }
            g_dir_close(dir);
        }
        g_free(dirname);
    }
}
