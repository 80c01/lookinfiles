/* Directory listings for the explorer tree (see dir_scan.h) */
#include "dir_scan.h"
#include "file_types.h"

#include <glib/gstdio.h>
#include <sys/stat.h>

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
    case DIR_SCAN_SORT_EXT: {
        gchar* xa = g_utf8_casefold(file_extension(a->name), -1);
        gchar* xb = g_utf8_casefold(file_extension(b->name), -1);
        res = g_strcmp0(xa, xb);
        g_free(xa);
        g_free(xb);
        break;
    }
    case DIR_SCAN_SORT_NAME:
    default:
        break;
    }
    return res != 0 ? res : g_utf8_collate(a->name, b->name);
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
            g_free(g_array_index(node->entries, DirScanEntry, i).name);
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

DirScanNode* dir_scan_read(const gchar* dirname, gboolean show_hidden, DirScanSort sort,
                           GHashTable* expanded, GCancellable* cancellable) {
    DirScanNode* node = g_new0(DirScanNode, 1);
    node->dirname = g_strdup(dirname);

    GDir* dir = g_dir_open(dirname, 0, NULL);
    if (dir == NULL) {
        node->unreadable = TRUE;
        return node;
    }

    node->entries = g_array_new(FALSE, FALSE, sizeof(DirScanEntry));
    node->children = g_ptr_array_new();
    const gchar* name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (g_cancellable_is_cancelled(cancellable)) {
            g_dir_close(dir);
            return node;
        }
        if (!show_hidden && name[0] == '.') {
            continue;
        }
        gchar* full = g_build_filename(dirname, name, NULL);
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

void dir_scan_collect_expand_all(const gchar* base, gboolean show_hidden, guint max_rows,
                                 GHashTable* out, GCancellable* cancellable) {
    GQueue queue = G_QUEUE_INIT;
    g_queue_push_tail(&queue, g_strdup(base));
    guint rows = 0;
    gchar* dirname;
    while ((dirname = g_queue_pop_head(&queue)) != NULL) {
        GDir* dir = (rows < max_rows && !g_cancellable_is_cancelled(cancellable))
                        ? g_dir_open(dirname, 0, NULL)
                        : NULL;
        if (dir != NULL) {
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
