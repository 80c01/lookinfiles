#include "file_list.h"
#include "editor.h"
#include "viewer.h"
#include "search.h"
#include "ui.h"

/* Comparator context (single-threaded: GTK main loop only) */
static gint cur_sort_mode = SORT_NAME;

/* File extension ("" if none), ignoring dot-files initial dot */
const gchar* file_extension(const gchar* name) {
    if (name == NULL) {
        return "";
    }
    const gchar* dot = strrchr(name, '.');
    return (dot != NULL && dot != name) ? dot + 1 : "";
}

/* Return TRUE if file name indicates Markdown content */
gboolean is_markdown_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "md") == 0 ||
            g_ascii_strcasecmp(ext, "markdown") == 0 ||
            g_ascii_strcasecmp(ext, "mdown") == 0 ||
            g_ascii_strcasecmp(ext, "mkd") == 0 ||
            g_ascii_strcasecmp(ext, "mkdn") == 0);
}

/* Return TRUE if file name indicates HTML content */
gboolean is_html_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "html") == 0 ||
            g_ascii_strcasecmp(ext, "htm") == 0 ||
            g_ascii_strcasecmp(ext, "xhtml") == 0 ||
            g_ascii_strcasecmp(ext, "shtml") == 0);
}

/* Return TRUE if file name indicates an image format */
gboolean is_image_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "png") == 0 ||
            g_ascii_strcasecmp(ext, "jpg") == 0 ||
            g_ascii_strcasecmp(ext, "jpeg") == 0 ||
            g_ascii_strcasecmp(ext, "gif") == 0 ||
            g_ascii_strcasecmp(ext, "svg") == 0 ||
            g_ascii_strcasecmp(ext, "svgz") == 0 ||
            g_ascii_strcasecmp(ext, "bmp") == 0 ||
            g_ascii_strcasecmp(ext, "ico") == 0 ||
            g_ascii_strcasecmp(ext, "webp") == 0 ||
            g_ascii_strcasecmp(ext, "tiff") == 0 ||
            g_ascii_strcasecmp(ext, "tif") == 0 ||
            g_ascii_strcasecmp(ext, "pnm") == 0 ||
            g_ascii_strcasecmp(ext, "pbm") == 0 ||
            g_ascii_strcasecmp(ext, "pgm") == 0 ||
            g_ascii_strcasecmp(ext, "ppm") == 0 ||
            g_ascii_strcasecmp(ext, "xpm") == 0 ||
            g_ascii_strcasecmp(ext, "xbm") == 0 ||
            g_ascii_strcasecmp(ext, "tga") == 0 ||
            g_ascii_strcasecmp(ext, "icns") == 0 ||
            g_ascii_strcasecmp(ext, "cur") == 0);
}

/* Return TRUE if file content can be rendered richly in preview pane */
gboolean is_renderable_file(const gchar* name) {
    return is_markdown_file(name) || is_html_file(name) || is_image_file(name);
}

