#include "explorer/file_list.h"
#include "git/git_state.h"
#include "preview/preview_state.h"
#include "core/i18n.h"
#include "search/search.h"
#include "explorer/file_actions.h"
#include "explorer/file_moves.h"
#include "explorer/nav_history.h"
#include "explorer/fs_watch.h"
#include "core/app_events.h"
#include "theme/text_style.h"
#include "explorer/tree_nav.h"
#include "git/git_utils.h"
#include "core/file_types.h"
#include "git/git_tab.h"
#include "editor/editor.h"
#include "media/video.h"
#include "media/video_tab.h"
#include "media/audio.h"
#include "explorer/bookmarks.h"
#include "notes/notes.h"
#include "explorer/remote.h"
#include "games.h"
#include "explorer/archive_tree.h"
#include "formats/archive_extract.h"
#include "explorer/dir_scan.h"

G_STATIC_ASSERT((gint)DIR_SCAN_SORT_NAME == SORT_NAME && (gint)DIR_SCAN_SORT_DATE == SORT_DATE &&
                (gint)DIR_SCAN_SORT_SIZE == SORT_SIZE && (gint)DIR_SCAN_SORT_EXT == SORT_EXT);


static void update_tree_git_statuses_recursive(ExplorerRow* parent, const GitState* git) {
    for (guint i = 0; i < explorer_row_n_children(parent); i++) {
        ExplorerRow* row = explorer_row_child(parent, i);
        if (row->path[0] != '\0') {
            const gchar* badge = git_state_badge(git, row->path);
            explorer_row_set_badge(row, badge != NULL ? badge : "");
        }
        update_tree_git_statuses_recursive(row, git);
    }
}

typedef struct {
    AppData* app;
    gchar* base_dir;     /* app->base_dir when the scan started */
    GitState* state;     /* read by the scan */
} GitScanTaskData;

static void git_scan_task_data_free(gpointer data) {
    GitScanTaskData* td = (GitScanTaskData*)data;
    g_free(td->base_dir);
    git_state_free(td->state);
    g_free(td);
}

static void git_scan_thread_func(GTask* task, gpointer source_object,
                                 gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    (void)cancellable;
    GitScanTaskData* td = (GitScanTaskData*)task_data;
    gchar* dir = g_canonicalize_filename(td->base_dir, NULL);
    GHashTable* statuses = scan_git_statuses(dir);
    guint ahead = 0, behind = 0;
    gboolean has_upstream = git_read_ahead_behind(dir, &ahead, &behind);
    gchar* root = find_git_root(td->base_dir);
    td->state = git_state_new(statuses, root, has_upstream, ahead, behind);
    g_free(root);
    g_free(dir);
    g_task_return_boolean(task, TRUE);
}

/* One scan at a time: libgit2 cannot stop a status scan, so a scan asked
 * while one runs (files keep changing during a build) waits for its end
 * instead of piling up worker threads */
static gboolean git_scan_running = FALSE;
static gboolean git_scan_wanted = FALSE;
static GCancellable* git_scan_cancellable = NULL; /* the scan whose result is wanted */

static void start_async_git_scan(AppData* app);

static gboolean rescan_idle(gpointer data) {
    start_async_git_scan((AppData*)data);
    return G_SOURCE_REMOVE;
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
    git_scan_running = FALSE;
    if (git_scan_wanted) {
        /* Files changed during the scan: one more scan, after this result */
        git_scan_wanted = FALSE;
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, rescan_idle, app, NULL);
    }
    if (g_cancellable_is_cancelled(cancellable) ||
        cancellable != git_scan_cancellable ||
        app->sidebar_view != SIDEBAR_VIEW_FILES ||
        g_strcmp0(td->base_dir, app->base_dir) != 0) {
        return;
    }
    gboolean status_equal = git_state_same_badges(app->git_state, td->state);
    if (status_equal && git_state_same_counts(app->git_state, td->state)) {
        return;
    }
    git_state_free(app->git_state);
    app->git_state = td->state;
    td->state = NULL;

    if (!status_equal) {
        update_tree_git_statuses_recursive(explorer_tree_get_root(app->tree), app->git_state);
        app_emit(app, APP_EVENT_CHROME_CHANGED);
        ExplorerRow* sel = tree_nav_selected(app);
        if (sel != NULL && explorer_row_is(sel, "dir")) {
            app_emit(app, APP_EVENT_PREVIEW_STALE);
        }
    }
    app_emit(app, APP_EVENT_STATUS_CHANGED);
}

