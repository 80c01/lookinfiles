#include <gtk/gtk.h>
#include <string.h>
#include <sys/stat.h>
#include <glib/gstdio.h>

#ifdef HAVE_VTE
#include <vte/vte.h>
#endif

#ifdef HAVE_GSV
/* The library headers themselves use deprecated GTimeVal: silence them */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtksourceview/gtksource.h>
#pragma GCC diagnostic pop
#include <gio/gio.h>
#endif

/* Modes de tri cyclés par la touche o */
enum { SORT_NAME, SORT_DATE, SORT_SIZE, SORT_EXT, SORT_COUNT };

/* Apercu des gros fichiers */
#define PREVIEW_THRESHOLD (512 * 1024)  /* au-dela de 512 Ko : apercu seul */
#define PREVIEW_BYTES     (64 * 1024)   /* premiers octets affiches */

/* Entree collectee pour le tri */
typedef struct {
    gchar* name;
    gboolean is_dir;
    gint64 mtime;
    gint64 size;
} FileEntry;

/* Contexte du comparateur (mono-thread : boucle GTK uniquement) */
static gint cur_sort_mode = SORT_NAME;

/* Extension d'un nom ("" si aucune), en ignorant le point initial des caches */
static const gchar* file_extension(const gchar* name) {
    const gchar* dot = strrchr(name, '.');
    return (dot != NULL && dot != name) ? dot + 1 : "";
}

/* Tri : dossiers d'abord, puis selon le mode courant */
static gint compare_entries(gconstpointer a, gconstpointer b) {
    const FileEntry* ea = (const FileEntry*)a;
    const FileEntry* eb = (const FileEntry*)b;

    if (ea->is_dir != eb->is_dir) {
        return ea->is_dir ? -1 : 1;
    }

    gint res = 0;
    switch (cur_sort_mode) {
    case SORT_DATE:
        res = (ea->mtime > eb->mtime) ? -1 : (ea->mtime < eb->mtime) ? 1 : 0; /* recent d'abord */
        break;
    case SORT_SIZE:
        res = (ea->size > eb->size) ? -1 : (ea->size < eb->size) ? 1 : 0; /* gros d'abord */
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
        res = g_utf8_collate(ea->name, eb->name); /* departage par nom */
    }
    return res;
}

/* Donnees partagees entre la selection et le visualiseur */
typedef struct {
    GtkTextBuffer* buffer;
    gchar* base_dir;
    GtkTreeModel* model;
    GtkTreeView* tree_view;
    GtkWidget* text_view; /* right pane viewer (Alt+Left/Right cycling) */
    GtkTreeSelection* selection;
    GtkWidget* search_entry;
    GtkWidget* path_label;
    gchar* search_term;
    gboolean show_hidden; /* fichiers caches (prefixe .) */
    gint sort_mode; /* SORT_NAME / DATE / SIZE / EXT */
    gboolean pending_g; /* pour la sequence gg */
    gboolean search_in_viewer; /* '/' opened from the viewer, not the list */
    gint viewer_match; /* last viewer match (char offset), -1 = none */

    /* Surveillance du disque (file monitors) */
    GFileMonitor* dir_monitor;   /* repertoire courant */
    GFileMonitor* file_monitor;  /* fichier affiche a droite */
    gchar* viewed_path;          /* chemin du fichier affiche, NULL sinon */
    gchar* viewed_name;          /* nom du fichier affiche */
    gboolean dir_dirty;          /* pending directory event */
    gboolean file_dirty;         /* pending file event */
    guint refresh_source;        /* debounce timeout id, 0 = idle */

    /* Ctrl-T embedded terminal panel */
    GtkWidget* term_scroll;
#ifdef HAVE_VTE
    GtkWidget* vte;
#endif
} AppData;

/* Moniteurs de fichiers, definis plus bas dans le fichier */
static void monitor_directory(AppData* app);
static void monitor_file(AppData* app, const char* path);
/* Recherche dans le viewer, definie plus bas: utilisee par on_search_changed */
static void do_viewer_search(AppData* app, gint direction);

/* ===== Navigation style vim ===== */

static gint list_count(AppData* app) {
    return gtk_tree_model_iter_n_children(app->model, NULL);
}

static gint current_index(AppData* app) {
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

static void select_index(AppData* app, gint idx) {
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

/* Nombre de lignes visibles dans le treeview (pour Ctrl-F / Ctrl-B) */
static gint count_visible_rows(AppData* app) {
    GtkTreePath* start = NULL;
    GtkTreePath* end = NULL;
    if (!gtk_tree_view_get_visible_range(app->tree_view, &start, &end)) {
        return 10; /* valeur de repli si le modele est vide */
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

/* ===== Recherche (/, n, N) ===== */

static gboolean name_matches(AppData* app, gint idx) {
    GtkTreeIter it;
    if (!gtk_tree_model_iter_nth_child(app->model, &it, NULL, idx)) {
        return FALSE;
    }
    gchar* name = NULL;
    gtk_tree_model_get(app->model, &it, 0, &name, -1);
    gboolean res = FALSE;
    if (name != NULL && app->search_term != NULL) {
        gchar* hay = g_utf8_casefold(name, -1);
        gchar* needle = g_utf8_casefold(app->search_term, -1);
        res = (hay != NULL && needle != NULL && strstr(hay, needle) != NULL);
        g_free(hay);
        g_free(needle);
    }
    g_free(name);
    return res;
}

static void do_search(AppData* app, gint direction) {
    if (app->search_term == NULL || app->search_term[0] == '\0') {
        return;
    }
    gint n = list_count(app);
    if (n <= 0) {
        return;
    }
    gint cur = current_index(app);
    for (gint k = 1; k <= n; k++) {
        gint idx = (direction > 0) ? ((cur + k) % n + n) % n
                                   : ((cur - k) % n + n) % n;
        if (name_matches(app, idx)) {
            select_index(app, idx);
            return;
        }
    }
}

static void show_search(AppData* app, gboolean from_viewer) {
    app->search_in_viewer = from_viewer;
    gtk_widget_show(app->search_entry);
    gtk_widget_grab_focus(app->search_entry);
}

static void hide_search(AppData* app) {
    gtk_widget_hide(app->search_entry);
    /* Return to the panel that opened the bar */
    gtk_widget_grab_focus(app->search_in_viewer ? app->text_view
                                                : GTK_WIDGET(app->tree_view));
}

static void on_search_changed(GtkSearchEntry* entry, gpointer data) {
    AppData* app = (AppData*)data;
    const gchar* text = gtk_entry_get_text(GTK_ENTRY(entry));
    g_free(app->search_term);
    app->search_term = g_strdup(text);
    if (app->search_in_viewer) {
        do_viewer_search(app, +1);
    } else {
        do_search(app, +1);
    }
}

static void on_search_activate(GtkWidget* entry, gpointer data) {
    (void)entry;
    hide_search((AppData*)data);
}

static gboolean on_search_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data) {
    (void)widget;
    if (event->keyval == GDK_KEY_Escape) {
        hide_search((AppData*)data);
        return TRUE;
    }
    return FALSE;
}

/* ===== Right pane (viewer focused): same vim bindings as the list ===== */

/* j / k in the viewer: scroll the viewport by one text line */
static void viewer_scroll_lines(AppData* app, gint lines) {
    GtkTextView* view = GTK_TEXT_VIEW(app->text_view);
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));

    /* One line height taken from the first buffered line */
    GtkTextIter first;
    gtk_text_buffer_get_start_iter(app->buffer, &first);
    GdkRectangle rect;
    gtk_text_view_get_iter_location(view, &first, &rect);
    gdouble line_h = (rect.height > 0.0) ? rect.height : 14.0;

    gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) + lines * line_h);
}