/* Sort: directories first, then according to active sort mode */
static gint compare_entries(gconstpointer a, gconstpointer b) {
    const FileEntry* ea = (const FileEntry*)a;
    const FileEntry* eb = (const FileEntry*)b;

    if (ea->is_dir != eb->is_dir) {
        return ea->is_dir ? -1 : 1;
    }

    gint res = 0;
    switch (cur_sort_mode) {
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

/* VS Code-like emoji for a tree entry, based on kind / extension */
const gchar* file_icon(const gchar* name, gboolean is_dir) {
    if (is_dir) {
        return "📁";
    }
    const gchar* dot = strrchr(name, '.');
    if (dot == NULL) {
        /* Build/remote entry names without any extension */
        if (g_strcmp0(name, "Makefile") == 0 ||
            g_ascii_strcasecmp(name, "makefile") == 0) {
            return "🔧";
        }
        return "📄";
    }
#define EXT_IS(x) (g_ascii_strcasecmp(dot, (x)) == 0)
    if (EXT_IS(".c") || EXT_IS(".h")) {
        return "🛠️"; /* systems / C sources */
    }
    if (EXT_IS(".cc") || EXT_IS(".cpp") || EXT_IS(".cxx") || EXT_IS(".hpp")) {
        return "🔷"; /* C++ */
    }
    if (EXT_IS(".py") || EXT_IS(".pyw")) {
        return "🐍";
    }
    if (EXT_IS(".java") || EXT_IS(".jar")) {
        return "☕";
    }
    if (EXT_IS(".js") || EXT_IS(".mjs") || EXT_IS(".ts")) {
        return "📜";
    }
    if (EXT_IS(".html") || EXT_IS(".htm") || EXT_IS(".xml")) {
        return "🌐";
    }
    if (EXT_IS(".css") || EXT_IS(".scss") || EXT_IS(".less")) {
        return "🎨";
    }
    if (EXT_IS(".json") || EXT_IS(".yaml") || EXT_IS(".yml") ||
        EXT_IS(".toml") || EXT_IS(".ini") || EXT_IS(".cfg")) {
        return "🧾";
    }
    if (EXT_IS(".sh") || EXT_IS(".bash") || EXT_IS(".zsh")) {
        return "🐚";
    }
    if (EXT_IS(".png") || EXT_IS(".jpg") || EXT_IS(".jpeg") || EXT_IS(".gif") ||
        EXT_IS(".svg") || EXT_IS(".bmp") || EXT_IS(".ico") || EXT_IS(".webp")) {
        return "🖼️";
    }
    if (EXT_IS(".mp3") || EXT_IS(".wav") || EXT_IS(".ogg") || EXT_IS(".flac")) {
        return "🎵";
    }
    if (EXT_IS(".mp4") || EXT_IS(".avi") || EXT_IS(".mkv") || EXT_IS(".mov")) {
        return "🎬";
    }
    if (EXT_IS(".tar") || EXT_IS(".gz") || EXT_IS(".tgz") || EXT_IS(".zip") ||
        EXT_IS(".rar") || EXT_IS(".7z") || EXT_IS(".xz") || EXT_IS(".bz2")) {
        return "📦";
    }
    if (EXT_IS(".db") || EXT_IS(".sqlite") || EXT_IS(".sqlite3")) {
        return "🗃️";
    }
    if (EXT_IS(".o") || EXT_IS(".so") || EXT_IS(".a") || EXT_IS(".bin") ||
        EXT_IS(".exe")) {
        return "⚙️"; /* compiled artifacts */
    }
    if (EXT_IS(".md") || EXT_IS(".txt") || EXT_IS(".rst") || EXT_IS(".log")) {
        return "📝";
    }
    return "📄";
#undef EXT_IS
}

/* Total top-level entries in the model */
gint list_count(AppData* app) {
    return gtk_tree_model_iter_n_children(app->model, NULL);
}

/* Return path of the first visible row */
GtkTreePath* get_first_visible_path(GtkTreeView* view) {
    GtkTreeModel* model = gtk_tree_view_get_model(view);
    GtkTreeIter it;
    if (gtk_tree_model_get_iter_first(model, &it)) {
        return gtk_tree_model_get_path(model, &it);
    }
    return NULL;
}

/* Return next visible row path (following expansions) or NULL if at end */
GtkTreePath* get_next_visible_path(GtkTreeView* view, GtkTreePath* path) {
    if (path == NULL) {
        return NULL;
    }
    GtkTreeModel* model = gtk_tree_view_get_model(view);
    GtkTreePath* curr = gtk_tree_path_copy(path);

    /* 1. If row is expanded, next visible row is its first child */
    if (gtk_tree_view_row_expanded(view, curr)) {
        GtkTreePath* child = gtk_tree_path_copy(curr);
        gtk_tree_path_down(child);
        GtkTreeIter it;
        if (gtk_tree_model_get_iter(model, &it, child)) {
            gtk_tree_path_free(curr);
            return child;
        }
        gtk_tree_path_free(child);
    }

    /* 2. Try next sibling at current level, or walk up to ancestors */
    while (gtk_tree_path_get_depth(curr) > 0) {
        GtkTreePath* next = gtk_tree_path_copy(curr);
        gtk_tree_path_next(next);
        GtkTreeIter it;
        if (gtk_tree_model_get_iter(model, &it, next)) {
            gtk_tree_path_free(curr);
            return next;
        }
        gtk_tree_path_free(next);
        if (!gtk_tree_path_up(curr)) {
            break;
        }
    }

    gtk_tree_path_free(curr);
    return NULL;
}

/* Return previous visible row path (following expansions) or NULL if at top */
GtkTreePath* get_prev_visible_path(GtkTreeView* view, GtkTreePath* path) {
    if (path == NULL) {
        return NULL;
    }
    GtkTreeModel* model = gtk_tree_view_get_model(view);
    GtkTreePath* curr = gtk_tree_path_copy(path);

    /* 1. Try previous sibling */
    if (gtk_tree_path_prev(curr)) {
        /* If that sibling is expanded, step to its last visible descendant */
        while (gtk_tree_view_row_expanded(view, curr)) {
            GtkTreeIter parent_it;
            if (!gtk_tree_model_get_iter(model, &parent_it, curr)) {
                break;
            }
            gint n = gtk_tree_model_iter_n_children(model, &parent_it);
            if (n <= 0) {
                break;
            }
            gtk_tree_path_down(curr);
            for (gint i = 1; i < n; i++) {
                gtk_tree_path_next(curr);
            }
        }
        return curr;
    }

    /* 2. No previous sibling: if we have a parent, parent is previous visible */
    if (gtk_tree_path_get_depth(curr) > 1) {
        gtk_tree_path_up(curr);
        return curr;
    }

    gtk_tree_path_free(curr);
    return NULL;
}

/* Return path of the last visible row */
GtkTreePath* get_last_visible_path(GtkTreeView* view) {
    GtkTreePath* cur = get_first_visible_path(view);
    if (cur == NULL) {
        return NULL;
    }
    while (TRUE) {
        GtkTreePath* next = get_next_visible_path(view, cur);
        if (next == NULL) {
            return cur;
        }
        gtk_tree_path_free(cur);
        cur = next;
    }
}

/* Select row at path and scroll it into view */
void select_path(AppData* app, GtkTreePath* path) {
    if (path == NULL) {
        return;
    }
    gtk_tree_selection_select_path(app->selection, path);
    gtk_tree_view_set_cursor(app->tree_view, path, NULL, FALSE);
    gtk_tree_view_scroll_to_cell(app->tree_view, path, NULL, FALSE, 0.0, 0.0);
}

/* Select the first visible row */
void select_first_row(AppData* app) {
    GtkTreePath* p = get_first_visible_path(app->tree_view);
    if (p != NULL) {
        select_path(app, p);
        gtk_tree_path_free(p);
    }
}

/* Select the last visible row */
void select_last_row(AppData* app) {
    GtkTreePath* p = get_last_visible_path(app->tree_view);
    if (p != NULL) {
        select_path(app, p);
        gtk_tree_path_free(p);
    }
}

/* Select the next visible row */
void select_next_row(AppData* app) {
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        select_first_row(app);
        return;
    }
    GtkTreePath* path = gtk_tree_model_get_path(app->model, &it);
    GtkTreePath* next = get_next_visible_path(app->tree_view, path);
    if (next != NULL) {
        select_path(app, next);
        gtk_tree_path_free(next);
    }
    gtk_tree_path_free(path);
}

/* Select the previous visible row */
void select_prev_row(AppData* app) {
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        select_first_row(app);
        return;
    }
    GtkTreePath* path = gtk_tree_model_get_path(app->model, &it);
    GtkTreePath* prev = get_prev_visible_path(app->tree_view, path);
    if (prev != NULL) {
        select_path(app, prev);
        gtk_tree_path_free(prev);
    }
    gtk_tree_path_free(path);
}