void cancel_git_status_scan(AppData* app) {
    (void)app;
    if (git_scan_cancellable != NULL) {
        g_cancellable_cancel(git_scan_cancellable);
        g_clear_object(&git_scan_cancellable);
    }
}

/* Read the git status badges of app->base_dir off the UI thread */
static void start_async_git_scan(AppData* app) {
    if (app->tree == NULL || app->base_dir == NULL ||
        app->sidebar_view != SIDEBAR_VIEW_FILES) {
        cancel_git_status_scan(app);
        return;
    }
    if (git_scan_running) {
        /* The running scan's result is kept only if it is still for this
         * directory (checked when it ends); a fresh scan follows it */
        git_scan_wanted = TRUE;
        return;
    }
    cancel_git_status_scan(app);
    git_scan_running = TRUE;
    git_scan_cancellable = g_cancellable_new();
    GitScanTaskData* td = g_new0(GitScanTaskData, 1);
    td->app = app;
    td->base_dir = g_strdup(app->base_dir);
    GTask* task = g_task_new(NULL, git_scan_cancellable, on_git_scan_ready, NULL);
    g_task_set_task_data(task, td, git_scan_task_data_free);
    g_task_run_in_thread(task, git_scan_thread_func);
    g_object_unref(task);
}

void refresh_git_status_in_place(AppData* app) {
    if (app != NULL) {
        start_async_git_scan(app);
    }
}

/* Show view in the explorer panel, or the files again when it is shown */
static void toggle_sidebar_view(AppData* app, gint view) {
    if (app == NULL) {
        return;
    }
    if (app->left_vbox != NULL && !gtk_widget_get_visible(app->left_vbox)) {
        gtk_widget_set_visible(app->left_vbox, TRUE);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    }
    explorer_set_view(app, app->sidebar_view == view ? SIDEBAR_VIEW_FILES : view);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    refresh_listing(app);
    /* Back to the files: the scan restores the selection */
    if (app->sidebar_view != SIDEBAR_VIEW_FILES || view != SIDEBAR_VIEW_BOOKMARKS) {
        select_first_row(app);
    }
}

void toggle_bookmarks_view(AppData* app) {
    toggle_sidebar_view(app, SIDEBAR_VIEW_BOOKMARKS);
}

void toggle_notes_view(AppData* app) {
    toggle_sidebar_view(app, SIDEBAR_VIEW_NOTES);
}

void toggle_remote_view(AppData* app) {
    toggle_sidebar_view(app, SIDEBAR_VIEW_REMOTE);
}

void toggle_games_view(AppData* app) {
    toggle_sidebar_view(app, SIDEBAR_VIEW_GAMES);
}


gboolean files_any_expanded(AppData* app) {
    return app != NULL && app->expanded_paths != NULL &&
           g_hash_table_size(app->expanded_paths) > 0;
}

static void start_async_dir_scan(AppData* app, const gchar* dirname,
                                 const gchar* select_path, const gchar* select_name,
                                 gboolean keep_view, gboolean preview, gboolean expand_all);

/* Expand all reads the folders on a worker thread (start_async_dir_scan) */
void toggle_collapse_expand_files(AppData* app) {
    if (app == NULL || app->tree == NULL) {
        return;
    }
    if (files_any_expanded(app)) {
        explorer_tree_collapse_all(app->tree);
        if (app->expanded_paths != NULL) {
            g_hash_table_remove_all(app->expanded_paths);
        }
    } else if (app->base_dir != NULL) {
        start_async_dir_scan(app, app->base_dir, NULL, NULL, TRUE, FALSE, TRUE);
    }
}

/* Set while a scan result re-expands its folders: the monitors and the
 * workspace are synced once at the end, not once per folder */
static gboolean batch_expanding = FALSE;

/* Placeholder child of an unread directory: keeps the expander visible until
 * the row is expanded and its contents are read */
static void append_placeholder(ExplorerRow* parent, const gchar* name, const gchar* label,
                               const gchar* path) {
    explorer_row_append(parent, "-", name, label, path);
}

/* Expanded level of the folder entry i of node, NULL if it was not expanded */
static const DirScanNode* entry_child_node(const DirScanNode* node, guint i) {
    return (node->children != NULL && i < node->children->len) ? g_ptr_array_index(node->children, i) : NULL;
}

