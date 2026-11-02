/* Archives as folders in the explorer (see archive_tree.h). */
#include "archive_tree.h"

#include <string.h>

#include "archive_extract.h"
#include "archive_listing.h"
#include "explorer_tree.h"

/* Entries kept in the tree, and the largest entry extracted for a preview */
#define ARCHIVE_TREE_MAX_ENTRIES 20000
#define ARCHIVE_TREE_MAX_EXTRACT (128u * 1024u * 1024u)

gboolean archive_tree_row_is_archive(ExplorerRow* row) {
    return row != NULL && explorer_row_is(row, "file") && row->children != NULL;
}

gboolean archive_tree_is_entry(ExplorerRow* row) {
    return explorer_row_is(row, "arc_dir") || explorer_row_is(row, "arc_file");
}

/* The archive row above an entry, NULL if none */
static ExplorerRow* archive_of(ExplorerRow* row) {
    for (ExplorerRow* r = explorer_row_get_parent(row); r != NULL; r = explorer_row_get_parent(r)) {
        if (archive_tree_row_is_archive(r)) {
            return r;
        }
    }
    return NULL;
}

/* ---- Listing --------------------------------------------------------- */

typedef struct ArcNode {
    gchar* name;
    gchar* orig;        /* entry name as stored (files), NULL for folders */
    gchar* clean;       /* path from the archive root, no '/' at the ends */
    gboolean is_dir;
    GPtrArray* kids;    /* ArcNode*, folders only */
} ArcNode;

static void arc_node_free(gpointer data) {
    ArcNode* n = data;
    if (n == NULL) {
        return;
    }
    g_free(n->name);
    g_free(n->orig);
    g_free(n->clean);
    if (n->kids != NULL) {
        g_ptr_array_unref(n->kids);
    }
    g_free(n);
}

static ArcNode* arc_node_new(const gchar* name, const gchar* clean, const gchar* orig, gboolean is_dir) {
    ArcNode* n = g_new0(ArcNode, 1);
    n->name = g_strdup(name);
    n->clean = g_strdup(clean);
    n->orig = g_strdup(orig);
    n->is_dir = is_dir;
    if (is_dir) {
        n->kids = g_ptr_array_new_with_free_func(arc_node_free);
    }
    return n;
}

static gint arc_node_compare(gconstpointer a, gconstpointer b) {
    const ArcNode* x = *(ArcNode* const*)a;
    const ArcNode* y = *(ArcNode* const*)b;
    if (x->is_dir != y->is_dir) {
        return x->is_dir ? -1 : 1;
    }
    gchar* fx = g_utf8_casefold(x->name, -1);
    gchar* fy = g_utf8_casefold(y->name, -1);
    gint r = g_utf8_collate(fx, fy);
    g_free(fx);
    g_free(fy);
    return r;
}

static void arc_node_sort(ArcNode* n) {
    if (n->kids == NULL) {
        return;
    }
    g_ptr_array_sort(n->kids, arc_node_compare);
    for (guint i = 0; i < n->kids->len; i++) {
        arc_node_sort(g_ptr_array_index(n->kids, i));
    }
}

/* Folder node for clean path (created with its ancestors) */
static ArcNode* ensure_dir(ArcNode* root, GHashTable* dirs, const gchar* clean) {
    ArcNode* found = g_hash_table_lookup(dirs, clean);
    if (found != NULL) {
        return found;
    }
    const gchar* slash = strrchr(clean, '/');
    ArcNode* parent = root;
    if (slash != NULL) {
        gchar* up = g_strndup(clean, (gsize)(slash - clean));
        parent = ensure_dir(root, dirs, up);
        g_free(up);
    }
    ArcNode* dir = arc_node_new(slash != NULL ? slash + 1 : clean, clean, NULL, TRUE);
    g_ptr_array_add(parent->kids, dir);
    g_hash_table_insert(dirs, g_strdup(clean), dir);
    return dir;
}

typedef struct {
    gchar* archive;
    ArcNode* root;
    gboolean incomplete;
    gchar* error;
} ListData;

static void list_data_free(gpointer data) {
    ListData* d = data;
    g_free(d->archive);
    arc_node_free(d->root);
    g_free(d->error);
    g_free(d);
}