/* Number of visible rows in tree view (for Ctrl-F / Ctrl-B) */
gint count_visible_rows(AppData* app) {
    GtkTreePath* start = NULL;
    GtkTreePath* end = NULL;
    if (!gtk_tree_view_get_visible_range(app->tree_view, &start, &end)) {
        return 10;
    }
    gint count = 1;
    GtkTreePath* cur = gtk_tree_path_copy(start);
    while (gtk_tree_path_compare(cur, end) != 0) {
        GtkTreePath* next = get_next_visible_path(app->tree_view, cur);
        if (next == NULL) {
            break;
        }
        gtk_tree_path_free(cur);
        cur = next;
        count++;
    }
    gtk_tree_path_free(cur);
    gtk_tree_path_free(start);
    gtk_tree_path_free(end);
    return (count > 0) ? count : 10;
}

/* Jump forward by one page of visible rows */
void page_down_rows(AppData* app) {
    gint count = count_visible_rows(app);
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        select_first_row(app);
        return;
    }
    GtkTreePath* cur = gtk_tree_model_get_path(app->model, &it);
    for (gint i = 0; i < count; i++) {
        GtkTreePath* next = get_next_visible_path(app->tree_view, cur);
        if (next == NULL) {
            break;
        }
        gtk_tree_path_free(cur);
        cur = next;
    }
    select_path(app, cur);
    gtk_tree_path_free(cur);
}

