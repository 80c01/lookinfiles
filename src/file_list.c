#include "file_list.h"
#include "fs_watch.h"
#include "panels.h"
#include "app_events.h"
#include "text_style.h"
#include "tree_nav.h"
#include "help.h"
#include "git_utils.h"
#include "file_types.h"
#include "git_tab.h"
#include "editor.h"
#include "video.h"
#include "audio.h"
#include "search.h"
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


static void update_tree_git_statuses_recursive(GtkTreeModel* model, GtkTreeIter* parent,
                                               GHashTable* statuses) {
    GtkTreeIter iter;
    if (!gtk_tree_model_iter_children(model, &iter, parent)) {
        return;
    }
    do {
        gchar* path = NULL;
        gchar* old_badge = NULL;
        gtk_tree_model_get(model, &iter,
                           COL_PATH, &path,
                           COL_GIT_STATUS, &old_badge,
                           -1);

        if (path != NULL && path[0] != '\0') {
            const gchar* new_badge = "";
            if (statuses != NULL) {
                const gchar* b = (const gchar*)g_hash_table_lookup(statuses, path);
                if (b != NULL) {
                    new_badge = b;
                }
            }
            if (g_strcmp0(old_badge, new_badge) != 0) {
                gtk_tree_store_set(GTK_TREE_STORE(model), &iter,
                                   COL_GIT_STATUS, new_badge,
                                   -1);
            }
        }
        g_free(path);
        g_free(old_badge);

        /* Recurse into children */
        update_tree_git_statuses_recursive(model, &iter, statuses);
    } while (gtk_tree_model_iter_next(model, &iter));
}

void refresh_git_status_in_place(AppData* app) {
    if (app == NULL || app->model == NULL) {
        return;
    }
    if (app->sidebar_view != SIDEBAR_VIEW_FILES) {
        return;
    }
    if (app->base_dir == NULL) {
        return;
    }

    GHashTable* new_statuses = scan_git_statuses(app->base_dir);

    if (app->git_statuses != NULL) {
        g_hash_table_destroy(app->git_statuses);
        app->git_statuses = NULL;
    }
    app->git_statuses = new_statuses;

    update_tree_git_statuses_recursive(app->model, NULL, app->git_statuses);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
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
    if (app == NULL || app->tree_view == NULL) {
        return;
    }
    gboolean has_expanded = (app->expanded_paths != NULL &&
                             g_hash_table_size(app->expanded_paths) > 0);
    if (has_expanded) {
        gtk_tree_view_collapse_all(app->tree_view);
        if (app->expanded_paths != NULL) {
            g_hash_table_remove_all(app->expanded_paths);
        }
    } else {
        gtk_tree_view_expand_all(app->tree_view);
    }
}