static void list_thread(GTask* task, gpointer source, gpointer task_data, GCancellable* cancellable) {
    (void)source;
    ListData* d = task_data;
    GError* error = NULL;
    ArchiveListing* l = archive_list(d->archive, ARCHIVE_TREE_MAX_ENTRIES, cancellable, &error);
    if (l == NULL) {
        d->error = g_strdup(error != NULL ? error->message : "cannot read the archive");
        g_clear_error(&error);
        g_task_return_boolean(task, FALSE);
        return;
    }
    d->incomplete = l->incomplete || l->n_entries > l->entries->len;
    d->root = arc_node_new("", "", NULL, TRUE);
    GHashTable* dirs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (guint i = 0; i < l->entries->len; i++) {
        const ArchiveEntry* e = &g_array_index(l->entries, ArchiveEntry, i);
        gchar* clean = archive_extract_clean_path(e->path);
        if (clean == NULL) {
            continue;
        }
        if (e->type == ARCHIVE_ENTRY_DIR) {
            ensure_dir(d->root, dirs, clean);
        } else {
            const gchar* slash = strrchr(clean, '/');
            ArcNode* parent = d->root;
            if (slash != NULL) {
                gchar* up = g_strndup(clean, (gsize)(slash - clean));
                parent = ensure_dir(d->root, dirs, up);
                g_free(up);
            }
            g_ptr_array_add(parent->kids,
                            arc_node_new(slash != NULL ? slash + 1 : clean, clean, e->path, FALSE));
        }
        g_free(clean);
    }
    g_hash_table_destroy(dirs);
    arc_node_sort(d->root);
    archive_listing_free(l);
    g_task_return_boolean(task, TRUE);
}

static void append_note(ExplorerRow* parent, const gchar* label) {
    explorer_row_append(parent, "-", "", label, "");
}

static void apply_node(ExplorerRow* parent, const gchar* archive, const ArcNode* node) {
    for (guint i = 0; i < node->kids->len; i++) {
        const ArcNode* k = g_ptr_array_index(node->kids, i);
        if (k->is_dir) {
            gchar* path = g_strdup_printf("%s!/%s", archive, k->clean);
            ExplorerRow* row = explorer_row_append_container(parent, "arc_dir", k->name, k->name, path);
            g_free(path);
            explorer_row_set_glyph(row, "folder");
            explorer_row_set_loaded(row, TRUE);
            apply_node(row, archive, k);
            if (k->kids->len == 0) {
                append_note(row, "  (empty)");
            }
        } else {
            gchar* path = g_strdup_printf("%s!/%s", archive, k->orig);
            ExplorerRow* row = explorer_row_append(parent, "arc_file", k->name, k->name, path);
            g_free(path);
            explorer_row_set_icon_name(row, k->name);
        }
    }
}

static void on_listed(GObject* source, GAsyncResult* result, gpointer user_data) {
    (void)source;
    ExplorerRow* row = user_data;
    GTask* task = G_TASK(result);
    ListData* d = g_task_get_task_data(task);
    gboolean ok = g_task_propagate_boolean(task, NULL);
    g_object_set_data(G_OBJECT(row), "arc-loading", NULL);
    AppData* app = g_object_get_data(G_OBJECT(row), "arc-app");
    if (row->parent != NULL && app != NULL &&
        explorer_tree_is_expanded(app->tree, row)) {
        /* Never leave the row without children: that collapses it */
        ExplorerRow* note = explorer_row_n_children(row) > 0 ? explorer_row_child(row, 0) : NULL;
        if (!ok) {
            gchar* label = g_strdup_printf("  (%s)", d->error != NULL ? d->error : "cannot read the archive");
            explorer_row_set_label(note, label);
            g_free(label);
        } else {
            apply_node(row, row->path, d->root);
            gboolean more = d->root->kids->len > 0;
            if (d->incomplete) {
                append_note(row, "  (more entries not shown)");
                more = TRUE;
            }
            if (more) {
                explorer_row_remove(note);
            } else {
                explorer_row_set_label(note, "  (empty)");
            }
            explorer_row_set_loaded(row, TRUE);
        }
    }
    g_object_unref(row);
}

