#include "file_list.h"
#include "search.h"
#include "file_actions.h"
#include "file_moves.h"
#include "nav_history.h"
#include "fs_watch.h"
#include "app_events.h"
#include "text_style.h"
#include "tree_nav.h"
#include "git_utils.h"
#include "file_types.h"
#include "git_tab.h"
#include "editor.h"
#include "video.h"
#include "audio.h"
#include "bookmarks.h"
#include "notes.h"
#include "remote.h"

/* Sort: directories first, then according to active sort mode (thread-safe) */
static gint compare_entries_with_data(gconstpointer a, gconstpointer b, gpointer user_data) {
    const FileEntry* ea = (const FileEntry*)a;
    const FileEntry* eb = (const FileEntry*)b;
    gint sort_mode = GPOINTER_TO_INT(user_data);

    if (ea->is_dir != eb->is_dir) {
        return ea->is_dir ? -1 : 1;
    }

    gint res = 0;
    switch (sort_mode) {
    case SORT_DATE:
        res = (ea->mtime > eb->mtime) ? -1 : (ea->mtime < eb->mtime) ? 1 : 0; /* newest first */
        break;
    case SORT_SIZE:
        res = (ea->size > eb->size) ? -1 : (ea->size < eb->size) ? 1 : 0; /* largest first */
        break;
    case SORT_EXT: {
        gchar* xa = g_utf8_casefold(file_extension(ea->name), -1);
        gchar* xb = g_utf8_casefold(file_extension(eb->name), -1);
        res = g_strcmp0(xa, xb);
        g_free(xa);
        g_free(xb);
        break;
    }
    default: /* SORT_NAME */
        break;
    }

    if (res == 0) {
        res = g_utf8_collate(ea->name, eb->name); /* tie-breaker by name */
    }
    return res;
}


static void update_tree_git_statuses_recursive(ExplorerRow* parent, GHashTable* statuses) {
    for (guint i = 0; i < explorer_row_n_children(parent); i++) {
        ExplorerRow* row = explorer_row_child(parent, i);
        if (row->path[0] != '\0') {
            const gchar* badge = (statuses != NULL) ? g_hash_table_lookup(statuses, row->path) : NULL;
            explorer_row_set_badge(row, badge != NULL ? badge : "");
        }
        update_tree_git_statuses_recursive(row, statuses);
    }
}

/* Same keys and values (git status maps: path -> badge markup) */
static gboolean string_maps_equal(GHashTable* a, GHashTable* b) {
    guint na = (a != NULL) ? g_hash_table_size(a) : 0;
    guint nb = (b != NULL) ? g_hash_table_size(b) : 0;
    if (na != nb) {
        return FALSE;
    }
    if (na == 0) {
        return TRUE;
    }
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, a);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        if (g_strcmp0(value, g_hash_table_lookup(b, key)) != 0) {
            return FALSE;
        }
    }
    return TRUE;
}

typedef struct {
    AppData* app;
    gchar* base_dir;     /* app->base_dir when the scan started */
    GHashTable* statuses;
} GitScanTaskData;

static void git_scan_task_data_free(gpointer data) {
    GitScanTaskData* td = (GitScanTaskData*)data;
    g_free(td->base_dir);
    if (td->statuses != NULL) {
        g_hash_table_destroy(td->statuses);
    }
    g_free(td);
}

static void git_scan_thread_func(GTask* task, gpointer source_object,
                                 gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    (void)cancellable;
    GitScanTaskData* td = (GitScanTaskData*)task_data;
    gchar* dir = g_canonicalize_filename(td->base_dir, NULL);
    td->statuses = scan_git_statuses(dir);
    g_free(dir);
    g_task_return_boolean(task, TRUE);
}

/* Git badges arrive after the listing: update the rows (and a directory
 * preview, which shows git counts) only when something changed */
static void on_git_scan_ready(GObject* source_object, GAsyncResult* res,
                              gpointer user_data) {
    (void)source_object;
    (void)user_data;
    GTask* task = G_TASK(res);
    GCancellable* cancellable = g_task_get_cancellable(task);
    GitScanTaskData* td = (GitScanTaskData*)g_task_get_task_data(task);
    AppData* app = td->app;
    if (g_cancellable_is_cancelled(cancellable) ||
        cancellable != app->git_status_cancellable ||
        app->sidebar_view != SIDEBAR_VIEW_FILES ||
        g_strcmp0(td->base_dir, app->base_dir) != 0 ||
        string_maps_equal(app->git_statuses, td->statuses)) {
        return;
    }
    if (app->git_statuses != NULL) {
        g_hash_table_destroy(app->git_statuses);
    }
    app->git_statuses = td->statuses;
    td->statuses = NULL;

    update_tree_git_statuses_recursive(explorer_tree_get_root(app->tree), app->git_statuses);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    ExplorerRow* sel = tree_nav_selected(app);
    if (sel != NULL && explorer_row_is(sel, "dir")) {
        app_emit(app, APP_EVENT_PREVIEW_STALE);
    }
}