/* Apply a pre-scanned DirScanNode tree into GtkTreeStore on the main thread */
static void apply_dir_node_to_store(GtkTreeStore* store, AppData* app, GtkTreeIter* parent,
                                    const DirScanNode* node) {
    if (parent == NULL && app != NULL) {
        if (app->sidebar_view == SIDEBAR_VIEW_NOTES) {
            notes_append_to_store(store, app);
            return;
        }
        if (app->sidebar_view == SIDEBAR_VIEW_BOOKMARKS || app->show_bookmarks) {
            bookmarks_append_to_store(store, app);
            return;
        }
    }

    if (node == NULL) {
        return;
    }

    if (node->unreadable) {
        GtkTreeIter iter;
        gtk_tree_store_append(store, &iter, parent);
        gtk_tree_store_set(store, &iter,
                           COL_NAME, "(cannot read directory)",
                           COL_TYPE, "-",
                           COL_LABEL, (parent == NULL) ? "📄 (cannot read directory)"
                                                       : "  (cannot read directory)",
                           COL_PATH, node->dirname,
                           COL_LOADED, TRUE,
                           COL_GIT_STATUS, "",
                           -1);
        return;
    }

    if (node->entries->len == 0 && parent != NULL) {
        GtkTreeIter iter;
        gtk_tree_store_append(store, &iter, parent);
        gtk_tree_store_set(store, &iter,
                           COL_NAME, "(empty)",
                           COL_TYPE, "-",
                           COL_LABEL, "  (empty)",
                           COL_PATH, "",
                           COL_LOADED, TRUE,
                           COL_GIT_STATUS, "",
                           -1);
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

        GtkTreeIter iter;
        gtk_tree_store_append(store, &iter, parent);

        if (e->is_dir) {
            gboolean is_expanded = (child_node != NULL);
            const gchar* icon = is_expanded ? "📂" : "📁";
            gchar* label = g_strconcat(icon, " ", e->name, NULL);
            gtk_tree_store_set(store, &iter,
                               COL_NAME, e->name,
                               COL_TYPE, "dir",
                               COL_LABEL, label,
                               COL_PATH, can_full,
                               COL_LOADED, is_expanded,
                               COL_GIT_STATUS, git_badge,
                               -1);
            g_free(label);

            if (is_expanded) {
                apply_dir_node_to_store(store, app, &iter, child_node);
            } else {
                /* Add dummy child so expander arrow is shown */
                GtkTreeIter dummy;
                gtk_tree_store_append(store, &dummy, &iter);
                gtk_tree_store_set(store, &dummy,
                                   COL_NAME, "",
                                   COL_TYPE, "-",
                                   COL_LABEL, "...",
                                   COL_PATH, "",
                                   COL_LOADED, TRUE,
                                   COL_GIT_STATUS, "",
                                   -1);
            }
        } else {
            gboolean is_buf = (app != NULL && app->sidebar_view == SIDEBAR_VIEW_NOTES);
            const gchar* type_name = is_buf ? "buffer" : "file";
            const gchar* icon_name = is_buf ? "📝" : file_icon(e->name, FALSE);
            gchar* label = g_strconcat(icon_name, " ", e->name, NULL);
            gtk_tree_store_set(store, &iter,
                               COL_NAME, e->name,
                               COL_TYPE, type_name,
                               COL_LABEL, label,
                               COL_PATH, can_full,
                               COL_LOADED, TRUE,
                               COL_GIT_STATUS, git_badge,
                               -1);
            g_free(label);
        }
        g_free(can_full);
    }
}

/* Populate one directory level under parent in the GtkTreeStore */
static void populate_level(GtkTreeStore* store, AppData* app, GtkTreeIter* parent,
                           const gchar* dirname, gboolean show_hidden, gint sort_mode) {
    GHashTable* exp = (app != NULL) ? app->expanded_paths : NULL;
    DirScanNode* node = scan_dir_node_recursive(dirname, show_hidden, sort_mode, exp, NULL);
    apply_dir_node_to_store(store, app, parent, node);
    dir_scan_node_free(node);
}

/* Populate the tree store with directory contents */
void populate_store(GtkTreeStore* store, AppData* app, const gchar* dirname,
                    gboolean show_hidden, gint sort_mode) {
    if (app != NULL) {
        if (app->git_statuses != NULL) {
            g_hash_table_destroy(app->git_statuses);
            app->git_statuses = NULL;
        }
        app->git_statuses = scan_git_statuses(dirname);
    }
    gtk_tree_store_clear(store);
    tree_nav_reset_column_widths(app);
    populate_level(store, app, NULL, dirname, show_hidden, sort_mode);
}

/* Recursively expand paths that were previously expanded */
void reexpand_paths_recursive(AppData* app, GtkTreeIter* parent) {
    GtkTreeIter child;
    if (!gtk_tree_model_iter_children(app->model, &child, parent)) {
        return;
    }
    do {
        gchar* type = NULL;
        gchar* path_val = NULL;
        gtk_tree_model_get(app->model, &child,
                           COL_TYPE, &type,
                           COL_PATH, &path_val, -1);
        if (g_strcmp0(type, "dir") == 0 && path_val != NULL) {
            if (g_hash_table_contains(app->expanded_paths, path_val)) {
                GtkTreePath* tp = gtk_tree_model_get_path(app->model, &child);
                gtk_tree_view_expand_row(app->tree_view, tp, FALSE);
                gtk_tree_path_free(tp);
                reexpand_paths_recursive(app, &child);
            }
        }
        g_free(type);
        g_free(path_val);
    } while (gtk_tree_model_iter_next(app->model, &child));
}