/* Ctrl+F / Ctrl+B in the viewer: one viewport page down / up */
static void viewer_scroll_page(AppData* app, gint direction) {
    GtkTextView* view = GTK_TEXT_VIEW(app->text_view);
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
    gtk_adjustment_set_value(
        adj, gtk_adjustment_get_value(adj) + direction * gtk_adjustment_get_page_size(adj));
}

/* gg / G in the viewer: jump to the top / bottom of the buffer */
static void viewer_goto(AppData* app, gboolean top) {
    GtkTextView* view = GTK_TEXT_VIEW(app->text_view);
    GtkTextIter it;
    if (top) {
        gtk_text_buffer_get_start_iter(app->buffer, &it);
    } else {
        gtk_text_buffer_get_end_iter(app->buffer, &it);
    }
    gtk_text_view_scroll_to_iter(view, &it, 0.0, FALSE, 0.0, top ? 0.0 : 1.0);
}

/* Case-insensitive search (char-wise folding) from a char-offset anchor.
 * Returns the match start offset or -1. */
static gint utf8_ci_find_from(const gchar* hay, const gchar* needle, gint start) {
    const gint hlen = g_utf8_strlen(hay, -1);
    const gint nlen = g_utf8_strlen(needle, -1);
    if (nlen == 0 || hlen == 0 || nlen > hlen) {
        return -1;
    }

    gunichar* hcv = g_new(gunichar, hlen); /* NOSONAR: bounded by PREVIEW_BYTES */
    gunichar* ncv = g_new(gunichar, nlen);
    const gchar* p = hay;
    for (gint i = 0; i < hlen; i++) {
        hcv[i] = g_unichar_tolower(g_utf8_get_char(p));
        p = g_utf8_next_char(p);
    }
    p = needle;
    for (gint i = 0; i < nlen; i++) {
        ncv[i] = g_unichar_tolower(g_utf8_get_char(p));
        p = g_utf8_next_char(p);
    }

    gint max_start = hlen - nlen;
    gint match = -1;
    const gint kmax = (start <= max_start) ? max_start : -1;
    for (gint k = start; k <= kmax; k++) {
        if (hcv[k] != ncv[0]) {
            continue;
        }
        gint j = 1;
        while (j < nlen && hcv[k + j] == ncv[j]) {
            j++;
        }
        if (j == nlen) {
            match = k;
            break;
        }
    }
    g_free(hcv);
    g_free(ncv);
    return match;
}

/* Viewer search (/ n N): select and scroll to next/prev occurrence */
static void do_viewer_search(AppData* app, gint direction) {
    if (app->search_term == NULL || app->search_term[0] == '\0') {
        return;
    }
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(app->buffer, &start, &end);
    gchar* text = gtk_text_buffer_get_text(app->buffer, &start, &end, FALSE);
    if (text == NULL || text[0] == '\0') {
        g_free(text);
        return;
    }

    const gint hlen = g_utf8_strlen(text, -1);
    /* Anchor: the last match start (-1 = none). For the next-occurrence
     * direction we resume after it; for previous we stay below it. */
    gint anchor = ((app->viewer_match >= 0 && app->viewer_match < hlen))
                      ? app->viewer_match : -1;

    gint match = -1;
    if (direction > 0) {
        /* After the current match (or from the top) */
        gint from = (anchor >= 0) ? anchor : 0;
        const gchar* ap = (anchor >= 0)
            ? g_utf8_offset_to_pointer(text, anchor) : text;
        from = g_utf8_pointer_to_offset(text, g_utf8_next_char(ap));
        match = utf8_ci_find_from(text, app->search_term, from);
        if (match < 0 && from > 0) {
            /* Wrap around: search again from the top */
            match = utf8_ci_find_from(text, app->search_term, 0);
        }
    } else {
        /* Previous match: scan forward, keep the last one strictly before
         * the anchor (or the last one overall when nothing is anchored) */
        const gint limit = (anchor >= 0) ? anchor : hlen;
        gint probe = 0;
        while (probe <= limit) {
            gint m = utf8_ci_find_from(text, app->search_term, probe);
            if (m < 0 || m >= limit) {
                break;
            }
            match = m;
            probe = m + 1;
        }
        if (match < 0 && anchor >= 0) {
            /* Wrap around: jump to the last occurrence in the buffer */
            gint probe2 = 0;
            for (;;) {
                gint m = utf8_ci_find_from(text, app->search_term, probe2);
                if (m < 0) {
                    break;
                }
                match = m;
                probe2 = m + 1;
            }
        }
    }

    if (match >= 0) {
        gint nlen = g_utf8_strlen(app->search_term, -1);
        GtkTextIter ms, me;
        gtk_text_buffer_get_start_iter(app->buffer, &ms);
        gtk_text_iter_set_offset(&ms, match);
        gtk_text_buffer_get_start_iter(app->buffer, &me);
        gtk_text_iter_set_offset(&me, match + nlen);
        gtk_text_buffer_select_range(app->buffer, &ms, &me);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(app->text_view), &ms,
                                     0.0, TRUE, 0.0, 0.5);
        app->viewer_match = match;
    }
    g_free(text);
}