static void start_listing(AppData* app, ExplorerRow* row) {
    if (g_object_get_data(G_OBJECT(row), "arc-loading") != NULL) {
        return;
    }
    g_object_set_data(G_OBJECT(row), "arc-loading", GINT_TO_POINTER(1));
    g_object_set_data(G_OBJECT(row), "arc-app", app);
    /* Reuse the placeholder: a row emptied while expanded collapses */
    ExplorerRow* first = explorer_row_n_children(row) == 1 ? explorer_row_child(row, 0) : NULL;
    if (first != NULL && explorer_row_is(first, "-")) {
        explorer_row_set_label(first, "  (reading...)");
    } else {
        append_note(row, "  (reading...)");
        while (explorer_row_n_children(row) > 1) {
            explorer_row_remove(explorer_row_child(row, 0));
        }
    }
    ListData* d = g_new0(ListData, 1);
    d->archive = g_strdup(row->path);
    GTask* task = g_task_new(NULL, NULL, on_listed, g_object_ref(row));
    g_task_set_task_data(task, d, list_data_free);
    g_task_run_in_thread(task, list_thread);
    g_object_unref(task);
}

gboolean archive_tree_handle_row_toggled(AppData* app, ExplorerRow* row, gboolean expanded) {
    if (explorer_row_is(row, "arc_dir")) {
        return TRUE;
    }
    if (!archive_tree_row_is_archive(row)) {
        return FALSE;
    }
    if (expanded) {
        if (!row->loaded) {
            start_listing(app, row);
        }
    } else if (g_object_get_data(G_OBJECT(row), "arc-loading") == NULL) {
        /* Drop the entries: the next expansion reads the archive again */
        explorer_row_remove_children(row);
        append_note(row, "...");
        explorer_row_set_loaded(row, FALSE);
    }
    return TRUE;
}

/* ---- Extraction ------------------------------------------------------ */

static GHashTable* in_flight = NULL; /* cache paths being extracted */

typedef struct {
    AppData* app;
    ExplorerRow* row;
    gchar* archive;
    gchar* entry;
    gchar* dest;
    gchar* error;
} ExtractData;

static void extract_data_free(gpointer data) {
    ExtractData* d = data;
    g_object_unref(d->row);
    g_free(d->archive);
    g_free(d->entry);
    g_free(d->dest);
    g_free(d->error);
    g_free(d);
}

static void extract_thread(GTask* task, gpointer source, gpointer task_data, GCancellable* cancellable) {
    (void)source;
    ExtractData* d = task_data;
    GError* error = NULL;
    gboolean ok = archive_extract_entry(d->archive, d->entry, d->dest, ARCHIVE_TREE_MAX_EXTRACT,
                                        cancellable, &error);
    if (!ok) {
        d->error = g_strdup(error != NULL ? error->message : "cannot extract the entry");
        g_clear_error(&error);
    }
    g_task_return_boolean(task, ok);
}

static void on_extracted(GObject* source, GAsyncResult* result, gpointer user_data) {
    (void)source;
    (void)user_data;
    GTask* task = G_TASK(result);
    ExtractData* d = g_task_get_task_data(task);
    gboolean ok = g_task_propagate_boolean(task, NULL);
    g_hash_table_remove(in_flight, d->dest);
    AppData* app = d->app;
    if (explorer_tree_get_selected(app->tree) == d->row) {
        if (ok) {
            g_signal_emit_by_name(app->tree, "selection-changed");
        } else if (app->buffer != NULL) {
            gchar* text = g_strdup_printf("%s\n\nCannot show this entry: %s\n", d->row->name,
                                          d->error != NULL ? d->error : "extraction failed");
            gtk_text_buffer_set_text(app->buffer, text, -1);
            g_free(text);
        }
    }
}

gchar* archive_tree_resolve(AppData* app, ExplorerRow* row) {
    ExplorerRow* arc = archive_of(row);
    if (arc == NULL || !explorer_row_is(row, "arc_file")) {
        return NULL;
    }
    const gchar* entry = row->path + strlen(arc->path) + 2; /* skip "!/" */
    gchar* dest = archive_extract_cache_path(arc->path, entry);
    if (dest == NULL) {
        return NULL;
    }
    if (g_file_test(dest, G_FILE_TEST_IS_REGULAR)) {
        return dest;
    }
    if (in_flight == NULL) {
        in_flight = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    }
    if (g_hash_table_contains(in_flight, dest)) {
        g_free(dest);
        return NULL;
    }
    g_hash_table_add(in_flight, g_strdup(dest));
    ExtractData* d = g_new0(ExtractData, 1);
    d->app = app;
    d->row = g_object_ref(row);
    d->archive = g_strdup(arc->path);
    d->entry = g_strdup(entry);
    d->dest = dest;
    GTask* task = g_task_new(NULL, NULL, on_extracted, NULL);
    g_task_set_task_data(task, d, extract_data_free);
    g_task_run_in_thread(task, extract_thread);
    g_object_unref(task);
    return NULL;
}