/* Lazy loading handler when a directory row is about to be expanded */
gboolean on_tree_test_expand_row(GtkTreeView* tree_view, GtkTreeIter* iter,
                                 GtkTreePath* path, gpointer user_data) {
    (void)tree_view;
    (void)path;
    AppData* app = (AppData*)user_data;
    GtkTreeStore* store = GTK_TREE_STORE(app->model);

    gboolean loaded = FALSE;
    gchar* full_path = NULL;
    gtk_tree_model_get(app->model, iter,
                       COL_LOADED, &loaded,
                       COL_PATH, &full_path, -1);

    if (!loaded && full_path != NULL) {
        GtkTreeIter child;
        if (gtk_tree_model_iter_children(app->model, &child, iter)) {
            gtk_tree_store_remove(store, &child);
        }
        populate_level(store, app, iter, full_path, app->show_hidden, app->sort_mode);
        gtk_tree_store_set(store, iter, COL_LOADED, TRUE, -1);
    }
    g_free(full_path);
    return FALSE; /* FALSE allows expansion */
}

/* Handle row expansion: record path and switch to open folder icon */
void on_tree_row_expanded(GtkTreeView* tree_view, GtkTreeIter* iter,
                          GtkTreePath* path, gpointer user_data) {
    (void)tree_view;
    (void)path;
    AppData* app = (AppData*)user_data;
    if (bookmarks_handle_row_toggled(app, iter, TRUE)) {
        return;
    }
    if (notes_handle_row_toggled(app, iter, TRUE)) {
        return;
    }
    gchar* name = NULL;
    gchar* full_path = NULL;
    gtk_tree_model_get(app->model, iter,
                       COL_NAME, &name,
                       COL_PATH, &full_path, -1);
    if (full_path != NULL && app->expanded_paths != NULL) {
        g_hash_table_add(app->expanded_paths, g_strdup(full_path));
        sync_directory_monitors(app);
    }
    if (name != NULL) {
        gchar* label = g_strconcat("📂 ", name, NULL);
        gtk_tree_store_set(GTK_TREE_STORE(app->model), iter, COL_LABEL, label, -1);
        g_free(label);
    }
    if (!app->switching_ws && !app->dir_scan_pending) {
        app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    }
    g_free(name);
    g_free(full_path);
}

/* Handle row collapse: remove path and switch back to closed folder icon */
void on_tree_row_collapsed(GtkTreeView* tree_view, GtkTreeIter* iter,
                           GtkTreePath* path, gpointer user_data) {
    (void)tree_view;
    (void)path;
    AppData* app = (AppData*)user_data;
    if (bookmarks_handle_row_toggled(app, iter, FALSE)) {
        return;
    }
    if (notes_handle_row_toggled(app, iter, FALSE)) {
        return;
    }
    gchar* name = NULL;
    gchar* full_path = NULL;
    gtk_tree_model_get(app->model, iter,
                       COL_NAME, &name,
                       COL_PATH, &full_path, -1);
    if (full_path != NULL && app->expanded_paths != NULL) {
        gchar* prefix = g_strdup_printf("%s/", full_path);
        GHashTableIter hiter;
        gpointer hkey, hval;
        g_hash_table_iter_init(&hiter, app->expanded_paths);
        while (g_hash_table_iter_next(&hiter, &hkey, &hval)) {
            const gchar* p = (const gchar*)hkey;
            if (g_strcmp0(p, full_path) == 0 || g_str_has_prefix(p, prefix)) {
                g_hash_table_iter_remove(&hiter);
            }
        }
        g_free(prefix);
        sync_directory_monitors(app);
    }
    if (name != NULL) {
        gchar* label = g_strconcat("📁 ", name, NULL);
        gtk_tree_store_set(GTK_TREE_STORE(app->model), iter, COL_LABEL, label, -1);
        g_free(label);
    }

    /* Reset children to dummy row so future re-expansion reloads fresh from disk */
    GtkTreeIter child;
    while (gtk_tree_model_iter_children(app->model, &child, iter)) {
        gtk_tree_store_remove(GTK_TREE_STORE(app->model), &child);
    }
    GtkTreeIter dummy;
    gtk_tree_store_append(GTK_TREE_STORE(app->model), &dummy, iter);
    gtk_tree_store_set(GTK_TREE_STORE(app->model), &dummy,
                       COL_NAME, "",
                       COL_TYPE, "-",
                       COL_LABEL, "...",
                       COL_PATH, "",
                       COL_LOADED, TRUE,
                       COL_GIT_STATUS, "",
                       -1);
    gtk_tree_store_set(GTK_TREE_STORE(app->model), iter, COL_LOADED, FALSE, -1);

    if (!app->switching_ws && !app->dir_scan_pending) {
        app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    }
    g_free(name);
    g_free(full_path);
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
    GHashTable* git_statuses;
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
    if (td->git_statuses != NULL) {
        g_hash_table_destroy(td->git_statuses);
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

    td->git_statuses = scan_git_statuses(td->dirname);

    if (g_task_return_error_if_cancelled(task)) {
        return;
    }
    g_task_return_boolean(task, TRUE);
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

    GtkTreeStore* store = GTK_TREE_STORE(app->model);

    tree_nav_block_selection_handler(app);

    if (app->git_statuses != NULL) {
        g_hash_table_destroy(app->git_statuses);
        app->git_statuses = NULL;
    }
    if (td->git_statuses != NULL) {
        app->git_statuses = td->git_statuses;
        td->git_statuses = NULL;
    }

    gint saved_page = -1;
    if (app->notebook != NULL && !app->editor_dedicated) {
        saved_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
    }

    gtk_tree_store_clear(store);
    tree_nav_reset_column_widths(app);
    apply_dir_node_to_store(store, app, NULL, td->root_node);
    reexpand_paths_recursive(app, NULL);
    bookmarks_reexpand(app);
    notes_reexpand(app);
    sync_directory_monitors(app);

    if (td->select_path != NULL) {
        if (!select_by_path(app, td->select_path)) {
            tree_nav_unblock_selection_handler(app);
            select_first_row(app);
        } else {
            tree_nav_unblock_selection_handler(app);
        }
    } else {
        tree_nav_unblock_selection_handler(app);
        if (td->select_name != NULL) {
            if (!select_by_name(app, td->select_name)) {
                select_first_row(app);
            }
        } else {
            select_first_row(app);
        }
    }

    if (saved_page > 0 && app->notebook != NULL && !app->editor_dedicated) {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), saved_page);
    }

    app_emit(app, APP_EVENT_CHROME_CHANGED);
    git_tab_update(app, app->base_dir);
}