/* Handler clavier du viewer : j k gg G / n N Ctrl+F Ctrl+B */
static gboolean on_viewer_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data) {
    (void)widget;
    AppData* app = (AppData*)data;
    guint key = event->keyval;

    switch (key) {
    case GDK_KEY_j: /* une ligne vers le bas */
        app->pending_g = FALSE;
        viewer_scroll_lines(app, +1);
        return TRUE;
    case GDK_KEY_k: /* une ligne vers le haut */
        app->pending_g = FALSE;
        viewer_scroll_lines(app, -1);
        return TRUE;
    case GDK_KEY_g: /* gg = haut du fichier */
        if (app->pending_g) {
            app->pending_g = FALSE;
            viewer_goto(app, TRUE);
        } else {
            app->pending_g = TRUE;
        }
        return TRUE;
    case GDK_KEY_G: /* bas du fichier */
        app->pending_g = FALSE;
        viewer_goto(app, FALSE);
        return TRUE;
    case GDK_KEY_slash:
        app->pending_g = FALSE;
        show_search(app, TRUE);
        return TRUE;
    case GDK_KEY_n: /* occurrence suivante */
        app->pending_g = FALSE;
        do_viewer_search(app, +1);
        return TRUE;
    case GDK_KEY_N: /* occurrence precedente */
        app->pending_g = FALSE;
        do_viewer_search(app, -1);
        return TRUE;
    case GDK_KEY_b: /* Ctrl+B : page precedente */
        app->pending_g = FALSE;
        if (event->state & GDK_CONTROL_MASK) {
            viewer_scroll_page(app, -1);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_f: /* Ctrl+F : page suivante */
        app->pending_g = FALSE;
        if (event->state & GDK_CONTROL_MASK) {
            viewer_scroll_page(app, +1);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_Escape:
        app->pending_g = FALSE;
        return FALSE;
    default:
        app->pending_g = FALSE;
        return FALSE;
    }
}

/* VS Code-like emoji for a tree entry, based on kind / extension */
static const gchar* file_icon(const gchar* name, gboolean is_dir) {
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

/* Remplit le store avec le contenu reel du repertoire (dossiers d'abord, tries) */
static void populate_store(GtkListStore* store, const gchar* dirname, gboolean show_hidden, gint sort_mode) {
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

    /* Collecter les entrees avec leurs metadonnees (tri) */
    GArray* entries = g_array_new(FALSE, FALSE, sizeof(FileEntry));
    const gchar* name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (!show_hidden && name[0] == '.') {
            continue; /* fichier cache masque */
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

    /* Trier selon le mode courant (dossiers toujours en premier) */
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

/* Label du haut : repertoire courant + tri + fichiers caches, toujours a jour */
static void update_path_label(AppData* app) {
    static const gchar* sort_names[SORT_COUNT] = {"name", "date", "size", "ext"};
    gchar* text = g_strdup_printf("Directory: %s   [sort: %s] [hidden: %s]",
                                  app->base_dir, sort_names[app->sort_mode],
                                  app->show_hidden ? "on" : "off");
    gtk_label_set_text(GTK_LABEL(app->path_label), text);
    g_free(text);
}

/* Navigue vers un nouveau repertoire (Entree / Retour arriere) */
static void reload_directory(AppData* app, const gchar* new_dir) {
    if (!g_file_test(new_dir, G_FILE_TEST_IS_DIR)) {
        return;
    }

    g_free(app->base_dir);
    app->base_dir = g_strdup(new_dir);
    update_path_label(app);

    /* La recherche precedente n'a plus de sens dans un nouveau repertoire */
    g_free(app->search_term);
    app->search_term = NULL;
    gtk_entry_set_text(GTK_ENTRY(app->search_entry), "");

    populate_store(GTK_LIST_STORE(app->model), new_dir, app->show_hidden, app->sort_mode);

    /* Vider le panneau droit */
    gtk_text_buffer_set_text(app->buffer, "", -1);
    app->viewer_match = -1;

    /* Plus de fichier affiche a droite : couper son moniteur */
    g_free(app->viewed_path);
    g_free(app->viewed_name);
    app->viewed_path = NULL;
    app->viewed_name = NULL;
    app->file_dirty = FALSE;
    app->dir_dirty = FALSE;
    monitor_file(app, NULL);
    monitor_directory(app);

    /* selectionner la premiere entree */
    if (list_count(app) > 0) {
        select_index(app, 0);
    }
}

/* Re-peuple la liste en conservant la selection courante (nom) */
static void refresh_listing(AppData* app) {
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

/* I : basculer l'affichage des fichiers caches (prefixe .) */
static void toggle_hidden_files(AppData* app) {
    app->show_hidden = !app->show_hidden;
    refresh_listing(app);
}

/* o : cycle de tri nom -> date -> taille -> extension */
static void cycle_sort_mode(AppData* app) {
    app->sort_mode = (app->sort_mode + 1) % SORT_COUNT;
    refresh_listing(app);
}

/* Enter the selected entry when it is a directory (Enter key / double click) */
static void enter_selected_directory(AppData* app) {
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
    }
    g_free(name);
    g_free(type);
}

/* Double click on a row: enter it when it is a directory */
static void on_row_activated(GtkTreeView* view, GtkTreePath* path,
                             GtkTreeViewColumn* column, gpointer data) {
    (void)view;
    (void)path;
    (void)column;
    enter_selected_directory((AppData*)data);
}

/* Handler clavier du treeview : j k gg G / n N */
static gboolean on_tree_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data) {
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
        /* gg = aller au debut */
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
    case GDK_KEY_n: /* occurrence suivante */
        app->pending_g = FALSE;
        do_search(app, +1);
        return TRUE;
    case GDK_KEY_N: /* occurrence precedente */
        app->pending_g = FALSE;
        do_search(app, -1);
        return TRUE;
    case GDK_KEY_Return: /* Entrer dans le repertoire selectionne */
    case GDK_KEY_KP_Enter:
    case GDK_KEY_Delete:
    case GDK_KEY_KP_Delete:
    case GDK_KEY_ISO_Enter:
        app->pending_g = FALSE;
        enter_selected_directory(app);
        return TRUE;
    case GDK_KEY_BackSpace: /* Repertoire parent */
        app->pending_g = FALSE;
        {
            gchar* parent = g_path_get_dirname(app->base_dir);
            if (g_strcmp0(parent, app->base_dir) != 0) {
                reload_directory(app, parent);
            }
            g_free(parent);
        }
        return TRUE;
    case GDK_KEY_I: /* basculer les fichiers caches */
    case GDK_KEY_i:
        app->pending_g = FALSE;
        toggle_hidden_files(app);
        return TRUE;
    case GDK_KEY_o: /* cycle de tri : nom -> date -> taille -> ext */
        app->pending_g = FALSE;
        cycle_sort_mode(app);
        return TRUE;
    case GDK_KEY_b: /* Ctrl+B : page precedente */
        app->pending_g = FALSE;
        if (event->state & GDK_CONTROL_MASK) {
            select_index(app, current_index(app) - count_visible_rows(app));
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_f: /* Ctrl+F : page suivante */
        app->pending_g = FALSE;
        if (event->state & GDK_CONTROL_MASK) {
            select_index(app, current_index(app) + count_visible_rows(app));
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_Escape:
        app->pending_g = FALSE;
        return FALSE;
    default:
        app->pending_g = FALSE; /* toute autre touche annule gg */
        break;
    }
    return FALSE;
}

/* GtkSourceView: syntax highlighting language for the displayed entry;
 * NULL name resets to plain text (directory listings, errors...). */
static void apply_viewer_language(AppData* app, const gchar* name) {
#ifdef HAVE_GSV
    GtkSourceLanguageManager* mgr = gtk_source_language_manager_get_default();
    GtkSourceLanguage* lang = NULL;
    if (name != NULL) {
        gchar* ctype = g_content_type_guess(name, NULL, 0, NULL);
        lang = gtk_source_language_manager_guess_language(mgr, name, ctype);
        g_free(ctype);
    }
    GtkSourceBuffer* sbuf = GTK_SOURCE_BUFFER(app->buffer);
    gtk_source_buffer_set_language(sbuf, lang);
    gtk_source_buffer_set_highlight_syntax(sbuf, lang != NULL);
#else
    (void)app;
    (void)name;
#endif
}

#ifdef HAVE_GSV
/* VS Code Dark+ palette, as a GtkSourceView style scheme.
 * Both bare language-local ids and core "def:*" ids are defined: language
 * styles resolve through their map-to="def:*" target, so only those hit. */
static const gchar* VSCODE_SCHEME_XML =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!-- Inherits from a built-in dark scheme so languages whose style ids\n"
    "     are not overridden here keep Dark+ compatible colors. -->\n"
    "<style-scheme id=\"vscode-dark\" _name=\"VS Code Dark\" version=\"1.0\"\n"
    "               parent-scheme=\"oblivion\">\n"
    "  <_description>VS Code Dark+ palette for the file explorer viewer</_description>\n"
    "  <style name=\"def\" foreground=\"#d4d4d4\"/>\n"
    "  <style name=\"selection\" background=\"#264f78\"/>\n"
    "  <style name=\"current-line\" background=\"#2a2d2e\"/>\n"
    "  <style name=\"current-line-search\" background=\"#314365\"/>\n"
    "  <style name=\"bracket-match\" background=\"#515553\"/>\n"
    "  <style name=\"comment\" foreground=\"#6a9955\"/>\n"
    "  <style name=\"def:comment\" foreground=\"#6a9955\"/>\n"
    "  <style name=\"shebang\" foreground=\"#6a9955\"/>\n"
    "  <style name=\"def:shebang\" foreground=\"#6a9955\"/>\n"
    "  <style name=\"constant\" foreground=\"#569cd6\"/>\n"
    "  <style name=\"def:constant\" foreground=\"#569cd6\"/>\n"
    "  <style name=\"boolean\" foreground=\"#569cd6\"/>\n"
    "  <style name=\"def:boolean\" foreground=\"#569cd6\"/>\n"
    "  <style name=\"special-constant\" foreground=\"#4fc1ff\"/>\n"
    "  <style name=\"def:special-constant\" foreground=\"#4fc1ff\"/>\n"
    "  <style name=\"string\" foreground=\"#ce9178\"/>\n"
    "  <style name=\"def:string\" foreground=\"#ce9178\"/>\n"
    "  <style name=\"character\" foreground=\"#ce9178\"/>\n"
    "  <style name=\"def:character\" foreground=\"#ce9178\"/>\n"
    "  <style name=\"escape\" foreground=\"#d7ba7d\"/>\n"
    "  <style name=\"def:escape\" foreground=\"#d7ba7d\"/>\n"
    "  <style name=\"special-char\" foreground=\"#d7ba7d\"/>\n"
    "  <style name=\"def:special-char\" foreground=\"#d7ba7d\"/>\n"
    "  <style name=\"number\" foreground=\"#b5cea8\"/>\n"
    "  <style name=\"def:decimal\" foreground=\"#b5cea8\"/>\n"
    "  <style name=\"def:floating-point\" foreground=\"#b5cea8\"/>\n"
    "  <style name=\"keyword\" foreground=\"#569cd6\"/>\n"
    "  <style name=\"def:keyword\" foreground=\"#569cd6\"/>\n"
    "  <style name=\"keyword-modifier\" foreground=\"#569cd6\"/>\n"
    "  <style name=\"preprocessor\" foreground=\"#c586c0\"/>\n"
    "  <style name=\"def:preprocessor\" foreground=\"#c586c0\"/>\n"
    "  <style name=\"type\" foreground=\"#4ec9b0\"/>\n"
    "  <style name=\"def:type\" foreground=\"#4ec9b0\"/>\n"
    "  <style name=\"function\" foreground=\"#dcdcaa\"/>\n"
    "  <style name=\"def:function\" foreground=\"#dcdcaa\"/>\n"
    "  <style name=\"builtin-function\" foreground=\"#dcdcaa\"/>\n"
    "  <style name=\"variable\" foreground=\"#9cdcfe\"/>\n"
    "  <style name=\"def:variable\" foreground=\"#9cdcfe\"/>\n"
    "  <style name=\"operator\" foreground=\"#d4d4d4\"/>\n"
    "  <style name=\"def:operator\" foreground=\"#d4d4d4\"/>\n"
    "  <style name=\"error\" foreground=\"#f44747\"/>\n"
    "  <style name=\"def:error\" foreground=\"#f44747\"/>\n"
    "  <style name=\"included-file\" foreground=\"#ce9178\"/>\n"
    "  <style name=\"include-statement\" foreground=\"#c586c0\"/>\n"
    "  <style name=\"net-address-in-comment\" foreground=\"#6a9955\"/>\n"
    "</style-scheme>\n";

/* VS Code editor-like dark background for the viewer widget itself: style
 * scheme tags only shade text spans, the widget base is painted from CSS. */
static void apply_viewer_background(GtkWidget* view) {
    gtk_widget_set_name(view, "viewer");
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(
        css, "#viewer text { background-color: #1e1e1e; }", -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(view),
                                   GTK_STYLE_PROVIDER(css),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
}

/* Install the VS Code-style scheme and apply it to the viewer buffer:
 * the XML lives in the user cache dir (regenerated at each start). */
static void apply_vscode_scheme(GtkTextBuffer* buffer, GtkWidget* view) {
    GtkSourceStyleSchemeManager* mgr =
        gtk_source_style_scheme_manager_get_default();
    gchar* dir = g_build_filename(g_get_user_cache_dir(), "file-explorer",
                                  "scheme", NULL);
    if (g_mkdir_with_parents(dir, 0755) != 0) {
        g_free(dir);
        return;
    }
    gchar* path = g_build_filename(dir, "vscode-dark.xml", NULL);
    g_file_set_contents(path, VSCODE_SCHEME_XML, -1, NULL);
    g_free(path);
    gtk_source_style_scheme_manager_append_search_path(mgr, dir);
    g_free(dir);
    gtk_source_style_scheme_manager_force_rescan(mgr);

    GtkSourceStyleScheme* scheme =
        gtk_source_style_scheme_manager_get_scheme(mgr, "vscode-dark");
    if (scheme != NULL) {
        gtk_source_buffer_set_style_scheme(GTK_SOURCE_BUFFER(buffer), scheme);
        apply_viewer_background(view);
    } else {
        g_printerr("[ VS Code scheme unavailable: keeping default syntax colors ]\n");
    }
}
#endif

/* Charge le contenu du fichier selectionne dans le panneau droit */
/* Charge le contenu d'un fichier (avec apercu + sanitization) dans le buffer
 * Utilise par la selection ET par la surveillance de fichiers */
static void load_file_content(AppData* app, const gchar* name, const gchar* full) {
    apply_viewer_language(app, name);
    /* Taille du fichier pour decider entier vs apercu */
    struct stat st;
    gint64 fsize = (g_stat(full, &st) == 0) ? (gint64)st.st_size : -1;
    gchar* contents = NULL;
    gsize len = 0;
    gboolean preview = FALSE;

    if (fsize < 0) {
        gchar* msg = g_strdup_printf("[ Cannot read: %s ]", name);
        gtk_text_buffer_set_text(app->buffer, msg, -1);
        g_free(msg);
        app->viewer_match = -1;
        return;
    }

    if (fsize > PREVIEW_THRESHOLD) {
        /* Gros fichier : ne lire que le debut, pas le contenu entier */
        preview = TRUE;
        FILE* fp = g_fopen(full, "rb");
        if (fp != NULL) {
            contents = g_malloc0(PREVIEW_BYTES + 1);
            len = fread(contents, 1, PREVIEW_BYTES, fp);
            contents[len] = '\0';
            fclose(fp);
        }
    } else {
        g_file_get_contents(full, &contents, &len, NULL);
    }

    if (contents != NULL) {
        GString* text = g_string_new(NULL);
        if (preview) {
            g_string_append_printf(text,
                    "[ PREVIEW ] %s (%" G_GINT64_FORMAT " bytes total)\n"
                    "Showing first %u bytes. Large file, display truncated.\n\n",
                    name, fsize, (guint)len);
        }

        /* GtkTextBuffer exige de l'UTF-8 valide : nettoyer si besoin */
        if (len > 0 && !g_utf8_validate(contents, (gssize)len, NULL)) {
            gchar* valid = g_utf8_make_valid(contents, (gssize)len);
            g_string_append(text, valid);
            g_free(valid);
        } else {
            g_string_append_len(text, contents, (gssize)len);
        }
        gtk_text_buffer_set_text(app->buffer, text->str, (gint)text->len);
        app->viewer_match = -1; /* stale anchor: content just changed */
        g_string_free(text, TRUE);
        g_free(contents);
    } else {
        gchar* msg = g_strdup_printf("[ Cannot read: %s ]", name);
        gtk_text_buffer_set_text(app->buffer, msg, -1);
        g_free(msg);
        app->viewer_match = -1;
    }
}

/* ===== Surveillance du disque (GFileMonitor + debonce) ===== */

/* Un seul rafraichissement liste par le debounce : rep. et/ou contenu */
static gboolean deferred_refresh(gpointer data) {
    AppData* app = (AppData*)data;
    app->refresh_source = 0;

    if (app->dir_dirty) {
        app->dir_dirty = FALSE;
        refresh_listing(app); /* conserve la selection par nom */
    }
    if (app->file_dirty) {
        app->file_dirty = FALSE;
        if (app->viewed_path != NULL) {
            load_file_content(app, app->viewed_name, app->viewed_path);
        }
    }
    return G_SOURCE_REMOVE;
}

static void schedule_refresh(AppData* app) {
    if (app->refresh_source == 0) {
        app->refresh_source = g_timeout_add(150, deferred_refresh, app);
    }
}

static void on_dir_changed(GFileMonitor* monitor, GFile* file, GFile* other_file,
                           GFileMonitorEvent event_type, gpointer data) {
    (void)monitor; (void)file; (void)other_file;
    switch (event_type) {
    case G_FILE_MONITOR_EVENT_CREATED:
    case G_FILE_MONITOR_EVENT_DELETED:
    case G_FILE_MONITOR_EVENT_MOVED:
    case G_FILE_MONITOR_EVENT_RENAMED:
        ((AppData*)data)->dir_dirty = TRUE;
        schedule_refresh((AppData*)data);
        break;
    default:
        break;
    }
}

static void on_file_changed(GFileMonitor* monitor, GFile* file, GFile* other_file,
                            GFileMonitorEvent event_type, gpointer data) {
    (void)monitor; (void)file; (void)other_file;
    switch (event_type) {
    case G_FILE_MONITOR_EVENT_CHANGED:
    case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
    case G_FILE_MONITOR_EVENT_DELETED:
        ((AppData*)data)->file_dirty = TRUE;
        schedule_refresh((AppData*)data);
        break;
    default:
        break;
    }
}

/* (Re)arme le moniteur du repertoire courant */
static void monitor_directory(AppData* app) {
    if (app->dir_monitor != NULL) {
        g_object_unref(app->dir_monitor);
        app->dir_monitor = NULL;
    }
    GFile* f = g_file_new_for_path(app->base_dir);
    GError* err = NULL;
    app->dir_monitor = g_file_monitor_directory(f, G_FILE_MONITOR_NONE, NULL, &err);
    if (err != NULL) {
        g_error_free(err);
        app->dir_monitor = NULL;
    } else {
        g_signal_connect(app->dir_monitor, "changed", G_CALLBACK(on_dir_changed), app);
    }
    g_object_unref(f);
}

/* (Re)arme le moniteur du fichier affiche ; path = NULL pour couper */
static void monitor_file(AppData* app, const char* path) {
    if (app->file_monitor != NULL) {
        g_object_unref(app->file_monitor);
        app->file_monitor = NULL;
    }
    if (path != NULL) {
        GFile* f = g_file_new_for_path(path);
        GError* err = NULL;
        app->file_monitor = g_file_monitor(f, G_FILE_MONITOR_NONE, NULL, &err);
        if (err != NULL) {
            g_error_free(err);
            app->file_monitor = NULL;
        } else {
            g_signal_connect(app->file_monitor, "changed",
                             G_CALLBACK(on_file_changed), app);
        }
        g_object_unref(f);
    }
}

static void on_selection_changed(GtkTreeSelection* selection, gpointer user_data) {
    AppData* app = (AppData*)user_data;
    GtkTreeModel* model = NULL;
    GtkTreeIter it;

    if (!gtk_tree_selection_get_selected(selection, &model, &it)) {
        return;
    }

    gchar* name = NULL;
    gchar* type = NULL;
    gtk_tree_model_get(model, &it, 0, &name, 1, &type, -1);

    gchar* full = g_build_filename(app->base_dir, name, NULL);

    if (g_strcmp0(type, "dir") == 0) {
        /* Lister le contenu du repertoire dans le panneau droit */
        GString* listing = g_string_new(NULL);
        g_string_append_printf(listing, "[ Directory ] %s\n\n", name);

        GDir* sub = g_dir_open(full, 0, NULL);
        if (sub != NULL) {
            GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
            const gchar* sub_name;
            while ((sub_name = g_dir_read_name(sub)) != NULL) {
                if (!app->show_hidden && sub_name[0] == '.') {
                    continue;
                }
                g_ptr_array_add(names, g_strdup(sub_name));
            }
            g_dir_close(sub);
            g_ptr_array_sort(names, (GCompareFunc)g_strcmp0);

            for (guint i = 0; i < names->len; i++) {
                const gchar* sub_entry = g_ptr_array_index(names, i);
                gchar* sub_full = g_build_filename(full, sub_entry, NULL);
                gboolean sub_is_dir = g_file_test(sub_full, G_FILE_TEST_IS_DIR);
                g_free(sub_full);
                g_string_append_printf(listing, "  %s%s\n",
                                       sub_entry, sub_is_dir ? "/" : "");
            }
            if (names->len == 0) {
                g_string_append(listing, "  (empty)\n");
            }
            g_ptr_array_unref(names);
        } else {
            g_string_append(listing, "  (cannot read directory)\n");
        }

        gtk_text_buffer_set_text(app->buffer, listing->str, (gint)listing->len);
        app->viewer_match = -1;
        apply_viewer_language(app, NULL); /* listings stay plain text */
        g_string_free(listing, TRUE);
    } else {
        load_file_content(app, name, full);
    }

    /* Suivre le fichier affiche pour recharger au changement disque */
    g_free(app->viewed_path);
    g_free(app->viewed_name);
    if (g_strcmp0(type, "dir") == 0) {
        app->viewed_path = NULL;
        app->viewed_name = NULL;
        monitor_file(app, NULL);
    } else {
        app->viewed_path = g_strdup(full);
        app->viewed_name = g_strdup(name);
        monitor_file(app, full);
    }

    g_free(full);
    g_free(name);
    g_free(type);
}

/* ===== Ctrl-T: embedded terminal in a panel right of the viewer ===== */

#ifdef HAVE_VTE
/* VS Code Dark+ integrated-terminal palette: fg/bg like the editor, plus
 * the 16 ANSI colors it ships with (normal row then bright row). */
static const gchar* VSCODE_TERM_FG = "#cccccc";
static const gchar* VSCODE_TERM_BG = "#1e1e1e";
static const gchar* VSCODE_TERM_CURSOR = "#ffffff";
static const gchar* VSCODE_TERM_SELECTION = "#264f78";
static const gchar* VSCODE_TERM_ANSI[16] = {
    "#000000", "#cd3131", "#0dbc79", "#e5e510",
    "#2472c8", "#bc3fbc", "#11a8cd", "#e5e5e5",
    "#666666", "#f14c4c", "#23d18b", "#f5f543",
    "#3b8eea", "#d670d6", "#29b8db", "#e5e5e5",
};

static void apply_terminal_theme(AppData* app) {
    VteTerminal* term = VTE_TERMINAL(app->vte);
    GdkRGBA fg, bg;
    GdkRGBA ansi[16];
    gsize i;

    if (!gdk_rgba_parse(&fg, VSCODE_TERM_FG) || !gdk_rgba_parse(&bg, VSCODE_TERM_BG)) {
        return; /* invalid literal: keep VTE defaults */
    }
    for (i = 0; i < 16; i++) {
        gdk_rgba_parse(&ansi[i], VSCODE_TERM_ANSI[i]);
    }
    vte_terminal_set_colors(term, &fg, &bg, ansi, 16);

    GdkRGBA cursor;
    if (gdk_rgba_parse(&cursor, VSCODE_TERM_CURSOR)) {
        vte_terminal_set_color_cursor(term, &cursor);
    }
    GdkRGBA selection;
    if (gdk_rgba_parse(&selection, VSCODE_TERM_SELECTION)) {
        vte_terminal_set_color_highlight(term, &selection);
    }
}

/* Async spawn callback: report failures, then hand the child PID to the
 * terminal so it tracks the shell ("child-exited" stays reliable) */
static void on_spawn_result(VteTerminal* term, GPid pid, GError* error,
                            gpointer data) {
    (void)data;
    if (error != NULL) {
        g_printerr("[ Cannot start terminal shell: %s ]\n", error->message);
        g_error_free(error);
        return;
    }
    vte_terminal_watch_child(term, pid);
}

static void spawn_shell(AppData* app) {
    VteTerminal* term = VTE_TERMINAL(app->vte);
    const gchar* shell = g_getenv("SHELL");
    if (shell == NULL || shell[0] == '\0') {
        shell = "/bin/bash";
    }
    gchar* argv[2] = { (gchar*)shell, NULL };
    vte_terminal_spawn_async(term, VTE_PTY_DEFAULT, app->base_dir, argv, NULL,
                             G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, -1, NULL,
                             on_spawn_result, app);
}

static void on_term_child_exited(VteTerminal* term, gint status, gpointer data) {
    (void)term;
    (void)status;
    AppData* app = (AppData*)data;
    gtk_widget_hide(app->term_scroll);
    gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
}
#endif

/* Window-level handler: catches Ctrl-T before children, but only eats it
 * (other keys propagate to the focused widget / vim bindings untouched) */
/* gtk_widget_grab_focus() requires a REALIZED widget; a freshly created
 * terminal is not until it gets mapped, so grab the focus on realize */
static void on_vte_realize(GtkWidget* term, gpointer data) {
    (void)data;
    gtk_widget_grab_focus(term);
}

/* Alt+Left / Alt+Right: move the keyboard focus between the open panels:
 * file list -> text viewer -> Ctrl-T terminal panel (when visible) */
static void focus_adjacent_panel(gint step, AppData* app) {
    GtkWidget* panels[3];
    gint count = 0;
    gint current = -1;

    panels[count++] = GTK_WIDGET(app->tree_view);
    panels[count++] = app->text_view;
#ifdef HAVE_VTE
    if (app->vte != NULL && gtk_widget_get_visible(app->vte)) {
        panels[count++] = app->vte;
    }
#endif

    /* Which panel owns the focus? Walk the focused widget's ancestors */
    GtkWidget* w = gtk_window_get_focus(GTK_WINDOW(
        gtk_widget_get_toplevel(GTK_WIDGET(app->tree_view))));
    while (w != NULL && current < 0) {
        gint i;
        for (i = 0; i < count; i++) {
            if (panels[i] == w) {
                current = i;
                break;
            }
        }
        w = gtk_widget_get_parent(w);
    }
    if (current < 0) {
        current = 0; /* unknown focus (e.g. search bar): count as first panel */
    }

    gtk_widget_grab_focus(panels[(current + step + count) % count]);
}

static gboolean on_window_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data) {
    (void)widget;
    AppData* app = (AppData*)data;

    /* Alt+Left / Alt+Right: panel focus cycling */
    if ((event->state & GDK_MOD1_MASK) != 0 &&
        (event->keyval == GDK_KEY_Left || event->keyval == GDK_KEY_Right)) {
        focus_adjacent_panel(event->keyval == GDK_KEY_Right ? 1 : -1, app);
        return TRUE;
    }

    if ((event->state & GDK_CONTROL_MASK) == 0 || event->keyval != GDK_KEY_t) {
        return FALSE;
    }

#ifdef HAVE_VTE
    if (gtk_widget_get_visible(app->term_scroll)) {
        /* Ctrl-T again: hide the panel, the shell keeps running */
        gtk_widget_hide(app->term_scroll);
        gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
        return TRUE;
    }

    /* Show: fresh shell spawned in the current directory.
     * A shell idling inside the panel is old news: exit with Ctrl-D. */
    if (app->vte != NULL) {
        gtk_widget_destroy(app->vte);
    }
    gtk_widget_show(app->term_scroll);
    app->vte = vte_terminal_new();
    gtk_container_add(GTK_CONTAINER(app->term_scroll), app->vte);
    apply_terminal_theme(app);
    g_signal_connect(app->vte, "child-exited",
                     G_CALLBACK(on_term_child_exited), app);
    g_signal_connect(app->vte, "realize",
                     G_CALLBACK(on_vte_realize), app);
    spawn_shell(app);
    /* term_scroll has no-show-all: gtk_widget_show_all() on it would NOT
     * reach children, so show the terminal explicitly */
    gtk_widget_show(GTK_WIDGET(app->vte));
    /* Focus is taken in on_vte_realize(), once the terminal is realized */
#else
    /* No VTE installed: the panel is a notice instead of a terminal */
    gtk_widget_set_visible(app->term_scroll,
                           !gtk_widget_get_visible(app->term_scroll));
    gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
#endif
    return TRUE;
}

int main(int argc, char** argv) {
    /* Strip our own options first: gtk_init aborts on unknown options.
     * -f / --fullscreen: start in fullscreen mode. */
    gboolean start_fullscreen = FALSE;
    int kept = 0;
    for (int i = 0; i < argc; i++) {
        if (i > 0 && (g_strcmp0(argv[i], "-f") == 0 ||
                      g_strcmp0(argv[i], "--fullscreen") == 0)) {
            start_fullscreen = TRUE;
            continue;
        }
        argv[kept++] = argv[i];
    }
    argv[kept] = NULL;
    argc = kept;
    gtk_init(&argc, &argv);

    /* Theme sombre natif d'Adwaita */
    GtkSettings* settings = gtk_settings_get_default();
    g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_window_set_title(GTK_WINDOW(window), "File Explorer");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 600);

    /* Raccourci Ctrl+Q pour quitter l'application */
    GtkAccelGroup* accel = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(window), accel);
    gtk_accel_group_connect(accel, GDK_KEY_q, GDK_CONTROL_MASK, 0,
                            g_cclosure_new(G_CALLBACK(gtk_main_quit), NULL, NULL));

    /* Get current directory */
    char cwd_buf[2048];
    const char* cwd = (getcwd(cwd_buf, sizeof(cwd_buf)) != NULL) ? cwd_buf : "/workspace";

    /* Label pour montrer le path actuel (au-dessus de la zone divisée) */
    char path_text[512];
    snprintf(path_text, sizeof(path_text), "Directory: %s", cwd);
    GtkWidget* path_label = gtk_label_new(path_text);
    gtk_widget_set_halign(path_label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(path_label, 5);

    /* Label pour la colonne de droite */
    GtkWidget* sep_label = gtk_label_new(" ");
    gtk_widget_set_halign(sep_label, GTK_ALIGN_START);

    /* ===== Panneau gauche : liste des fichiers =====
     * Model: col0 = raw name, col1 = type, col2 = display label (VS Code-like emoji + name) */
    GtkListStore* store =
        gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

    /* Enumerer le contenu reel du repertoire courant */
    populate_store(store, cwd, FALSE, SORT_NAME); /* caches masques, tri par nom */

    /* GtkTreeView avec deux colonnes (Nom / Type) */
    GtkWidget* tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree_view), FALSE);

    GtkTreeViewColumn* col_name = gtk_tree_view_column_new();
    gtk_tree_view_column_set_expand(col_name, TRUE);
    GtkCellRenderer* renderer_name = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col_name, renderer_name, TRUE);
    gtk_tree_view_column_add_attribute(col_name, renderer_name, "text", 2);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col_name);

    /* Model still carries the raw "dir"/"file" type in column 1 (used by
     * handlers); only the Type column display was removed. */

    g_object_unref(store);

    /* Le treeview dans un ScrolledWindow */
    GtkWidget* left_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(left_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(left_scroll), tree_view);

    /* ===== Panneau droit : visualiseur de texte (lecture seule) =====
     * GtkSourceView (coloration syntaxique) si installe, sinon GtkTextView. */
#ifdef HAVE_GSV
    GtkWidget* text_view = gtk_source_view_new();
#else
    GtkWidget* text_view = gtk_text_view_new();
#endif
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);