/* Jump backward by one page of visible rows */
void page_up_rows(AppData* app) {
    gint count = count_visible_rows(app);
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        select_first_row(app);
        return;
    }
    GtkTreePath* cur = gtk_tree_model_get_path(app->model, &it);
    for (gint i = 0; i < count; i++) {
        GtkTreePath* prev = get_prev_visible_path(app->tree_view, cur);
        if (prev == NULL) {
            break;
        }
        gtk_tree_path_free(cur);
        cur = prev;
    }
    select_path(app, cur);
    gtk_tree_path_free(cur);
}

/* Select top-level row matching name (e.g. directory just left) */
gboolean select_by_name(AppData* app, const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    GtkTreeIter it;
    if (!gtk_tree_model_get_iter_first(app->model, &it)) {
        return FALSE;
    }
    do {
        gchar* row_name = NULL;
        gtk_tree_model_get(app->model, &it, COL_NAME, &row_name, -1);
        gboolean match = (g_strcmp0(row_name, name) == 0);
        g_free(row_name);
        if (match) {
            GtkTreePath* p = gtk_tree_model_get_path(app->model, &it);
            select_path(app, p);
            gtk_tree_path_free(p);
            return TRUE;
        }
    } while (gtk_tree_model_iter_next(app->model, &it));
    return FALSE;
}

/* Recursive helper to search for a row by absolute path */
static gboolean find_iter_by_path_recursive(GtkTreeModel* model, GtkTreeIter* parent,
                                            const gchar* full_path, GtkTreeIter* result) {
    GtkTreeIter child;
    if (!gtk_tree_model_iter_children(model, &child, parent)) {
        return FALSE;
    }
    do {
        gchar* path_val = NULL;
        gtk_tree_model_get(model, &child, COL_PATH, &path_val, -1);
        if (g_strcmp0(path_val, full_path) == 0) {
            g_free(path_val);
            *result = child;
            return TRUE;
        }
        g_free(path_val);
        if (find_iter_by_path_recursive(model, &child, full_path, result)) {
            return TRUE;
        }
    } while (gtk_tree_model_iter_next(model, &child));
    return FALSE;
}

/* Select row matching full path */
gboolean select_by_path(AppData* app, const gchar* full_path) {
    if (full_path == NULL) {
        return FALSE;
    }
    GtkTreeIter it;
    if (find_iter_by_path_recursive(app->model, NULL, full_path, &it)) {
        GtkTreePath* p = gtk_tree_model_get_path(app->model, &it);
        select_path(app, p);
        gtk_tree_path_free(p);
        return TRUE;
    }
    return FALSE;
}

/* Populate one directory level under parent in the GtkTreeStore */
static void populate_level(GtkTreeStore* store, AppData* app, GtkTreeIter* parent,
                           const gchar* dirname, gboolean show_hidden, gint sort_mode) {
    GDir* dir = g_dir_open(dirname, 0, NULL);
    if (dir == NULL) {
        GtkTreeIter iter;
        gtk_tree_store_append(store, &iter, parent);
        gtk_tree_store_set(store, &iter,
                           COL_NAME, "(cannot read directory)",
                           COL_TYPE, "-",
                           COL_LABEL, (parent == NULL) ? "📄 (cannot read directory)"
                                                       : "  (cannot read directory)",
                           COL_PATH, dirname,
                           COL_LOADED, TRUE,
                           -1);
        return;
    }

    /* Collect entries with metadata for sorting */
    GArray* entries = g_array_new(FALSE, FALSE, sizeof(FileEntry));
    const gchar* name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (!show_hidden && name[0] == '.') {
            continue;
        }
        gchar* full = g_build_filename(dirname, name, NULL);
        struct stat st;
        FileEntry e;
        e.name = g_strdup(name);
        e.is_dir = g_file_test(full, G_FILE_TEST_IS_DIR);
        e.mtime = (g_stat(full, &st) == 0) ? (gint64)st.st_mtime : 0;
        e.size = (e.is_dir) ? 0 : (gint64)st.st_size;
        g_free(full);
        g_array_append_vals(entries, &e, 1);
    }
    g_dir_close(dir);

    cur_sort_mode = sort_mode;
    g_array_sort(entries, compare_entries);

    if (entries->len == 0 && parent != NULL) {
        GtkTreeIter iter;
        gtk_tree_store_append(store, &iter, parent);
        gtk_tree_store_set(store, &iter,
                           COL_NAME, "(empty)",
                           COL_TYPE, "-",
                           COL_LABEL, "  (empty)",
                           COL_PATH, "",
                           COL_LOADED, TRUE,
                           -1);
    }

    for (guint i = 0; i < entries->len; i++) {
        const FileEntry* e = &g_array_index(entries, FileEntry, i);
        gchar* full = g_build_filename(dirname, e->name, NULL);
        GtkTreeIter iter;
        gtk_tree_store_append(store, &iter, parent);

        if (e->is_dir) {
            gboolean is_expanded = (app != NULL && app->expanded_paths != NULL &&
                                    g_hash_table_contains(app->expanded_paths, full));
            const gchar* icon = is_expanded ? "📂" : "📁";
            gchar* label = g_strconcat(icon, " ", e->name, NULL);
            gtk_tree_store_set(store, &iter,
                               COL_NAME, e->name,
                               COL_TYPE, "dir",
                               COL_LABEL, label,
                               COL_PATH, full,
                               COL_LOADED, is_expanded,
                               -1);
            g_free(label);

            if (is_expanded) {
                populate_level(store, app, &iter, full, show_hidden, sort_mode);
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
                                   -1);
            }
        } else {
            gchar* label = g_strconcat(file_icon(e->name, FALSE), " ", e->name, NULL);
            gtk_tree_store_set(store, &iter,
                               COL_NAME, e->name,
                               COL_TYPE, "file",
                               COL_LABEL, label,
                               COL_PATH, full,
                               COL_LOADED, TRUE,
                               -1);
            g_free(label);
        }
        g_free(full);
    }

    for (guint i = 0; i < entries->len; i++) {
        g_free(g_array_index(entries, FileEntry, i).name);
    }
    g_array_unref(entries);
}