static void start_async_dir_scan(AppData* app, const gchar* dirname,
                                 const gchar* select_path, const gchar* select_name) {
    if (app->dir_cancellable != NULL) {
        g_cancellable_cancel(app->dir_cancellable);
        g_clear_object(&app->dir_cancellable);
    }
    app->dir_cancellable = g_cancellable_new();
    app->dir_scan_pending = TRUE;

    DirScanTaskData* td = g_new0(DirScanTaskData, 1);
    td->app = app;
    td->dirname = g_canonicalize_filename(dirname, NULL);
    td->show_hidden = app->show_hidden;
    td->sort_mode = app->sort_mode;
    td->expanded_snapshot = clone_string_set(app->expanded_paths);
    td->select_path = (select_path != NULL) ? g_canonicalize_filename(select_path, NULL) : NULL;
    td->select_name = g_strdup(select_name);

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

    g_free(app->base_dir);
    app->base_dir = g_canonicalize_filename(new_dir, NULL);
    app->dir_scan_pending = TRUE;
    if (!app->switching_ws && app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
        g_free(app->workspaces[app->current_ws].selected_path);
        app->workspaces[app->current_ws].selected_path = NULL;
    }

    /* Clear prior search term */
    g_free(app->search_term);
    app->search_term = NULL;
    gtk_editable_set_text(GTK_EDITABLE(app->search_entry), "");

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
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->text_view), 0);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->text_view), 0);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->text_view), 0);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->text_view), 0);
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
    start_async_dir_scan(app, new_dir, NULL, select_name);
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
    start_async_dir_scan(app, app->base_dir, select_path, NULL);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
}