static void cancel_git_scan(AppData* app) {
    if (app->git_status_cancellable != NULL) {
        g_cancellable_cancel(app->git_status_cancellable);
        g_clear_object(&app->git_status_cancellable);
    }
}

/* Read the git status badges of app->base_dir off the UI thread */
static void start_async_git_scan(AppData* app) {
    cancel_git_scan(app);
    if (app->tree == NULL || app->base_dir == NULL ||
        app->sidebar_view != SIDEBAR_VIEW_FILES) {
        return;
    }
    app->git_status_cancellable = g_cancellable_new();
    GitScanTaskData* td = g_new0(GitScanTaskData, 1);
    td->app = app;
    td->base_dir = g_strdup(app->base_dir);
    GTask* task = g_task_new(NULL, app->git_status_cancellable, on_git_scan_ready, NULL);
    g_task_set_task_data(task, td, git_scan_task_data_free);
    g_task_run_in_thread(task, git_scan_thread_func);
    g_object_unref(task);
}

void refresh_git_status_in_place(AppData* app) {
    if (app != NULL) {
        start_async_git_scan(app);
    }
}

/* Off-thread directory scan snapshot node */
typedef struct DirScanNode {
    gchar* dirname;
    gboolean unreadable;
    GArray* entries;      /* FileEntry elements */
    GPtrArray* children;  /* DirScanNode* per entry (NULL if not expanded dir) */
} DirScanNode;

