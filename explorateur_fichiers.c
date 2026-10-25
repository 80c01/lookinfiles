#include <gtk/gtk.h>

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_window_set_title(GTK_WINDOW(window), "Explorateur de Fichiers");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 600);
    
    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    
    /* Panneau gauche - Liste */
    GtkWidget* left_pane = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_set_border_width(GTK_CONTAINER(left_pane), 0);
    
    GtkListStore* store = gtk_list_store_new(1, G_TYPE_STRING);
    
    {
        GtkTreeIter iter;
        
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, "/", -1);
        
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, "home", -1);
        
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, "Documents", -1);
        
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, "Téléchargements", -1);
        
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, "Images", -1);
        
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, "Musique", -1);
    }
    
    GtkWidget* tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_ref_sink(tree_view);
    
    GtkTreeViewColumn* column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, "Dossiers");
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_expand(column, TRUE);
    
    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_add_attribute(column, renderer, "text", 0);
    
    gtk_container_add(GTK_CONTAINER(left_pane), tree_view);
    gtk_widget_show(tree_view);
    
    g_object_unref(store);
    
    /* Panneau droit - Visualiseur */
    GtkWidget* right_pane = gtk_scrolled_window_new(NULL, NULL);
    
    GtkWidget* text_view = gtk_text_view_new();
    gtk_container_add(GTK_CONTAINER(right_pane), text_view);
    
    /* Pack les panneaux - gauche non-expandable, droit expandable par défaut */
    gtk_box_pack_start(GTK_BOX(main_box), left_pane, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_box), right_pane, TRUE, TRUE, 5);
    
    gtk_container_add(GTK_CONTAINER(window), main_box);
    gtk_widget_show_all(window);
    
    gtk_main();
    
    return 0;
}