void refresh_listing(AppData* app) {
    gchar* cur_path = NULL;
    GtkTreeIter it;
    if (gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        gtk_tree_model_get(app->model, &it, COL_PATH, &cur_path, -1);
    }

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
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        return;
    }
    gchar* name = NULL;
    gchar* type = NULL;
    gchar* full = NULL;
    gtk_tree_model_get(app->model, &it,
                       COL_NAME, &name,
                       COL_TYPE, &type,
                       COL_PATH, &full, -1);

    if (g_strcmp0(type, "note") == 0 || g_strcmp0(type, "notebook") == 0 ||
        g_strcmp0(type, "trash") == 0 || g_strcmp0(type, "trash_note") == 0 ||
        g_strcmp0(type, "buffer") == 0) {
        notes_activate_selected(app);
    } else if (g_strcmp0(type, "bookmark") == 0 || g_strcmp0(type, "bm_folder") == 0) {
        bookmarks_activate_selected(app);
    } else if (g_strcmp0(type, "remote") == 0 || app->sidebar_view == SIDEBAR_VIEW_REMOTE) {
        remote_activate_selected(app);
    } else if (g_strcmp0(type, "dir") == 0) {
        GtkTreePath* path = gtk_tree_model_get_path(app->model, &it);
        if (gtk_tree_view_row_expanded(app->tree_view, path)) {
            gtk_tree_view_collapse_row(app->tree_view, path);
        } else {
            gtk_tree_view_expand_row(app->tree_view, path, FALSE);
        }
        gtk_tree_path_free(path);
    } else if (g_strcmp0(type, "file") == 0 && full != NULL) {
        if (is_image_file(name) || is_pdf_file(name) || is_video_file(name) || is_audio_file(name)) {
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
            editor_open_file(app, name, full);
        }
    }
    g_free(name);
    g_free(type);
    g_free(full);
}

/* Double click on a row: same action as Enter */
void on_row_activated(GtkTreeView* view, GtkTreePath* path,
                      GtkTreeViewColumn* column, gpointer data) {
    (void)view;
    (void)path;
    (void)column;
    activate_selected_entry((AppData*)data);
}