static gchar* entry_path(const DirScanNode* node, guint i) {
    gchar* full = g_build_filename(node->dirname, g_array_index(node->entries, DirScanEntry, i).name, NULL);
    gchar* can_full = g_canonicalize_filename(full, NULL);
    g_free(full);
    return can_full;
}

static void apply_dir_node_to_rows(ExplorerRow* parent, AppData* app, const DirScanNode* node);

/* The row of entry i of node, inserted under parent at position (G_MAXUINT:
 * appended), with its expanded levels below it */
static ExplorerRow* insert_entry_row(ExplorerRow* parent, guint position, AppData* app,
                                     const DirScanNode* node, guint i) {
    const DirScanEntry* e = &g_array_index(node->entries, DirScanEntry, i);
    const DirScanNode* child_node = entry_child_node(node, i);
    gchar* can_full = entry_path(node, i);
    const gchar* git_badge = "";
    const gchar* b = (app != NULL) ? git_state_badge(app->git_state, can_full) : NULL;
    if (b != NULL) {
        git_badge = b;
    }
    ExplorerRow* row;
    if (e->is_dir) {
        /* Bare names: the tree draws the icons (explorer_tree.c) */
        gboolean is_expanded = (child_node != NULL);
        row = explorer_row_insert(parent, position, TRUE, "dir", e->name, e->name, can_full);
        explorer_row_set_glyph(row, "folder");
        explorer_row_set_badge(row, git_badge);
        explorer_row_set_loaded(row, is_expanded);
        if (is_expanded) {
            apply_dir_node_to_rows(row, app, child_node);
        } else {
            append_placeholder(row, "", "...", "");
        }
    } else {
        gboolean is_buf = (app != NULL && app->sidebar_view == SIDEBAR_VIEW_NOTES);
        gboolean as_archive = !is_buf && archive_extract_can_browse(e->name);
        row = explorer_row_insert(parent, position, as_archive, is_buf ? "buffer" : "file", e->name,
                                  e->name, can_full);
        explorer_row_set_icon_name(row, e->name);
        if (as_archive) {
            /* Rows start loaded: the first expansion must list the archive */
            explorer_row_set_loaded(row, FALSE);
            append_placeholder(row, "", "...", "");
        }
        explorer_row_set_badge(row, git_badge);
    }
    g_free(can_full);
    return row;
}

/* Apply a pre-scanned DirScanNode tree under parent (the root for the top level), on the main thread */
static void apply_dir_node_to_rows(ExplorerRow* parent, AppData* app, const DirScanNode* node) {
    gboolean top = parent->is_root;
    if (top && app != NULL) {
        if (app->sidebar_view == SIDEBAR_VIEW_NOTES) {
            notes_append_rows(parent, app);
            return;
        }
        if (app->sidebar_view == SIDEBAR_VIEW_BOOKMARKS) {
            bookmarks_append_rows(parent, app);
            return;
        }
    }

    if (node == NULL) {
        return;
    }

    if (node->unreadable) {
        append_placeholder(parent, "(cannot read directory)",
                           top ? _("(cannot read directory)") : _("  (cannot read directory)"),
                           node->dirname);
        return;
    }

    if (node->entries->len == 0 && !top) {
        append_placeholder(parent, "(empty)", _("  (empty)"), "");
        return;
    }

    for (guint i = 0; i < node->entries->len; i++) {
        insert_entry_row(parent, G_MAXUINT, app, node, i);
    }
}