#ifdef HAVE_GSV
    /* Syntax highlighting on the source buffer backing the view */
    gtk_source_buffer_set_highlight_syntax(
        GTK_SOURCE_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view))), TRUE);
#endif
    GtkWidget* right_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(right_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(right_scroll), text_view);

    /* Barre de recherche vim-like (/), masquee par defaut */
    GtkWidget* search_entry = gtk_search_entry_new();
    gtk_widget_set_no_show_all(search_entry, TRUE);

    /* Terminal panel container: created before AppData so it can hold it.
     * Content: a VteTerminal on Ctrl-T (#ifdef HAVE_VTE), else a notice. */
    GtkWidget* term_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(term_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
#ifndef HAVE_VTE
    GtkWidget* term_stub = gtk_label_new(
        "Terminal panel disabled:\ninstall libvte-2.91-dev then rebuild (make).");
    gtk_widget_set_halign(term_stub, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(term_stub, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(term_scroll), term_stub);
#endif

    /* Signalalement : selection a gauche => contenu a droite */
    AppData* app = g_new0(AppData, 1);
    app->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
#ifdef HAVE_GSV
    apply_vscode_scheme(app->buffer, text_view);
#endif
    app->base_dir = g_strdup(cwd);
    app->model = GTK_TREE_MODEL(store);
    app->tree_view = GTK_TREE_VIEW(tree_view);
    app->text_view = text_view;
    app->search_entry = search_entry;
    app->path_label = path_label;
    update_path_label(app); /* label initial : repertoire + [sort: name] [hidden: off] */

    /* Terminal panel (created just above, wired there) */
    app->term_scroll = term_scroll;
    g_signal_connect(window, "key-press-event",
                     G_CALLBACK(on_window_key_press), app);

    /* Surveillance du repertoire courant des l'ouverture */
    monitor_directory(app);

    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    app->selection = selection;
    g_signal_connect(selection, "changed", G_CALLBACK(on_selection_changed), app);

    /* Keybindings vim sur le treeview : j k gg G / n N */
    g_signal_connect(tree_view, "key-press-event", G_CALLBACK(on_tree_key_press), app);
    /* Double click on a directory: enter it */
    g_signal_connect(tree_view, "row-activated", G_CALLBACK(on_row_activated), app);
    /* Same vim bindings on the viewer (jump + search in the file content) */
    g_signal_connect(text_view, "key-press-event", G_CALLBACK(on_viewer_key_press), app);

    /* Barre de recherche : frappe = recherche live, Entree valide, Echap ferme */
    g_signal_connect(search_entry, "search-changed", G_CALLBACK(on_search_changed), app);
    g_signal_connect(search_entry, "activate", G_CALLBACK(on_search_activate), app);
    g_signal_connect(search_entry, "key-press-event", G_CALLBACK(on_search_key_press), app);

    /* ===== GtkPaned : poignee de redimensionnement entre les 2 panneaux ===== */
    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);

    /* Colonne gauche : barre de recherche (masquee) + liste */
    GtkWidget* left_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(left_vbox), search_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left_vbox), left_scroll, TRUE, TRUE, 0);

    /* Left pane keeps its width when the window is resized: the right side
     * absorbs the difference (resize=FALSE on child1) */
    gtk_paned_pack1(GTK_PANED(paned), left_vbox, FALSE, FALSE);

    /* Secondary splitter: file viewer | terminal panel (Ctrl-T) */
    GtkWidget* right_split = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(right_split), right_scroll, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(right_split), term_scroll, FALSE, FALSE);
    gtk_paned_set_position(GTK_PANED(right_split), 500);
    gtk_widget_set_size_request(term_scroll, 250, -1);
    gtk_widget_set_no_show_all(term_scroll, TRUE); /* hidden until Ctrl-T */

    gtk_paned_pack2(GTK_PANED(paned), right_split, TRUE, FALSE);

    /* Position initiale du séparateur : 300px pour le panneau gauche */
    gtk_paned_set_position(GTK_PANED(paned), 300);

    /* Largeur minimale du panneau gauche, mais redimensionnable au-dela */
    gtk_widget_set_size_request(left_vbox, 150, -1);

    /* Boite verticale englobante : label du chemin + paned */
    GtkWidget* outer_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget* label_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(label_row), path_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(label_row), sep_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(outer_vbox), label_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer_vbox), paned, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(window), outer_vbox);
    gtk_widget_show_all(window);

    if (start_fullscreen) {
        gtk_window_fullscreen(GTK_WINDOW(window));
    }

    gtk_main();

    return 0;
}