/* Populate the tree store with directory contents */
void populate_store(GtkTreeStore* store, AppData* app, const gchar* dirname,
                    gboolean show_hidden, gint sort_mode) {
    gtk_tree_store_clear(store);
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
    gchar* name = NULL;
    gchar* full_path = NULL;
    gtk_tree_model_get(app->model, iter,
                       COL_NAME, &name,
                       COL_PATH, &full_path, -1);
    if (full_path != NULL && app->expanded_paths != NULL) {
        g_hash_table_add(app->expanded_paths, g_strdup(full_path));
    }
    if (name != NULL) {
        gchar* label = g_strconcat("📂 ", name, NULL);
        gtk_tree_store_set(GTK_TREE_STORE(app->model), iter, COL_LABEL, label, -1);
        g_free(label);
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
    gchar* name = NULL;
    gchar* full_path = NULL;
    gtk_tree_model_get(app->model, iter,
                       COL_NAME, &name,
                       COL_PATH, &full_path, -1);
    if (full_path != NULL && app->expanded_paths != NULL) {
        g_hash_table_remove(app->expanded_paths, full_path);
    }
    if (name != NULL) {
        gchar* label = g_strconcat("📁 ", name, NULL);
        gtk_tree_store_set(GTK_TREE_STORE(app->model), iter, COL_LABEL, label, -1);
        g_free(label);
    }
    g_free(name);
    g_free(full_path);
}

/* Top bar label: current directory + sort mode + hidden files state */
void update_path_label(AppData* app) {
    static const gchar* sort_names[SORT_COUNT] = {"name", "date", "size", "ext"};
    gchar* text = g_strdup_printf("Directory: %s   [sort: %s] [hidden: %s]",
                                   app->base_dir, sort_names[app->sort_mode],
                                   app->show_hidden ? "on" : "off");
    gtk_label_set_text(GTK_LABEL(app->path_label), text);
    g_free(text);
}

/* Navigate to a new directory (Enter / Backspace) */
void reload_directory(AppData* app, const gchar* new_dir) {
    if (!g_file_test(new_dir, G_FILE_TEST_IS_DIR)) {
        return;
    }

    g_free(app->base_dir);
    app->base_dir = g_strdup(new_dir);
    update_path_label(app);

    /* Clear prior search term */
    g_free(app->search_term);
    app->search_term = NULL;
    gtk_entry_set_text(GTK_ENTRY(app->search_entry), "");

    /* Reset expanded paths when changing root directory */
    if (app->expanded_paths != NULL) {
        g_hash_table_remove_all(app->expanded_paths);
    }

    populate_store(GTK_TREE_STORE(app->model), app, new_dir, app->show_hidden, app->sort_mode);

    /* Clear right pane */
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_NONE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->text_view), 0);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->text_view), 0);
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

    /* Select first entry */
    select_first_row(app);
}

