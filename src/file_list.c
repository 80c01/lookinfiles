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

/* Return TRUE if file content can be rendered richly in preview pane */
gboolean is_renderable_file(const gchar* name) {
    return is_markdown_file(name) || is_html_file(name);
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

/* Total entries in the list model */
gint list_count(AppData* app) {
    return gtk_tree_model_iter_n_children(app->model, NULL);
}

/* Index of currently selected entry, or -1 if none */
gint current_index(AppData* app) {
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        return -1;
    }
    GtkTreePath* p = gtk_tree_model_get_path(app->model, &it);
    gint i = -1;
    gint* ind = gtk_tree_path_get_indices(p);
    if (ind != NULL && gtk_tree_path_get_depth(p) > 0) {
        i = ind[0];
    }
    gtk_tree_path_free(p);
    return i;
}

/* Select row at idx (clamped to valid bounds) */
void select_index(AppData* app, gint idx) {
    gint n = list_count(app);
    if (n <= 0) {
        return;
    }
    if (idx < 0) {
        idx = 0;
    }
    if (idx > n - 1) {
        idx = n - 1;
    }
    GtkTreeIter it;
    if (!gtk_tree_model_iter_nth_child(app->model, &it, NULL, idx)) {
        return;
    }
    gtk_tree_selection_select_iter(app->selection, &it);
    GtkTreePath* p = gtk_tree_model_get_path(app->model, &it);
    gtk_tree_view_set_cursor(app->tree_view, p, NULL, FALSE);
    gtk_tree_path_free(p);
}

/* Select the row whose raw name matches (e.g. the directory we just left);
 * returns FALSE when no such entry is listed */
gboolean select_by_name(AppData* app, const gchar* name) {
    gint n = list_count(app);
    for (gint i = 0; i < n; i++) {
        GtkTreeIter it;
        if (!gtk_tree_model_iter_nth_child(app->model, &it, NULL, i)) {
            break;
        }
        gchar* row_name = NULL;
        gtk_tree_model_get(app->model, &it, 0, &row_name, -1);
        gboolean same = (g_strcmp0(row_name, name) == 0);
        g_free(row_name);
        if (same) {
            select_index(app, i);
            return TRUE;
        }
    }
    return FALSE;
}

/* Number of visible rows in tree view (for Ctrl-F / Ctrl-B) */
gint count_visible_rows(AppData* app) {
    GtkTreePath* start = NULL;
    GtkTreePath* end = NULL;
    if (!gtk_tree_view_get_visible_range(app->tree_view, &start, &end)) {
        return 10; /* fallback if model is empty */
    }
    gint s = 0;
    gint e = 0;
    gint* ind = gtk_tree_path_get_indices(start);
    if (ind != NULL) {
        s = ind[0];
    }
    ind = gtk_tree_path_get_indices(end);
    if (ind != NULL) {
        e = ind[0];
    }
    gtk_tree_path_free(start);
    gtk_tree_path_free(end);
    gint n = e - s + 1;
    return (n > 0) ? n : 10;
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

/* Populate the store with directory contents (directories first, sorted) */
void populate_store(GtkListStore* store, const gchar* dirname, gboolean show_hidden, gint sort_mode) {
    GtkTreeIter iter;
    gtk_list_store_clear(store);

    GDir* dir = g_dir_open(dirname, 0, NULL);
    if (dir == NULL) {
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, "(cannot read directory)", -1);
        gtk_list_store_set(store, &iter, 1, "-", -1);
        gtk_list_store_set(store, &iter, 2, "📄 (cannot read directory)", -1);
        return;
    }

    /* Collect entries with metadata for sorting */
    GArray* entries = g_array_new(FALSE, FALSE, sizeof(FileEntry));
    const gchar* name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (!show_hidden && name[0] == '.') {
            continue; /* hidden file skipped */
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

    /* Sort according to active mode (directories always first) */
    cur_sort_mode = sort_mode;
    g_array_sort(entries, compare_entries);

    for (guint i = 0; i < entries->len; i++) {
        const FileEntry* e = &g_array_index(entries, FileEntry, i);
        gchar* label = g_strconcat(file_icon(e->name, e->is_dir), " ", e->name, NULL);
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, e->name, -1);
        gtk_list_store_set(store, &iter, 1, e->is_dir ? "dir" : "file", -1);
        gtk_list_store_set(store, &iter, 2, label, -1);
        g_free(label);
    }

    for (guint i = 0; i < entries->len; i++) {
        g_free(g_array_index(entries, FileEntry, i).name);
    }
    g_array_unref(entries);
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

    populate_store(GTK_LIST_STORE(app->model), new_dir, app->show_hidden, app->sort_mode);

    /* Clear right pane */
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_NONE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->text_view), 0);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->text_view), 0);
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

    /* Select first entry if present */
    if (list_count(app) > 0) {
        select_index(app, 0);
    }
}