static void dir_scan_node_free(gpointer data) {
    DirScanNode* node = (DirScanNode*)data;
    if (node == NULL) {
        return;
    }
    g_free(node->dirname);
    if (node->entries != NULL) {
        for (guint i = 0; i < node->entries->len; i++) {
            g_free(g_array_index(node->entries, FileEntry, i).name);
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

/* Scan and sort one directory level (and any expanded subdirectories) off-thread */
static DirScanNode* scan_dir_node_recursive(const gchar* dirname,
                                            gboolean show_hidden,
                                            gint sort_mode,
                                            GHashTable* expanded_set,
                                            GCancellable* cancellable) {
    DirScanNode* node = g_new0(DirScanNode, 1);
    node->dirname = g_strdup(dirname);

    GDir* dir = g_dir_open(dirname, 0, NULL);
    if (dir == NULL) {
        node->unreadable = TRUE;
        return node;
    }

    node->entries = g_array_new(FALSE, FALSE, sizeof(FileEntry));
    const gchar* name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (cancellable != NULL && g_cancellable_is_cancelled(cancellable)) {
            g_dir_close(dir);
            return node;
        }
        if (!show_hidden && name[0] == '.') {
            continue;
        }
        gchar* full = g_build_filename(dirname, name, NULL);
        struct stat st;
        gboolean ok = (g_stat(full, &st) == 0);
        FileEntry e;
        e.name = g_strdup(name);
        e.is_dir = ok ? S_ISDIR(st.st_mode) : FALSE;
        e.mtime = ok ? (gint64)st.st_mtime : 0;
        e.size = (ok && !e.is_dir) ? (gint64)st.st_size : 0;
        g_free(full);
        g_array_append_vals(node->entries, &e, 1);
    }
    g_dir_close(dir);

    g_array_sort_with_data(node->entries, compare_entries_with_data,
                           GINT_TO_POINTER(sort_mode));

    node->children = g_ptr_array_sized_new(node->entries->len);
    for (guint i = 0; i < node->entries->len; i++) {
        const FileEntry* e = &g_array_index(node->entries, FileEntry, i);
        DirScanNode* child_node = NULL;
        if (e->is_dir && expanded_set != NULL) {
            gchar* full = g_build_filename(dirname, e->name, NULL);
            if (g_hash_table_contains(expanded_set, full)) {
                child_node = scan_dir_node_recursive(full, show_hidden, sort_mode,
                                                     expanded_set, cancellable);
            }
            g_free(full);
        }
        g_ptr_array_add(node->children, child_node);
    }

    return node;
}

void toggle_bookmarks_view(AppData* app) {
    if (app == NULL) {
        return;
    }
    if (app->left_vbox != NULL && !gtk_widget_get_visible(app->left_vbox)) {
        gtk_widget_set_visible(app->left_vbox, TRUE);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    }
    if (app->sidebar_view == SIDEBAR_VIEW_BOOKMARKS) {
        app->sidebar_view = SIDEBAR_VIEW_FILES;
        app->show_bookmarks = FALSE;
    } else {
        app->sidebar_view = SIDEBAR_VIEW_BOOKMARKS;
        app->show_bookmarks = TRUE;
    }
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    refresh_listing(app);
    if (app->sidebar_view != SIDEBAR_VIEW_FILES) {
        select_first_row(app);
    }
}

void toggle_notes_view(AppData* app) {
    if (app == NULL) {
        return;
    }
    if (app->left_vbox != NULL && !gtk_widget_get_visible(app->left_vbox)) {
        gtk_widget_set_visible(app->left_vbox, TRUE);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    }
    if (app->sidebar_view == SIDEBAR_VIEW_NOTES) {
        app->sidebar_view = SIDEBAR_VIEW_FILES;
        app->show_bookmarks = FALSE;
    } else {
        app->sidebar_view = SIDEBAR_VIEW_NOTES;
        app->show_bookmarks = FALSE;
    }
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    refresh_listing(app);
    select_first_row(app);
}

void toggle_remote_view(AppData* app) {
    if (app == NULL) {
        return;
    }
    if (app->left_vbox != NULL && !gtk_widget_get_visible(app->left_vbox)) {
        gtk_widget_set_visible(app->left_vbox, TRUE);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    }
    if (app->sidebar_view == SIDEBAR_VIEW_REMOTE) {
        app->sidebar_view = SIDEBAR_VIEW_FILES;
        app->show_bookmarks = FALSE;
    } else {
        app->sidebar_view = SIDEBAR_VIEW_REMOTE;
        app->show_bookmarks = FALSE;
    }
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    refresh_listing(app);
    select_first_row(app);
}


void toggle_collapse_expand_files(AppData* app) {
    if (app == NULL || app->tree == NULL) {
        return;
    }
    gboolean has_expanded = (app->expanded_paths != NULL &&
                             g_hash_table_size(app->expanded_paths) > 0);
    if (has_expanded) {
        explorer_tree_collapse_all(app->tree);
        if (app->expanded_paths != NULL) {
            g_hash_table_remove_all(app->expanded_paths);
        }
    } else {
        explorer_tree_expand_all(app->tree);
    }
}

/* Placeholder child of an unread directory: keeps the expander visible until
 * the row is expanded and its contents are read */
static void append_placeholder(ExplorerRow* parent, const gchar* name, const gchar* label,
                               const gchar* path) {
    explorer_row_append(parent, "-", name, label, path);
}

/* Apply a pre-scanned DirScanNode tree under parent (the root for the top level), on the main thread */
static void apply_dir_node_to_rows(ExplorerRow* parent, AppData* app, const DirScanNode* node) {
    gboolean top = parent->is_root;
    if (top && app != NULL) {
        if (app->sidebar_view == SIDEBAR_VIEW_NOTES) {
            notes_append_rows(parent, app);
            return;
        }
        if (app->sidebar_view == SIDEBAR_VIEW_BOOKMARKS || app->show_bookmarks) {
            bookmarks_append_rows(parent, app);
            return;
        }
    }

    if (node == NULL) {
        return;
    }

    if (node->unreadable) {
        append_placeholder(parent, "(cannot read directory)",
                           top ? "📄 (cannot read directory)" : "  (cannot read directory)",
                           node->dirname);
        return;
    }

    if (node->entries->len == 0 && !top) {
        append_placeholder(parent, "(empty)", "  (empty)", "");
        return;
    }

    for (guint i = 0; i < node->entries->len; i++) {
        const FileEntry* e = &g_array_index(node->entries, FileEntry, i);
        const DirScanNode* child_node =
            (node->children != NULL && i < node->children->len)
                ? (const DirScanNode*)g_ptr_array_index(node->children, i)
                : NULL;
        gchar* full = g_build_filename(node->dirname, e->name, NULL);
        gchar* can_full = g_canonicalize_filename(full, NULL);
        g_free(full);
        const gchar* git_badge = "";
        if (app != NULL && app->git_statuses != NULL) {
            const gchar* b = (const gchar*)g_hash_table_lookup(app->git_statuses, can_full);
            if (b != NULL) {
                git_badge = b;
            }
        }

        if (e->is_dir) {
            /* Bare names: no folder icon, files get theirs from the tree
             * (explorer_tree.c), as in VS Code */
            gboolean is_expanded = (child_node != NULL);
            ExplorerRow* row = explorer_row_append_container(parent, "dir", e->name, e->name, can_full);
            explorer_row_set_badge(row, git_badge);
            explorer_row_set_loaded(row, is_expanded);
            if (is_expanded) {
                apply_dir_node_to_rows(row, app, child_node);
            } else {
                append_placeholder(row, "", "...", "");
            }
        } else {
            gboolean is_buf = (app != NULL && app->sidebar_view == SIDEBAR_VIEW_NOTES);
            gchar* label = is_buf ? g_strconcat("📝 ", e->name, NULL) : g_strdup(e->name);
            ExplorerRow* row = explorer_row_append(parent, is_buf ? "buffer" : "file", e->name, label, can_full);
            g_free(label);
            if (!is_buf) {
                explorer_row_set_icon_name(row, e->name);
            }
            explorer_row_set_badge(row, git_badge);
        }
        g_free(can_full);
    }
}

/* Read one directory level into parent's children */
static void populate_level(ExplorerRow* parent, AppData* app, const gchar* dirname,
                           gboolean show_hidden, gint sort_mode) {
    GHashTable* exp = (app != NULL) ? app->expanded_paths : NULL;
    DirScanNode* node = scan_dir_node_recursive(dirname, show_hidden, sort_mode, exp, NULL);
    apply_dir_node_to_rows(parent, app, node);
    dir_scan_node_free(node);
}

/* Fill the tree with a directory listing, synchronously (start-up); the git
 * badges follow from a worker thread */
void populate_tree(AppData* app, const gchar* dirname, gboolean show_hidden, gint sort_mode) {
    explorer_tree_clear(app->tree);
    populate_level(explorer_tree_get_root(app->tree), app, dirname, show_hidden, sort_mode);
    start_async_git_scan(app);
}

/* Recursively expand directories that were previously expanded */
void reexpand_paths_recursive(AppData* app, ExplorerRow* parent) {
    for (guint i = 0; i < explorer_row_n_children(parent); i++) {
        ExplorerRow* child = explorer_row_child(parent, i);
        if (explorer_row_is(child, "dir") && g_hash_table_contains(app->expanded_paths, child->path)) {
            explorer_tree_set_expanded(app->tree, child, TRUE);
            reexpand_paths_recursive(app, child);
        }
    }
}

/* Directory row expanded: read its contents (first time) and record the path */
static void on_dir_expanded(AppData* app, ExplorerRow* row) {
    if (!row->loaded && row->path[0] != '\0') {
        explorer_row_remove_children(row);
        populate_level(row, app, row->path, app->show_hidden, app->sort_mode);
        explorer_row_set_loaded(row, TRUE);
    }
    if (app->expanded_paths != NULL) {
        g_hash_table_add(app->expanded_paths, g_strdup(row->path));
        sync_directory_monitors(app);
    }
}

/* Directory row collapsed: forget it (and its subdirectories) and drop the
 * children so the next expansion reads the directory again */
static void on_dir_collapsed(AppData* app, ExplorerRow* row) {
    if (app->expanded_paths != NULL) {
        gchar* prefix = g_strdup_printf("%s/", row->path);
        GHashTableIter hiter;
        gpointer hkey;
        g_hash_table_iter_init(&hiter, app->expanded_paths);
        while (g_hash_table_iter_next(&hiter, &hkey, NULL)) {
            const gchar* p = (const gchar*)hkey;
            if (g_strcmp0(p, row->path) == 0 || g_str_has_prefix(p, prefix)) {
                g_hash_table_iter_remove(&hiter);
            }
        }
        g_free(prefix);
        sync_directory_monitors(app);
    }
    explorer_row_remove_children(row);
    append_placeholder(row, "", "...", "");
    explorer_row_set_loaded(row, FALSE);
}

/* A row was expanded or collapsed (explorer_tree "row-toggled") */
void on_tree_row_toggled(ExplorerTree* tree, ExplorerRow* row, gboolean expanded, gpointer user_data) {
    (void)tree;
    AppData* app = (AppData*)user_data;
    if (bookmarks_handle_row_toggled(app, row, expanded) ||
        notes_handle_row_toggled(app, row, expanded)) {
        return;
    }
    if (!explorer_row_is(row, "dir")) {
        return;
    }
    if (expanded) {
        on_dir_expanded(app, row);
    } else {
        on_dir_collapsed(app, row);
    }
    if (!app->switching_ws && !app->dir_scan_pending) {
        app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    }
}

typedef struct {
    AppData* app;
    gchar* dirname;
    gboolean show_hidden;
    gint sort_mode;
    GHashTable* expanded_snapshot;
    gchar* select_path;
    gchar* select_name;
    DirScanNode* root_node;
    gboolean refresh_preview; /* workspace switch or reveal: preview the restored selection */
    gboolean keep_view;       /* check of rows already shown: keep their selection and preview */
} DirScanTaskData;

static void dir_scan_task_data_free(gpointer data) {
    DirScanTaskData* td = (DirScanTaskData*)data;
    if (td == NULL) {
        return;
    }
    g_free(td->dirname);
    if (td->expanded_snapshot != NULL) {
        g_hash_table_destroy(td->expanded_snapshot);
    }
    g_free(td->select_path);
    g_free(td->select_name);
    dir_scan_node_free(td->root_node);
    g_free(td);
}

static GHashTable* clone_string_set(GHashTable* src) {
    GHashTable* copy = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (src != NULL) {
        GHashTableIter iter;
        gpointer key;
        g_hash_table_iter_init(&iter, src);
        while (g_hash_table_iter_next(&iter, &key, NULL)) {
            g_hash_table_add(copy, g_strdup((const gchar*)key));
        }
    }
    return copy;
}

static void dir_scan_thread_func(GTask* task, gpointer source_object,
                                 gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    DirScanTaskData* td = (DirScanTaskData*)task_data;

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }

    td->root_node = scan_dir_node_recursive(td->dirname, td->show_hidden,
                                            td->sort_mode, td->expanded_snapshot,
                                            cancellable);

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }
    g_task_return_boolean(task, TRUE);
}