/* Refresh the list while preserving selected entry and open expansions */
void refresh_listing(AppData* app) {
    gchar* cur_path = NULL;
    GtkTreeIter it;
    if (gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        gtk_tree_model_get(app->model, &it, COL_PATH, &cur_path, -1);
    }

    populate_store(GTK_TREE_STORE(app->model), app, app->base_dir,
                   app->show_hidden, app->sort_mode);

    reexpand_paths_recursive(app, NULL);
    update_path_label(app);

    if (cur_path != NULL) {
        if (!select_by_path(app, cur_path)) {
            select_first_row(app);
        }
        g_free(cur_path);
    } else {
        select_first_row(app);
    }
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

    if (g_strcmp0(type, "dir") == 0) {
        GtkTreePath* path = gtk_tree_model_get_path(app->model, &it);
        if (gtk_tree_view_row_expanded(app->tree_view, path)) {
            gtk_tree_view_collapse_row(app->tree_view, path);
        } else {
            gtk_tree_view_expand_row(app->tree_view, path, FALSE);
        }
        gtk_tree_path_free(path);
    } else if (g_strcmp0(type, "file") == 0 && full != NULL) {
        editor_open_file(app, name, full);
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

/* Treeview key press handler: j k gg G / n N BackSpace h i o Ctrl+B Ctrl+F f m space l Right Left */
gboolean on_tree_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data) {
    (void)widget;
    AppData* app = (AppData*)data;
    guint key = event->keyval;

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
    case GDK_KEY_l:
    case GDK_KEY_Right:
        app->pending_g = FALSE;
        {
            GtkTreeIter it;
            if (gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
                gchar* type = NULL;
                gtk_tree_model_get(app->model, &it, COL_TYPE, &type, -1);
                if (g_strcmp0(type, "dir") == 0) {
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
            }
        }
        return TRUE;
    case GDK_KEY_Left:
        app->pending_g = FALSE;
        {
            GtkTreeIter it;
            if (gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
                gchar* type = NULL;
                gtk_tree_model_get(app->model, &it, COL_TYPE, &type, -1);
                GtkTreePath* p = gtk_tree_model_get_path(app->model, &it);
                if (g_strcmp0(type, "dir") == 0 &&
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
                if (g_strcmp0(type, "dir") == 0) {
                    GtkTreePath* p = gtk_tree_model_get_path(app->model, &it);
                    if (gtk_tree_view_row_expanded(app->tree_view, p)) {
                        gtk_tree_view_collapse_row(app->tree_view, p);
                    } else {
                        gtk_tree_view_expand_row(app->tree_view, p, FALSE);
                    }
                    gtk_tree_path_free(p);
                }
                g_free(type);
            }
        }
        return TRUE;
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
        if ((event->state & GDK_CONTROL_MASK) != 0) {
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
    case GDK_KEY_BackSpace: /* Parent directory */
        app->pending_g = FALSE;
        {
            gchar* parent = g_path_get_dirname(app->base_dir);
            if (g_strcmp0(parent, app->base_dir) != 0) {
                /* Remember directory left to highlight it again in parent */
                gchar* came_from = g_path_get_basename(app->base_dir);
                reload_directory(app, parent);
                if (!select_by_name(app, came_from)) {
                    select_first_row(app);
                }
                g_free(came_from);
            }
            g_free(parent);
        }
        return TRUE;
    case GDK_KEY_h: /* h: toggle floating keybindings help */
        if ((event->state & GDK_CONTROL_MASK) == 0) {
            app->pending_g = FALSE;
            if (gtk_widget_get_visible(app->help_window)) {
                gtk_widget_hide(app->help_window);
            } else {
                gtk_widget_show_all(app->help_window);
            }
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
    case GDK_KEY_b: /* Ctrl+B: page up */
        app->pending_g = FALSE;
        if (event->state & GDK_CONTROL_MASK) {
            page_up_rows(app);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_f: /* Ctrl+F: page down; f alone: fullscreen */
        app->pending_g = FALSE;
        if (event->state & GDK_CONTROL_MASK) {
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
            if (app->viewed_path != NULL) {
                load_file_content(app, app->viewed_name, app->viewed_path);
            }
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_Escape:
        app->pending_g = FALSE;
        return FALSE;
    default:
        app->pending_g = FALSE; /* Any other key cancels gg */
        break;
    }
    return FALSE;
}