/* Read one directory level into parent's children */
static void populate_level(ExplorerRow* parent, AppData* app, const gchar* dirname,
                           gboolean show_hidden, gint sort_mode) {
    GHashTable* exp = (app != NULL) ? app->expanded_paths : NULL;
    DirScanNode* node = dir_scan_read(dirname, show_hidden, (DirScanSort)sort_mode, exp, NULL);
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

/* ------------------------------------------------------------------------- */
/* Rescan results merged into the rows shown                                 */
/* ------------------------------------------------------------------------- */

/* A level with more changes than this is rebuilt (one model change instead
 * of many) */
#define MERGE_MAX_CHANGES 64

typedef struct {
    GPtrArray* new_dirs; /* folder rows inserted with an expanded level */
    GPtrArray* rebuilt;  /* rows whose children were all rebuilt */
} MergeResult;

/* The row shows entry i of node (same kind, name and path) */
static gboolean row_shows_entry(ExplorerRow* row, const DirScanNode* node, guint i) {
    const DirScanEntry* e = &g_array_index(node->entries, DirScanEntry, i);
    if (!explorer_row_is(row, e->is_dir ? "dir" : "file") || g_strcmp0(row->name, e->name) != 0) {
        return FALSE;
    }
    gchar* path = entry_path(node, i);
    gboolean same = (g_strcmp0(row->path, path) == 0);
    g_free(path);
    return same;
}

static void rebuild_level(AppData* app, ExplorerRow* parent, const DirScanNode* node, MergeResult* out) {
    if (parent->is_root) {
        explorer_tree_clear(app->tree);
    } else {
        explorer_row_remove_children(parent);
    }
    apply_dir_node_to_rows(parent, app, node);
    g_ptr_array_add(out->rebuilt, parent);
}

/* Bring the rows under parent to node with the fewest changes: the rows of
 * entries still there stay (with their expansion, selection and scroll
 * position), new entries are inserted and gone ones removed. A level whose
 * order changed (another sort) or that changed a lot is rebuilt. */
static void merge_level(AppData* app, ExplorerRow* parent, const DirScanNode* node, MergeResult* out) {
    guint n_old = explorer_row_n_children(parent);
    gboolean listable = node != NULL && !node->unreadable && node->entries != NULL &&
                        (node->entries->len > 0 || parent->is_root);
    ExplorerRow* first = explorer_row_child(parent, 0);
    if (!listable || (first != NULL && explorer_row_is(first, "-"))) {
        rebuild_level(app, parent, node, out); /* placeholder shown or wanted */
        return;
    }
    guint n_new = node->entries->len;
    GHashTable* index = g_hash_table_new(g_str_hash, g_str_equal); /* name -> entry index + 1 */
    for (guint i = 0; i < n_new; i++) {
        g_hash_table_insert(index, g_array_index(node->entries, DirScanEntry, i).name, GUINT_TO_POINTER(i + 1));
    }
    /* Plan: which rows stay (in the entries' order) */
    gint* kept = g_new(gint, MAX(n_old, 1)); /* entry index of each row, -1 when it goes */
    guint n_kept = 0;
    gint last = -1;
    gboolean ordered = TRUE;
    for (guint j = 0; j < n_old; j++) {
        ExplorerRow* row = explorer_row_child(parent, j);
        guint idx = GPOINTER_TO_UINT(g_hash_table_lookup(index, row->name));
        kept[j] = (idx > 0 && row_shows_entry(row, node, idx - 1)) ? (gint)idx - 1 : -1;
        if (kept[j] >= 0) {
            ordered = ordered && kept[j] > last;
            last = kept[j];
            n_kept++;
        }
    }
    guint changes = (n_old - n_kept) + (n_new - n_kept);
    if (!ordered || changes > MERGE_MAX_CHANGES) {
        g_free(kept);
        g_hash_table_destroy(index);
        rebuild_level(app, parent, node, out);
        return;
    }
    for (guint j = n_old; j-- > 0;) {
        if (kept[j] < 0) {
            explorer_row_remove_at(parent, j);
        }
    }
    g_free(kept);
    guint pos = 0;
    for (guint i = 0; i < n_new; i++) {
        ExplorerRow* row = explorer_row_child(parent, pos);
        const DirScanNode* child_node = entry_child_node(node, i);
        if (row != NULL && GPOINTER_TO_UINT(g_hash_table_lookup(index, row->name)) == i + 1) {
            /* Kept: its expanded level is merged too (when still expanded) */
            if (child_node != NULL && row->loaded && explorer_tree_is_expanded(app->tree, row)) {
                merge_level(app, row, child_node, out);
            }
        } else {
            row = insert_entry_row(parent, pos, app, node, i);
            if (child_node != NULL) {
                g_ptr_array_add(out->new_dirs, row);
            }
        }
        pos++;
    }
    g_hash_table_destroy(index);
}

/* The rows shown list dirname (its top level is up to date or nearly) */
static gboolean rows_list_dir(AppData* app, const gchar* dirname) {
    ExplorerRow* first = explorer_row_child(explorer_tree_get_root(app->tree), 0);
    if (first == NULL || explorer_row_is(first, "-") || first->path[0] == '\0' ||
        app->sidebar_view != SIDEBAR_VIEW_FILES) {
        return FALSE;
    }
    gchar* parent = g_path_get_dirname(first->path);
    gchar* dir = g_canonicalize_filename(dirname, NULL);
    gboolean same = (g_strcmp0(parent, dir) == 0);
    g_free(dir);
    g_free(parent);
    return same;
}

/* Merge a rescan of the directory shown into its rows, then expand the
 * folders it inserted that were expanded */
static void merge_scan(AppData* app, const DirScanNode* root_node) {
    MergeResult out = { g_ptr_array_new(), g_ptr_array_new() };
    merge_level(app, explorer_tree_get_root(app->tree), root_node, &out);
    for (guint i = 0; i < out.new_dirs->len; i++) {
        ExplorerRow* row = g_ptr_array_index(out.new_dirs, i);
        if (g_hash_table_contains(app->expanded_paths, row->path)) {
            explorer_tree_set_expanded(app->tree, row, TRUE);
            reexpand_paths_recursive(app, row);
        }
    }
    for (guint i = 0; i < out.rebuilt->len; i++) {
        reexpand_paths_recursive(app, g_ptr_array_index(out.rebuilt, i));
    }
    g_ptr_array_unref(out.new_dirs);
    g_ptr_array_unref(out.rebuilt);
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
        if (!batch_expanding) {
            sync_directory_monitors(app);
        }
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
        notes_handle_row_toggled(app, row, expanded) ||
        remote_handle_row_toggled(app, row, expanded) ||
        archive_tree_handle_row_toggled(app, row, expanded)) {
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
    if (!app->switching_ws && !app->dir_scan_pending && !batch_expanding) {
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
    gboolean expand_all;      /* also expand every folder (up to EXPAND_ALL_MAX_ROWS rows) */
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

/* Expand all stops opening folders past this many rows: they are built on the UI thread */
#define EXPAND_ALL_MAX_ROWS 10000

static void dir_scan_thread_func(GTask* task, gpointer source_object,
                                 gpointer task_data, GCancellable* cancellable) {
    (void)source_object;
    DirScanTaskData* td = (DirScanTaskData*)task_data;

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }

    if (td->expand_all) {
        dir_scan_collect_expand_all(td->dirname, td->show_hidden, EXPAND_ALL_MAX_ROWS,
                                    td->expanded_snapshot, cancellable);
    }
    td->root_node = dir_scan_read(td->dirname, td->show_hidden, (DirScanSort)td->sort_mode,
                                  td->expanded_snapshot, cancellable);

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }
    g_task_return_boolean(task, TRUE);
}

/* The rows under parent already show node: same entries in the same order,
 * same expanded folders (any doubt counts as a difference) */
static gboolean rows_match_node(AppData* app, ExplorerRow* parent, const DirScanNode* node) {
    if (parent->is_root &&
        app->sidebar_view != SIDEBAR_VIEW_FILES) {
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
        const DirScanEntry* e = &g_array_index(node->entries, DirScanEntry, i);
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

    if (td->expand_all && app->expanded_paths != NULL) {
        GHashTableIter iter;
        gpointer key;
        g_hash_table_iter_init(&iter, td->expanded_snapshot);
        while (g_hash_table_iter_next(&iter, &key, NULL)) {
            g_hash_table_add(app->expanded_paths, g_strdup((const gchar*)key));
        }
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

    /* The same directory again (files changed): merged into the rows shown,
     * so the rows left as they were keep their state */
    gboolean merged = rows_list_dir(app, td->dirname);
    batch_expanding = TRUE;
    if (merged) {
        merge_scan(app, td->root_node);
    } else {
        explorer_tree_clear(app->tree);
        apply_dir_node_to_rows(explorer_tree_get_root(app->tree), app, td->root_node);
        reexpand_paths_recursive(app, explorer_tree_get_root(app->tree));
        bookmarks_reexpand(app);
        notes_reexpand(app);
    }
    batch_expanding = FALSE;
    sync_directory_monitors(app);
    if (td->expand_all && !app->switching_ws) {
        app_emit(app, APP_EVENT_LAYOUT_CHANGED); /* the workspace keeps the expansion */
    }
    tree_nav_unblock_selection_handler(app);
    ExplorerRow* sel = merged ? tree_nav_selected(app) : NULL;
    if (sel == NULL || td->refresh_preview || g_strcmp0(sel->path, td->select_path) != 0) {
        restore_selection(app, td);
    }

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
                                 gboolean keep_view, gboolean preview, gboolean expand_all) {
    if (app->dir_cancellable != NULL) {
        g_cancellable_cancel(app->dir_cancellable);
        g_clear_object(&app->dir_cancellable);
    }
    app->dir_cancellable = g_cancellable_new();
    if (!keep_view) {
        app->dir_scan_pending = TRUE; /* the rows shown are not this listing's yet */
    }
    cancel_git_status_scan(app);

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
    td->expand_all = expand_all;

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
    search_forget_viewer_match(app);

    /* Stop monitoring previous viewed file */
    preview_state_set_file(app, NULL, NULL);
    app->file_dirty = FALSE;
    fs_watch_forget_dir_changes(app);
    monitor_file(app, NULL);
    monitor_directory(app);

    /* Scan directory asynchronously off the UI thread */
    start_async_dir_scan(app, new_dir, NULL, select_name, FALSE, FALSE, FALSE);
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
    if (app->sidebar_view == SIDEBAR_VIEW_GAMES) {
        games_refresh(app);
        return;
    }
    start_async_dir_scan(app, app->base_dir, select_path, NULL, FALSE, FALSE, FALSE);
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
    explorer_set_view(app, SIDEBAR_VIEW_FILES);
    gsize n = strlen(app->base_dir);
    gboolean inside = g_str_has_prefix(path, app->base_dir) && n > 0 &&
                      (path[n] == '/' || path[n] == '\\' ||
                       app->base_dir[n - 1] == '/' || app->base_dir[n - 1] == '\\');
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
    start_async_dir_scan(app, app->base_dir, path, NULL, FALSE, TRUE, FALSE);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
}

void cancel_listing_scans(AppData* app) {
    if (app->dir_cancellable != NULL) {
        g_cancellable_cancel(app->dir_cancellable);
        g_clear_object(&app->dir_cancellable);
    }
    app->dir_scan_pending = FALSE;
    cancel_git_status_scan(app);
}

void check_listing(AppData* app) {
    if (app == NULL || app->sidebar_view != SIDEBAR_VIEW_FILES || app->base_dir == NULL) {
        return;
    }
    start_async_dir_scan(app, app->base_dir, NULL, NULL, TRUE, FALSE, FALSE);
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
    app_emit(app, APP_EVENT_STATUS_CHANGED);
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
    } else if (g_strcmp0(type, "game") == 0 || g_strcmp0(type, "game_folder") == 0 || app->sidebar_view == SIDEBAR_VIEW_GAMES) {
        if (g_strcmp0(type, "game_folder") == 0) {
            explorer_tree_toggle_expanded(app->tree, row);
        } else {
            games_activate_selected(app);
        }
    } else if (g_strcmp0(type, "dir") == 0 || archive_tree_row_is_archive(row) ||
               g_strcmp0(type, "arc_dir") == 0) {
        explorer_tree_toggle_expanded(app->tree, row);
    } else if (g_strcmp0(type, "arc_file") == 0) {
        gchar* cached = archive_tree_resolve(app, row);
        if (cached != NULL) {
            gchar* name = g_strdup(row->name);
            editor_open_file(app, name, cached);
            g_free(name);
            g_free(cached);
        }
    } else if (g_strcmp0(type, "file") == 0 && row->path[0] != '\0') {
        FileKind kind = (g_strcmp0(row->path, preview_state_path(app)) == 0)
                            ? preview_state_kind(app)
                            : file_kind_of_path(row->path, row->name);
        if (kind == FILE_KIND_VIDEO) {
            /* A tab of its own, moved like an editor tab (Space plays it in the preview) */
            gchar* name = g_strdup(row->name);
            gchar* full = g_strdup(row->path);
            video_tab_open(app, name, full);
            g_free(name);
            g_free(full);
        } else if (file_kind_is_media(kind)) {
            if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
                app_layout_begin(app);
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
    gchar* dir = steps > 1 ? nav_history_go(app, back, steps)
                           : back ? nav_history_back(app) : nav_history_forward(app);
    if (dir == NULL) {
        app_notice(app, steps > 1 ? "That folder no longer exists"
                        : back ? "No earlier folder" : "No later folder", FALSE);
        return; /* the view shown stays */
    }
    explorer_set_view(app, SIDEBAR_VIEW_FILES);
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
    if (explorer_set_view(app, SIDEBAR_VIEW_FILES)) {
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