/* The rows under parent already show node: same entries in the same order,
 * same expanded folders (any doubt counts as a difference) */
static gboolean rows_match_node(AppData* app, ExplorerRow* parent, const DirScanNode* node) {
    if (parent->is_root &&
        (app->sidebar_view != SIDEBAR_VIEW_FILES || app->show_bookmarks)) {
        return FALSE;
    }
    if (node == NULL || node->unreadable || node->entries == NULL) {
        return FALSE;
    }
    guint n = node->entries->len;
    if (n == 0 && !parent->is_root) {
        ExplorerRow* only = (explorer_row_n_children(parent) == 1) ? explorer_row_child(parent, 0) : NULL;
        return only != NULL && explorer_row_is(only, "-") && g_strcmp0(only->name, "(empty)") == 0;
    }
    if (explorer_row_n_children(parent) != n) {
        return FALSE;
    }
    for (guint i = 0; i < n; i++) {
        const FileEntry* e = &g_array_index(node->entries, FileEntry, i);
        ExplorerRow* row = explorer_row_child(parent, i);
        if (!explorer_row_is(row, e->is_dir ? "dir" : "file") || g_strcmp0(row->name, e->name) != 0) {
            return FALSE;
        }
        gchar* full = g_build_filename(node->dirname, e->name, NULL);
        gchar* can_full = g_canonicalize_filename(full, NULL);
        gboolean same_path = (g_strcmp0(row->path, can_full) == 0);
        g_free(can_full);
        g_free(full);
        if (!same_path) {
            return FALSE;
        }
        if (e->is_dir) {
            const DirScanNode* child_node = (node->children != NULL && i < node->children->len)
                                                ? g_ptr_array_index(node->children, i)
                                                : NULL;
            gboolean expanded = (child_node != NULL);
            if (row->loaded != expanded || explorer_tree_is_expanded(app->tree, row) != expanded) {
                return FALSE;
            }
            if (expanded && !rows_match_node(app, row, child_node)) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

/* Select the row asked for when the scan started (the preview handler is
 * blocked while a path is restored: a plain refresh keeps the preview as it is) */
static void restore_selection(AppData* app, const DirScanTaskData* td) {
    if (td->select_path != NULL) {
        tree_nav_block_selection_handler(app);
        gboolean found = select_by_path(app, td->select_path);
        tree_nav_unblock_selection_handler(app);
        if (!found) {
            select_first_row(app);
        }
    } else if (td->select_name == NULL || !select_by_name(app, td->select_name)) {
        select_first_row(app);
    }
    /* After a workspace switch the preview must show this workspace's
     * selection instead of the previous one */
    if (td->refresh_preview) {
        g_signal_emit_by_name(app->tree, "selection-changed");
    }
}

static void on_dir_scan_ready(GObject* source_object, GAsyncResult* res,
                              gpointer user_data) {
    (void)source_object;
    (void)user_data;
    GTask* task = G_TASK(res);
    GCancellable* cancellable = g_task_get_cancellable(task);
    DirScanTaskData* td = (DirScanTaskData*)g_task_get_task_data(task);

    if (td == NULL || g_cancellable_is_cancelled(cancellable) ||
        cancellable != td->app->dir_cancellable) {
        return;
    }

    AppData* app = td->app;
    app->dir_scan_pending = FALSE;

    if (td->root_node == NULL) {
        return;
    }

    if (rows_match_node(app, explorer_tree_get_root(app->tree), td->root_node)) {
        /* Nothing changed: the rows, their expansion and the scroll position stay */
        if (td->keep_view && tree_nav_selected(app) != NULL) {
            start_async_git_scan(app);
            return;
        }
        restore_selection(app, td);
        app_emit(app, APP_EVENT_CHROME_CHANGED);
        start_async_git_scan(app);
        return;
    }

    if (td->keep_view) {
        /* The rows shown are rebuilt: keep the row selected now, not the one
         * selected when the scan started */
        ExplorerRow* sel = tree_nav_selected(app);
        if (sel != NULL && sel->path[0] != '\0') {
            g_free(td->select_path);
            td->select_path = g_strdup(sel->path);
        }
    }

    tree_nav_block_selection_handler(app);

    gint saved_page = -1;
    if (app->notebook != NULL && !app->editor_dedicated) {
        saved_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
    }

    explorer_tree_clear(app->tree);
    apply_dir_node_to_rows(explorer_tree_get_root(app->tree), app, td->root_node);
    reexpand_paths_recursive(app, explorer_tree_get_root(app->tree));
    bookmarks_reexpand(app);
    notes_reexpand(app);
    sync_directory_monitors(app);
    tree_nav_unblock_selection_handler(app);
    restore_selection(app, td);

    if (saved_page > 0 && app->notebook != NULL && !app->editor_dedicated) {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), saved_page);
    }

    app_emit(app, APP_EVENT_CHROME_CHANGED);
    git_tab_update(app, app->base_dir);
    /* Rows got the badges known so far (kept from the previous scan: they are
     * keyed by full path, so another repository's never match); git runs now */
    start_async_git_scan(app);
}

static void start_async_dir_scan(AppData* app, const gchar* dirname,
                                 const gchar* select_path, const gchar* select_name,
                                 gboolean keep_view, gboolean preview) {
    if (app->dir_cancellable != NULL) {
        g_cancellable_cancel(app->dir_cancellable);
        g_clear_object(&app->dir_cancellable);
    }
    app->dir_cancellable = g_cancellable_new();
    if (!keep_view) {
        app->dir_scan_pending = TRUE; /* the rows shown are not this listing's yet */
    }
    cancel_git_scan(app);

    DirScanTaskData* td = g_new0(DirScanTaskData, 1);
    td->app = app;
    td->dirname = g_canonicalize_filename(dirname, NULL);
    td->show_hidden = app->show_hidden;
    td->sort_mode = app->sort_mode;
    td->expanded_snapshot = clone_string_set(app->expanded_paths);
    td->select_path = (select_path != NULL) ? g_canonicalize_filename(select_path, NULL) : NULL;
    td->select_name = g_strdup(select_name);
    td->refresh_preview = preview || (app->switching_ws && !keep_view);
    td->keep_view = keep_view;

    GTask* task = g_task_new(NULL, app->dir_cancellable, on_dir_scan_ready, NULL);
    g_task_set_task_data(task, td, dir_scan_task_data_free);
    g_task_run_in_thread(task, dir_scan_thread_func);
    g_object_unref(task);
}

static void reload_directory_with_selection(AppData* app, const gchar* new_dir,
                                            const gchar* select_name) {
    if (!g_file_test(new_dir, G_FILE_TEST_IS_DIR)) {
        return;
    }

    if (app->preview_cancellable != NULL) {
        g_cancellable_cancel(app->preview_cancellable);
        g_clear_object(&app->preview_cancellable);
    }

    gchar* old_dir = app->base_dir;
    app->base_dir = g_canonicalize_filename(new_dir, NULL);
    if (!app->switching_ws) {
        ExplorerRow* sel = tree_nav_selected(app);
        nav_history_folder_changed(app, old_dir, app->base_dir, sel != NULL ? sel->path : NULL);
    }
    g_free(old_dir);
    app->dir_scan_pending = TRUE;
    if (!app->switching_ws && app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
        g_free(app->workspaces[app->current_ws].selected_path);
        app->workspaces[app->current_ws].selected_path = NULL;
    }

    /* Clear prior search term */
    search_clear_explorer(app);

    /* Reset expanded paths when changing root directory */
    if (app->expanded_paths != NULL) {
        g_hash_table_remove_all(app->expanded_paths);
    }

    app_emit(app, APP_EVENT_CHROME_CHANGED);

    /* Clear right pane */
    audio_preview_stop(app);
    video_preview_stop(app);
    if (app->preview_stack != NULL) {
        gtk_stack_set_visible_child_name(GTK_STACK(app->preview_stack), "text");
    }
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_NONE);
    set_view_text_padding(app->text_view);
    set_view_monospace(app->text_view, FALSE);
    gtk_text_buffer_set_text(app->buffer, "", -1);
    app->viewer_match = -1;

    /* Stop monitoring previous viewed file */
    g_free(app->viewed_path);
    g_free(app->viewed_name);
    app->viewed_path = NULL;
    app->viewed_name = NULL;
    app->file_dirty = FALSE;
    app->dir_dirty = FALSE;
    monitor_file(app, NULL);
    monitor_directory(app);

    /* Scan directory asynchronously off the UI thread */
    start_async_dir_scan(app, new_dir, NULL, select_name, FALSE, FALSE);
}

