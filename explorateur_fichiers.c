#include <gtk/gtk.h>

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_window_set_title(GTK_WINDOW(window), "Explorateur de Fichiers");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 600);
    
    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    
    /* Get current directory */
    char cwd_buf[2048];
    const char* cwd = (getcwd(cwd_buf, sizeof(cwd_buf)) != NULL) ? cwd_buf : "/workspace";
    
    /* Panneau gauche - Liste des fichiers du repertoire actuel */
    GtkWidget* left_pane = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_set_border_width(GTK_CONTAINER(left_pane), 5);
    
    /* Creer un GtkBox vertical pour contenir le label et le treeview */
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(left_pane), vbox);
    
    /* Label pour montrer le path actuel */
    char path_text[256];
    snprintf(path_text, sizeof(path_text), "Repertoire: %s", cwd);
    GtkWidget* path_label = gtk_label_new(path_text);
    gtk_widget_set_halign(path_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), path_label, FALSE, FALSE, 0);
    
    /* Creer le GtkListStore */
    GtkTreeIter iter;
    GtkListStore* store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    
    /* En-tête */
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter, 0, "Nom", -1);
    gtk_list_store_set(store, &iter, 1, "Type", -1);
    
    /* Liste descriptive statique */
    const char* folders[] = {"/", "/home", "/tmp", "/etc", NULL};
    static const gchar* folder_types[] = {"root", "folder", "system", "system", NULL};
    
    for (int i = 0; folders[i] != NULL; i++) {
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, folders[i], -1);
        gtk_list_store_set(store, &iter, 1, folder_types[i], -1);
    }
    
    /* Ajout de dossiers du repertoire actuel si on est dans /workspace */
    if (strncmp(cwd, "/workspace", 10) == 0) {
        const char* project_files[] = {"explorateur_fichiers", "explorateur_fichiers.c", "Makefile", "README.md", NULL};
        
        for (int i = 0; project_files[i] != NULL; i++) {
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, project_files[i], -1);
            gtk_list_store_set(store, &iter, 1, "file", -1);
        }
    }
    
    /* Creer un GtkTreeView pour afficher les fichiers */
    GtkWidget* tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_ref_sink(tree_view);
    
    /* Ajouter une colonne pour afficher les noms */
    GtkTreeViewColumn* column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, "Fichiers");
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_expand(column, TRUE);
    
    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_add_attribute(column, renderer, "text", 0);
    
    /* Ajouter le treeview au vbox */
    gtk_box_pack_start(GTK_BOX(vbox), tree_view, TRUE, TRUE, 0);
    
    /* Afficher les widgets */
    gtk_widget_show_all(vbox);
    
    g_object_unref(store);
    
    /* Panneau droit */
    GtkWidget* right_pane = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_set_border_width(GTK_CONTAINER(right_pane), 0);
    
    GtkWidget* text_view = gtk_text_view_new();
    gtk_container_add(GTK_CONTAINER(right_pane), text_view);
    
    gtk_box_pack_start(GTK_BOX(main_box), left_pane, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_box), right_pane, TRUE, TRUE, 5);
    
    gtk_container_add(GTK_CONTAINER(window), main_box);
    gtk_widget_show_all(window);
    
    gtk_main();
    
    return 0;
}