/* Refresh the list while preserving selected entry */
void refresh_listing(AppData* app) {
    gchar* cur_name = NULL;
    GtkTreeIter it;
    if (gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        gtk_tree_model_get(app->model, &it, 0, &cur_name, -1);
    }

    populate_store(GTK_LIST_STORE(app->model), app->base_dir,
                   app->show_hidden, app->sort_mode);

    update_path_label(app);

    if (cur_name != NULL) {
        gint n = list_count(app);
        for (gint i = 0; i < n; i++) {
            GtkTreeIter it2;
            if (!gtk_tree_model_iter_nth_child(app->model, &it2, NULL, i)) {
                break;
            }
            gchar* name = NULL;
            gtk_tree_model_get(app->model, &it2, 0, &name, -1);
            gboolean same = (g_strcmp0(name, cur_name) == 0);
            g_free(name);
            if (same) {
                select_index(app, i);
                break;
            }
        }
        g_free(cur_name);
    } else if (list_count(app) > 0) {
        select_index(app, 0);
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

/* Enter / double-click on a list entry: enter directory or open editor tab */
void activate_selected_entry(AppData* app) {
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        return;
    }
    gchar* name = NULL;
    gchar* type = NULL;
    gtk_tree_model_get(app->model, &it, 0, &name, 1, &type, -1);
    if (g_strcmp0(type, "dir") == 0) {
        gchar* subdir = g_build_filename(app->base_dir, name, NULL);
        reload_directory(app, subdir);
        g_free(subdir);
    } else {
        gchar* full = g_build_filename(app->base_dir, name, NULL);
        editor_open_file(app, name, full);
        g_free(full);
    }
    g_free(name);
    g_free(type);
}

/* Double click on a row: same action as Enter */
void on_row_activated(GtkTreeView* view, GtkTreePath* path,
                      GtkTreeViewColumn* column, gpointer data) {
    (void)view;
    (void)path;
    (void)column;
    activate_selected_entry((AppData*)data);
}

/* Treeview key press handler: j k gg G / n N BackSpace h i o Ctrl+B Ctrl+F f m */
gboolean on_tree_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data) {
    (void)widget;
    AppData* app = (AppData*)data;
    guint key = event->keyval;

    switch (key) {
    case GDK_KEY_j:
        app->pending_g = FALSE;
        select_index(app, current_index(app) + 1);
        return TRUE;
    case GDK_KEY_k:
        app->pending_g = FALSE;
        select_index(app, current_index(app) - 1);
        return TRUE;
    case GDK_KEY_g:
        /* gg = jump to top */
        if (app->pending_g) {
            select_index(app, 0);
            app->pending_g = FALSE;
        } else {
            app->pending_g = TRUE;
        }
        return TRUE;
    case GDK_KEY_G:
        app->pending_g = FALSE;
        select_index(app, list_count(app) - 1);
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
        activate_selected_entry(app);
        return TRUE;
    case GDK_KEY_BackSpace: /* Parent directory */
        app->pending_g = FALSE;
        {
            gchar* parent = g_path_get_dirname(app->base_dir);
            if (g_strcmp0(parent, app->base_dir) != 0) {
                /* Remember directory left to highlight it again in parent */
                gchar* came_from = g_path_get_basename(app->base_dir);
                reload_directory(app, parent);
                if (!select_by_name(app, came_from) && list_count(app) > 0) {
                    select_index(app, 0);
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
            select_index(app, current_index(app) - count_visible_rows(app));
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_f: /* Ctrl+F: page down; f alone: fullscreen */
        app->pending_g = FALSE;
        if (event->state & GDK_CONTROL_MASK) {
            select_index(app, current_index(app) + count_visible_rows(app));
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