/* Navigate to a new directory (Enter / Backspace) */
void reload_directory(AppData* app, const gchar* new_dir) {
    reload_directory_with_selection(app, new_dir, NULL);
}

/* Open a location: enter a directory, or enter a file's directory and select the file */
void navigate_to_path(AppData* app, const gchar* path) {
    if (app == NULL || path == NULL) {
        return;
    }
    if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
        reload_directory_with_selection(app, path, NULL);
        return;
    }
    gchar* parent = g_path_get_dirname(path);
    gchar* base = g_path_get_basename(path);
    reload_directory_with_selection(app, parent, base);
    g_free(base);
    g_free(parent);
}

/* Refresh the list while preserving selected entry and open expansions */
void refresh_listing_with_path(AppData* app, const gchar* select_path) {
    if (app->sidebar_view == SIDEBAR_VIEW_BOOKMARKS) {
        /* Bookmarks live in memory: rebuild synchronously, keeping the selected item */
        bookmarks_refresh(app);
        return;
    }
    if (app->sidebar_view == SIDEBAR_VIEW_NOTES) {
        /* Notes live in memory & manifest: rebuild synchronously, keeping selected item */
        notes_refresh(app);
        return;
    }
    if (app->sidebar_view == SIDEBAR_VIEW_REMOTE) {
        /* Remote machines live in memory & config: rebuild synchronously */
        remote_refresh(app);
        return;
    }
    start_async_dir_scan(app, app->base_dir, select_path, NULL, FALSE, FALSE);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
}

