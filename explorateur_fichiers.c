#include <gtk/gtk.h>

/* Donnees partagees entre la selection et le visualiseur */
typedef struct {
    GtkTextBuffer* buffer;
    gchar* base_dir;
} AppData;

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
    GtkTreeIter iter;
    GtkListStore* store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);

    /* Enumerer le contenu reel du repertoire courant */
    GDir* dir = g_dir_open(cwd, 0, NULL);
    if (dir != NULL) {
        /* Collecter les noms pour les trier (dossiers d'abord) */
        GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
        const gchar* name;
        while ((name = g_dir_read_name(dir)) != NULL) {
            g_ptr_array_add(names, g_strdup(name));
        }
        g_dir_close(dir);

        g_ptr_array_sort(names, (GCompareFunc)g_strcmp0);

        /* Premier passage : les repertoires, deuxieme : les fichiers */
        for (int pass = 0; pass < 2; pass++) {
            for (guint i = 0; i < names->len; i++) {
                const gchar* entry = g_ptr_array_index(names, i);
                gchar* full = g_build_filename(cwd, entry, NULL);
                gboolean is_dir = g_file_test(full, G_FILE_TEST_IS_DIR);
                g_free(full);

                if ((pass == 0) != is_dir) {
                    continue;
                }

                gtk_list_store_append(store, &iter);
                gtk_list_store_set(store, &iter, 0, entry, -1);
                gtk_list_store_set(store, &iter, 1, is_dir ? "dir" : "file", -1);
            }
        }

        g_ptr_array_unref(names);
    } else {
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, "(impossible de lire le repertoire)", -1);
        gtk_list_store_set(store, &iter, 1, "-", -1);
    }

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

    /* Signalalement : selection a gauche => contenu a droite */
    AppData* app = g_new0(AppData, 1);
    app->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    app->base_dir = g_strdup(cwd);

    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(selection, "changed", G_CALLBACK(on_selection_changed), app);

    /* ===== GtkPaned : poignee de redimensionnement entre les 2 panneaux ===== */
    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(paned), left_scroll, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), right_scroll, TRUE, FALSE);

    /* Position initiale du séparateur : 300px pour le panneau gauche */
    gtk_paned_set_position(GTK_PANED(paned), 300);

    /* Largeur minimale du panneau gauche, mais redimensionnable au-dela */
    gtk_widget_set_size_request(left_scroll, 150, -1);

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
