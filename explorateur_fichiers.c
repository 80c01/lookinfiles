#include <gtk/gtk.h>
#include <string.h>
#include <sys/stat.h>
#include <glib/gstdio.h>

/* Modes de tri cyclés par la touche o */
enum { SORT_NAME, SORT_DATE, SORT_SIZE, SORT_EXT, SORT_COUNT };

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
    GtkTreeSelection* selection;
    GtkWidget* search_entry;
    GtkWidget* path_label;
    gchar* search_term;
    gboolean show_hidden; /* fichiers caches (prefixe .) */
    gint sort_mode; /* SORT_NAME / DATE / SIZE / EXT */
    gboolean pending_g; /* pour la sequence gg */
} AppData;

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

static void show_search(AppData* app) {
    gtk_widget_show(app->search_entry);
    gtk_widget_grab_focus(app->search_entry);
}

static void hide_search(AppData* app) {
    gtk_widget_hide(app->search_entry);
    gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
}

static void on_search_changed(GtkSearchEntry* entry, gpointer data) {
    AppData* app = (AppData*)data;
    const gchar* text = gtk_entry_get_text(GTK_ENTRY(entry));
    g_free(app->search_term);
    app->search_term = g_strdup(text);
    do_search(app, +1);
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

/* Remplit le store avec le contenu reel du repertoire (dossiers d'abord, tries) */
static void populate_store(GtkListStore* store, const gchar* dirname, gboolean show_hidden, gint sort_mode) {
    GtkTreeIter iter;
    gtk_list_store_clear(store);

    GDir* dir = g_dir_open(dirname, 0, NULL);
    if (dir == NULL) {
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, "(impossible de lire le repertoire)", -1);
        gtk_list_store_set(store, &iter, 1, "-", -1);
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
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, e->name, -1);
        gtk_list_store_set(store, &iter, 1, e->is_dir ? "dir" : "file", -1);
    }

    for (guint i = 0; i < entries->len; i++) {
        g_free(g_array_index(entries, FileEntry, i).name);
    }
    g_array_unref(entries);
}

/* Label du haut : repertoire courant + mode de tri, toujours a jour */
static void update_path_label(AppData* app) {
    static const gchar* sort_names[SORT_COUNT] = {"nom", "date", "taille", "ext"};
    gchar* text = g_strdup_printf("Repertoire: %s   [tri: %s]",
                                  app->base_dir, sort_names[app->sort_mode]);
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
        show_search(app);
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
    case GDK_KEY_ISO_Enter: {
        app->pending_g = FALSE;
        GtkTreeIter it;
        if (gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
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
        return TRUE;
    }
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

/* Charge le contenu du fichier selectionne dans le panneau droit */
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
        gchar* msg = g_strdup_printf("[ Repertoire ] %s", name);
        gtk_text_buffer_set_text(app->buffer, msg, -1);
        g_free(msg);
    } else {
        gchar* contents = NULL;
        gsize len = 0;
        if (g_file_get_contents(full, &contents, &len, NULL) && contents != NULL) {
            gtk_text_buffer_set_text(app->buffer, contents, -1);
            g_free(contents);
        } else {
            gchar* msg = g_strdup_printf("[ Impossible de lire : %s ]", name);
            gtk_text_buffer_set_text(app->buffer, msg, -1);
            g_free(msg);
        }
    }

    g_free(full);
    g_free(name);
    g_free(type);
}

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);

    /* Theme sombre natif d'Adwaita */
    GtkSettings* settings = gtk_settings_get_default();
    g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_window_set_title(GTK_WINDOW(window), "Explorateur de Fichiers");
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
    snprintf(path_text, sizeof(path_text), "Repertoire: %s", cwd);
    GtkWidget* path_label = gtk_label_new(path_text);
    gtk_widget_set_halign(path_label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(path_label, 5);

    /* Label pour la colonne de droite */
    GtkWidget* sep_label = gtk_label_new(" ");
    gtk_widget_set_halign(sep_label, GTK_ALIGN_START);

    /* ===== Panneau gauche : liste des fichiers ===== */
    GtkListStore* store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);

    /* Enumerer le contenu reel du repertoire courant */
    populate_store(store, cwd, FALSE, SORT_NAME); /* caches masques, tri par nom */

    /* GtkTreeView avec deux colonnes (Nom / Type) */
    GtkWidget* tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree_view), TRUE);

    GtkTreeViewColumn* col_name = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col_name, "Fichiers");
    gtk_tree_view_column_set_resizable(col_name, TRUE);
    gtk_tree_view_column_set_expand(col_name, TRUE);
    GtkCellRenderer* renderer_name = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col_name, renderer_name, TRUE);
    gtk_tree_view_column_add_attribute(col_name, renderer_name, "text", 0);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col_name);

    GtkTreeViewColumn* col_type = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col_type, "Type");
    GtkCellRenderer* renderer_type = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col_type, renderer_type, TRUE);
    gtk_tree_view_column_add_attribute(col_type, renderer_type, "text", 1);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col_type);

    g_object_unref(store);

    /* Le treeview dans un ScrolledWindow */
    GtkWidget* left_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(left_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(left_scroll), tree_view);

    /* ===== Panneau droit : visualiseur de texte ===== */
    GtkWidget* text_view = gtk_text_view_new();
    GtkWidget* right_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(right_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(right_scroll), text_view);

    /* Barre de recherche vim-like (/), masquee par defaut */
    GtkWidget* search_entry = gtk_search_entry_new();
    gtk_widget_set_no_show_all(search_entry, TRUE);

    /* Signalalement : selection a gauche => contenu a droite */
    AppData* app = g_new0(AppData, 1);
    app->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    app->base_dir = g_strdup(cwd);
    app->model = GTK_TREE_MODEL(store);
    app->tree_view = GTK_TREE_VIEW(tree_view);
    app->search_entry = search_entry;
    app->path_label = path_label;
    update_path_label(app); /* label initial : repertoire + [tri: nom] */

    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    app->selection = selection;
    g_signal_connect(selection, "changed", G_CALLBACK(on_selection_changed), app);

    /* Keybindings vim sur le treeview : j k gg G / n N */
    g_signal_connect(tree_view, "key-press-event", G_CALLBACK(on_tree_key_press), app);

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

    gtk_paned_pack1(GTK_PANED(paned), left_vbox, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), right_scroll, TRUE, FALSE);

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

    gtk_main();

    return 0;
}