void reveal_path_in_tree(AppData* app, const gchar* path) {
    if (app == NULL || path == NULL || app->base_dir == NULL) {
        return;
    }
    if (app->left_vbox != NULL && !gtk_widget_get_visible(app->left_vbox)) {
        gtk_widget_set_visible(app->left_vbox, TRUE);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    }
    app->sidebar_view = SIDEBAR_VIEW_FILES;
    app->show_bookmarks = FALSE;
    gsize n = strlen(app->base_dir);
    gboolean inside = g_str_has_prefix(path, app->base_dir) && n > 0 &&
                      (path[n] == '/' || app->base_dir[n - 1] == '/');
    if (!inside) {
        navigate_to_path(app, path);
        return;
    }
    /* Expand every folder between the root and the entry */
    gchar* dir = g_path_get_dirname(path);
    while (app->expanded_paths != NULL && strlen(dir) > n && g_str_has_prefix(dir, app->base_dir)) {
        g_hash_table_add(app->expanded_paths, g_strdup(dir));
        gchar* up = g_path_get_dirname(dir);
        g_free(dir);
        dir = up;
    }
    g_free(dir);
    /* Unlike a plain refresh, the revealed entry is previewed */
    start_async_dir_scan(app, app->base_dir, path, NULL, FALSE, TRUE);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
}