/* Treeview key press handler: j k gg G zz / n N BackSpace ? i o Ctrl+B Ctrl+F f m space l Right h Left */
gboolean on_tree_key_press(GtkEventControllerKey* controller, guint keyval,
                           guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    AppData* app = (AppData*)data;
    guint key = keyval;

    /* Bookmark manager keys (add, new folder, edit, delete, reorder) */
    if (bookmarks_handle_key(app, keyval, state)) {
        return TRUE;
    }

    /* Notes manager keys (new note, new notebook, edit, move, delete, restore, reorder) */
    if (notes_handle_key(app, keyval, state)) {
        return TRUE;
    }

    /* Remote manager keys (connect, terminal, add, edit, delete) */
    if (remote_handle_key(app, keyval, state)) {
        return TRUE;
    }

    if (key != GDK_KEY_z && key != GDK_KEY_t && key != GDK_KEY_b) {
        app->pending_z = FALSE;
    }

    switch (key) {
    case GDK_KEY_j:
        app->pending_g = FALSE;
        select_next_row(app);
        return TRUE;
    case GDK_KEY_k:
        app->pending_g = FALSE;
        select_prev_row(app);
        return TRUE;
    case GDK_KEY_g:
        /* gg = jump to top */
        if (app->pending_g) {
            select_first_row(app);
            app->pending_g = FALSE;
        } else {
            app->pending_g = TRUE;
        }
        return TRUE;
    case GDK_KEY_G:
        app->pending_g = FALSE;
        select_last_row(app);
        return TRUE;
    case GDK_KEY_z:
        /* zz = center selected row vertically */
        app->pending_g = FALSE;
        if ((state & GDK_CONTROL_MASK) == 0) {
            if (app->pending_z) {
                center_selected_row(app);
                app->pending_z = FALSE;
            } else {
                app->pending_z = TRUE;
            }
            return TRUE;
        }
        app->pending_z = FALSE;
        return FALSE;
    case GDK_KEY_t:
        /* zt = scroll selected row to top of the panel */
        app->pending_g = FALSE;
        if ((state & GDK_CONTROL_MASK) == 0 && app->pending_z) {
            top_selected_row(app);
            app->pending_z = FALSE;
            return TRUE;
        }
        app->pending_z = FALSE;
        return FALSE;
    case GDK_KEY_l:
    case GDK_KEY_Right:
        app->pending_g = FALSE;
        {
            GtkTreeIter it;
            if (gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
                gchar* type = NULL;
                gchar* full = NULL;
                gtk_tree_model_get(app->model, &it,
                                   COL_TYPE, &type,
                                   COL_PATH, &full, -1);
                if (g_strcmp0(type, "bookmark") == 0) {
                    bookmarks_activate_selected(app);
                } else if (g_strcmp0(type, "dir") == 0 || g_strcmp0(type, "bm_folder") == 0 ||
                           g_strcmp0(type, "notebook") == 0 || g_strcmp0(type, "trash") == 0) {
                    GtkTreePath* p = gtk_tree_model_get_path(app->model, &it);
                    if (!gtk_tree_view_row_expanded(app->tree_view, p)) {
                        gtk_tree_view_expand_row(app->tree_view, p, FALSE);
                    } else {
                        /* Already expanded: step into first child */
                        GtkTreePath* child = gtk_tree_path_copy(p);
                        gtk_tree_path_down(child);
                        GtkTreeIter ch_it;
                        if (gtk_tree_model_get_iter(app->model, &ch_it, child)) {
                            select_path(app, child);
                        }
                        gtk_tree_path_free(child);
                    }
                    gtk_tree_path_free(p);
                }
                g_free(type);
                g_free(full);
            }
        }
        return TRUE;
    case GDK_KEY_h:
    case GDK_KEY_Left:
        app->pending_g = FALSE;
        {
            GtkTreeIter it;
            if (gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
                gchar* type = NULL;
                gtk_tree_model_get(app->model, &it, COL_TYPE, &type, -1);
                GtkTreePath* p = gtk_tree_model_get_path(app->model, &it);
                if ((g_strcmp0(type, "dir") == 0 || g_strcmp0(type, "bm_folder") == 0 ||
                     g_strcmp0(type, "notebook") == 0 || g_strcmp0(type, "trash") == 0) &&
                    gtk_tree_view_row_expanded(app->tree_view, p)) {
                    gtk_tree_view_collapse_row(app->tree_view, p);
                } else if (gtk_tree_path_get_depth(p) > 1) {
                    /* Jump to parent directory row */
                    gtk_tree_path_up(p);
                    select_path(app, p);
                }
                gtk_tree_path_free(p);
                g_free(type);
            }
        }
        return TRUE;
    case GDK_KEY_space:
        app->pending_g = FALSE;
        {
            GtkTreeIter it;
            if (gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
                gchar* type = NULL;
                gtk_tree_model_get(app->model, &it, COL_TYPE, &type, -1);
                if (g_strcmp0(type, "dir") == 0 || g_strcmp0(type, "bm_folder") == 0 ||
                    g_strcmp0(type, "notebook") == 0 || g_strcmp0(type, "trash") == 0) {
                    GtkTreePath* p = gtk_tree_model_get_path(app->model, &it);
                    if (gtk_tree_view_row_expanded(app->tree_view, p)) {
                        gtk_tree_view_collapse_row(app->tree_view, p);
                    } else {
                        gtk_tree_view_expand_row(app->tree_view, p, FALSE);
                    }
                    gtk_tree_path_free(p);
                } else if (app->viewed_name != NULL && !app->render_raw) {
                    if (is_video_file(app->viewed_name)) {
                        video_preview_toggle_play(app);
                    } else if (is_audio_file(app->viewed_name)) {
                        audio_preview_toggle_play(app);
                    }
                }
                g_free(type);
            }
        }
        return TRUE;
    case GDK_KEY_p:
    case GDK_KEY_P:
        app->pending_g = FALSE;
        if (app->viewed_name != NULL && !app->render_raw) {
            if (is_video_file(app->viewed_name)) {
                video_preview_toggle_play(app);
                return TRUE;
            } else if (is_audio_file(app->viewed_name)) {
                audio_preview_toggle_play(app);
                return TRUE;
            }
        }
        return FALSE;
    case GDK_KEY_slash:
        app->pending_g = FALSE;
        show_search(app, FALSE);
        return TRUE;
    case GDK_KEY_n: /* next match */
        app->pending_g = FALSE;
        do_search(app, +1);
        return TRUE;
    case GDK_KEY_N: /* previous match */
        app->pending_g = FALSE;
        do_search(app, -1);
        return TRUE;
    case GDK_KEY_Return: /* Enter selected directory or open editor tab */
    case GDK_KEY_KP_Enter:
    case GDK_KEY_Delete:
    case GDK_KEY_KP_Delete:
    case GDK_KEY_ISO_Enter:
        app->pending_g = FALSE;
        if ((state & GDK_CONTROL_MASK) != 0) {
            /* Ctrl+Enter: enter directory as root */
            GtkTreeIter it;
            if (gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
                gchar* type = NULL;
                gchar* full = NULL;
                gtk_tree_model_get(app->model, &it,
                                   COL_TYPE, &type,
                                   COL_PATH, &full, -1);
                if (g_strcmp0(type, "dir") == 0 && full != NULL) {
                    reload_directory(app, full);
                }
                g_free(type);
                g_free(full);
            }
        } else {
            activate_selected_entry(app);
        }
        return TRUE;
    case GDK_KEY_BackSpace: /* Parent directory (or return to files view from bookmarks / buffers) */
        app->pending_g = FALSE;
        if (app->sidebar_view != SIDEBAR_VIEW_FILES || app->show_bookmarks) {
            app->sidebar_view = SIDEBAR_VIEW_FILES;
            app->show_bookmarks = FALSE;
            refresh_listing(app);
            return TRUE;
        }
        {
            gchar* parent = g_path_get_dirname(app->base_dir);
            if (g_strcmp0(parent, app->base_dir) != 0) {
                /* Remember directory left to highlight it again in parent */
                gchar* came_from = g_path_get_basename(app->base_dir);
                reload_directory_with_selection(app, parent, came_from);
                g_free(came_from);
            }
            g_free(parent);
        }
        return TRUE;
    case GDK_KEY_question: /* ?: toggle internal keybindings help panel */
        if ((state & GDK_CONTROL_MASK) == 0) {
            app->pending_g = FALSE;
            toggle_help_panel(app);
            return TRUE;
        }
        app->pending_g = FALSE;
        return FALSE;
    case GDK_KEY_I: /* Toggle hidden files */
    case GDK_KEY_i:
        app->pending_g = FALSE;
        toggle_hidden_files(app);
        return TRUE;
    case GDK_KEY_o: /* Cycle sort: name -> date -> size -> ext */
        app->pending_g = FALSE;
        cycle_sort_mode(app);
        return TRUE;
    case GDK_KEY_b: /* Ctrl+B: page up; zb: scroll selected row to bottom of panel */
        app->pending_g = FALSE;
        if (state & GDK_CONTROL_MASK) {
            app->pending_z = FALSE;
            page_up_rows(app);
            return TRUE;
        }
        if (app->pending_z) {
            bottom_selected_row(app);
            app->pending_z = FALSE;
            return TRUE;
        }
        app->pending_z = FALSE;
        return FALSE;
    case GDK_KEY_d: /* Ctrl+D: bookmark selected entry (or edit / remove its existing bookmark) */
    case GDK_KEY_D:
        app->pending_g = FALSE;
        if (state & GDK_CONTROL_MASK) {
            bookmarks_add_from_context(app);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_a: /* Ctrl+A: switch between files and bookmarks views in explorer panel */
    case GDK_KEY_A:
        app->pending_g = FALSE;
        if (state & GDK_CONTROL_MASK) {
            toggle_bookmarks_view(app);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_u: /* Ctrl+U: toggle notes view in explorer panel */
    case GDK_KEY_U:
        app->pending_g = FALSE;
        if (state & GDK_CONTROL_MASK) {
            toggle_notes_view(app);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_r: /* r in notes view / Ctrl+R / F2: rename selected item */
    case GDK_KEY_R:
        app->pending_g = FALSE;
        if ((state & GDK_CONTROL_MASK) != 0 || app->sidebar_view == SIDEBAR_VIEW_NOTES) {
            if (app->sidebar_view == SIDEBAR_VIEW_NOTES) {
                notes_edit_selected(app);
            }
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_F2:
        app->pending_g = FALSE;
        if (app->sidebar_view == SIDEBAR_VIEW_NOTES) {
            notes_edit_selected(app);
        }
        return TRUE;
    case GDK_KEY_f: /* Ctrl+F: page down; f alone: fullscreen */
        app->pending_g = FALSE;
        if (state & GDK_CONTROL_MASK) {
            page_down_rows(app);
            return TRUE;
        }
        toggle_fullscreen(app->window);
        return TRUE;
    case GDK_KEY_m:
    case GDK_KEY_M:
        app->pending_g = FALSE;
        if (app->viewed_name != NULL && is_renderable_file(app->viewed_name)) {
            app->render_raw = !app->render_raw;
            app_emit(app, APP_EVENT_LAYOUT_CHANGED);
            if (app->viewed_path != NULL) {
                app_emit(app, APP_EVENT_PREVIEW_STALE);
            }
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_Escape:
        app->pending_g = FALSE;
        if (app->help_window != NULL && gtk_widget_get_visible(app->help_window)) {
            toggle_help_panel(app);
            return TRUE;
        }
        return FALSE;
    default:
        app->pending_g = FALSE; /* Any other key cancels gg and z sequences */
        app->pending_z = FALSE;
        break;
    }
    return FALSE;
}