void cancel_listing_scans(AppData* app) {
    if (app->dir_cancellable != NULL) {
        g_cancellable_cancel(app->dir_cancellable);
        g_clear_object(&app->dir_cancellable);
    }
    app->dir_scan_pending = FALSE;
    cancel_git_scan(app);
}

void check_listing(AppData* app) {
    if (app == NULL || app->sidebar_view != SIDEBAR_VIEW_FILES || app->base_dir == NULL) {
        return;
    }
    start_async_dir_scan(app, app->base_dir, NULL, NULL, TRUE, FALSE);
}

void refresh_listing(AppData* app) {
    ExplorerRow* row = tree_nav_selected(app);
    gchar* cur_path = (row != NULL) ? g_strdup(row->path) : NULL;
    refresh_listing_with_path(app, cur_path);
    g_free(cur_path);
}

/* Toggle visibility of hidden files (dot-prefixed) */
void toggle_hidden_files(AppData* app) {
    app->show_hidden = !app->show_hidden;
    refresh_listing(app);
}

/* Cycle sort mode: name -> date -> size -> extension */
void cycle_sort_mode(AppData* app) {
    app->sort_mode = (app->sort_mode + 1) % SORT_COUNT;
    refresh_listing(app);
}

/* Enter / double-click on a list entry: toggle directory expand/collapse or open editor tab */
void activate_selected_entry(AppData* app) {
    ExplorerRow* row = tree_nav_selected(app);
    if (row == NULL) {
        return;
    }
    const gchar* type = row->type;

    if (g_strcmp0(type, "note") == 0 || g_strcmp0(type, "notebook") == 0 ||
        g_strcmp0(type, "trash") == 0 || g_strcmp0(type, "trash_note") == 0 ||
        g_strcmp0(type, "buffer") == 0) {
        notes_activate_selected(app);
    } else if (g_strcmp0(type, "bookmark") == 0 || g_strcmp0(type, "bm_folder") == 0) {
        bookmarks_activate_selected(app);
    } else if (g_strcmp0(type, "remote") == 0 || app->sidebar_view == SIDEBAR_VIEW_REMOTE) {
        remote_activate_selected(app);
    } else if (g_strcmp0(type, "dir") == 0) {
        explorer_tree_toggle_expanded(app->tree, row);
    } else if (g_strcmp0(type, "file") == 0 && row->path[0] != '\0') {
        FileKind kind = (g_strcmp0(row->path, app->viewed_path) == 0)
                            ? app->viewed_kind
                            : file_kind_of_path(row->path, row->name);
        if (file_kind_is_media(kind)) {
            if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
                app_emit(app, APP_EVENT_LAYOUT_CHANGED);
                app->applying_layout = TRUE;
                gtk_widget_set_visible(app->viewer_box, TRUE);
                app_emit(app, APP_EVENT_CHROME_CHANGED);
                app_emit(app, APP_EVENT_LAYOUT_REBUILT);
            }
            if (app->notebook != NULL) {
                gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
            }
        } else {
            gchar* name = g_strdup(row->name); /* the row may go away while opening */
            gchar* full = g_strdup(row->path);
            editor_open_file(app, name, full);
            g_free(name);
            g_free(full);
        }
    }
}

/* Double click on a row (explorer_tree "row-activated"): same action as Enter */
void on_row_activated(ExplorerTree* tree, ExplorerRow* row, gpointer data) {
    (void)tree;
    AppData* app = (AppData*)data;
    if (tree_nav_selected(app) != row) {
        select_row(app, row);
    }
    activate_selected_entry(app);
}

/* Treeview key press handler: j k gg G zz / n N BackSpace ? i o Ctrl+B Ctrl+F f m space l Right h Left */
/* Run a file action, then refresh the tree selecting what it returned */
static void run_file_action(AppData* app, gchar* (*action)(AppData*)) {
    gchar* select = action(app);
    if (select != NULL) {
        refresh_listing_with_path(app, select);
        g_free(select);
    }
}

void create_new_file_in_tree(AppData* app) {
    run_file_action(app, file_action_new_file);
}

void create_new_folder_in_tree(AppData* app) {
    run_file_action(app, file_action_new_folder);
}

void rename_selected_in_tree(AppData* app) {
    run_file_action(app, file_action_rename_selected);
}

void trash_selected_in_tree(AppData* app) {
    run_file_action(app, file_action_trash_selected);
}

/* Shift+H / Shift+L, mouse back / forward, the top bar arrows: the folder
 * shown steps before / after */
static void walk_history(AppData* app, gboolean back, guint steps) {
    if (app->sidebar_view != SIDEBAR_VIEW_FILES || app->show_bookmarks) {
        app->sidebar_view = SIDEBAR_VIEW_FILES;
        app->show_bookmarks = FALSE;
    }
    gchar* dir = steps > 1 ? nav_history_go(app, back, steps)
                           : back ? nav_history_back(app) : nav_history_forward(app);
    if (dir == NULL) {
        app_notice(app, steps > 1 ? "That folder no longer exists"
                        : back ? "No earlier folder" : "No later folder", FALSE);
        return;
    }
    /* The entry selected when the folder was left; else, coming back to a
     * folder from one of its subfolders, that subfolder */
    gchar* select = g_strdup(nav_history_selection(app, dir));
    gchar* parent = g_path_get_dirname(app->base_dir);
    if (select == NULL && g_strcmp0(parent, dir) == 0) {
        select = g_path_get_basename(app->base_dir);
    }
    reload_directory_with_selection(app, dir, select);
    g_free(select);
    g_free(parent);
    g_free(dir);
}

void go_back_directory(AppData* app) {
    walk_history(app, TRUE, 1);
}

void go_forward_directory(AppData* app) {
    walk_history(app, FALSE, 1);
}

void go_history_directory(AppData* app, gboolean back, guint steps) {
    if (steps > 0) walk_history(app, back, steps);
}

void undo_last_file_move(AppData* app) {
    run_file_action(app, file_moves_undo);
}

/* Backspace: parent directory, or back to the files view from the other views */
void go_to_parent_directory(AppData* app) {
    if (app->sidebar_view != SIDEBAR_VIEW_FILES || app->show_bookmarks) {
        app->sidebar_view = SIDEBAR_VIEW_FILES;
        app->show_bookmarks = FALSE;
        refresh_listing(app);
        return;
    }
    gchar* parent = g_path_get_dirname(app->base_dir);
    if (g_strcmp0(parent, app->base_dir) != 0) {
        /* Remember directory left to highlight it again in parent */
        gchar* came_from = g_path_get_basename(app->base_dir);
        reload_directory_with_selection(app, parent, came_from);
        g_free(came_from);
    }
    g_free(parent);
}
