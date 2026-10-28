/* Explorer file actions: new file / new folder prompts and creation in the selected directory. */
#include "file_actions.h"
#include "file_list.h"
#include "editor.h"

typedef struct {
    GMainLoop* loop;
    gchar* result;
    GtkWidget* entry;
} PromptDialogData;

static void on_prompt_dialog_response(GtkDialog* dlg, gint response_id, gpointer user_data) {
    (void)dlg;
    PromptDialogData* data = (PromptDialogData*)user_data;
    if (response_id == GTK_RESPONSE_OK && data->entry != NULL) {
        const gchar* text = gtk_editable_get_text(GTK_EDITABLE(data->entry));
        if (text != NULL) {
            gchar* trimmed = g_strstrip(g_strdup(text));
            if (trimmed[0] != '\0') {
                data->result = trimmed;
            } else {
                g_free(trimmed);
            }
        }
    }
    if (g_main_loop_is_running(data->loop)) {
        g_main_loop_quit(data->loop);
    }
}

static gchar* prompt_new_folder_dialog(GtkWindow* parent_win) {
    GtkWidget* dlg = gtk_dialog_new_with_buttons("New Folder", parent_win,
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 "Cancel", GTK_RESPONSE_CANCEL,
                                                 "Create", GTK_RESPONSE_OK,
                                                 NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);

    GtkWidget* label = gtk_label_new("Enter folder name:");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget* entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), "folder");
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_append(GTK_BOX(box), entry);
    gtk_box_append(GTK_BOX(content), box);

    PromptDialogData d = { g_main_loop_new(NULL, FALSE), NULL, entry };
    g_signal_connect(dlg, "response", G_CALLBACK(on_prompt_dialog_response), &d);

    gtk_window_present(GTK_WINDOW(dlg));
    gtk_widget_grab_focus(entry);
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    g_main_loop_run(d.loop);
    g_main_loop_unref(d.loop);
    gtk_window_destroy(GTK_WINDOW(dlg));

    return d.result;
}

static gchar* get_target_files_dir(AppData* app) {
    if (app == NULL || app->tree_view == NULL || app->selection == NULL) {
        return (app && app->base_dir) ? g_strdup(app->base_dir) : g_strdup(g_get_home_dir());
    }
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        return (app->base_dir) ? g_strdup(app->base_dir) : g_strdup(g_get_home_dir());
    }
    gchar* path = NULL;
    gchar* type = NULL;
    gtk_tree_model_get(app->model, &it, COL_PATH, &path, COL_TYPE, &type, -1);
    if (path == NULL || path[0] == '\0') {
        g_free(type);
        g_free(path);
        return (app->base_dir) ? g_strdup(app->base_dir) : g_strdup(g_get_home_dir());
    }
    gchar* target = NULL;
    if (g_strcmp0(type, "dir") == 0 && g_file_test(path, G_FILE_TEST_IS_DIR)) {
        target = g_strdup(path);
    } else {
        target = g_path_get_dirname(path);
    }
    g_free(type);
    g_free(path);
    return target;
}

static gchar* prompt_new_file_dialog(GtkWindow* parent_win) {
    GtkWidget* dlg = gtk_dialog_new_with_buttons("New File", parent_win,
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 "Cancel", GTK_RESPONSE_CANCEL,
                                                 "Create", GTK_RESPONSE_OK,
                                                 NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);

    GtkWidget* label = gtk_label_new("Enter file name:");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_append(GTK_BOX(box), entry);
    gtk_box_append(GTK_BOX(content), box);

    PromptDialogData d = { g_main_loop_new(NULL, FALSE), NULL, entry };
    g_signal_connect(dlg, "response", G_CALLBACK(on_prompt_dialog_response), &d);

    gtk_window_present(GTK_WINDOW(dlg));
    gtk_widget_grab_focus(entry);
    g_main_loop_run(d.loop);
    g_main_loop_unref(d.loop);
    gtk_window_destroy(GTK_WINDOW(dlg));

    return d.result;
}

void create_new_file_in_tree(AppData* app) {
    if (app == NULL) {
        return;
    }
    gchar* target_dir = get_target_files_dir(app);
    gchar* filename = prompt_new_file_dialog(app->window);
    if (filename == NULL) {
        g_free(target_dir);
        return;
    }

    if (strchr(filename, '/') != NULL || strchr(filename, '\\') != NULL ||
        g_strcmp0(filename, ".") == 0 || g_strcmp0(filename, "..") == 0) {
        g_free(filename);
        g_free(target_dir);
        return;
    }

    gchar* full_path = g_build_filename(target_dir, filename, NULL);
    if (!g_file_test(full_path, G_FILE_TEST_EXISTS)) {
        g_file_set_contents(full_path, "", 0, NULL);
    }

    if (app->expanded_paths != NULL && g_strcmp0(target_dir, app->base_dir) != 0) {
        g_hash_table_add(app->expanded_paths, g_strdup(target_dir));
    }

    editor_open_file(app, filename, full_path);
    refresh_listing_with_path(app, full_path);

    g_free(filename);
    g_free(full_path);
    g_free(target_dir);
}

void create_new_folder_in_tree(AppData* app) {
    if (app == NULL) {
        return;
    }
    gchar* target_dir = get_target_files_dir(app);
    gchar* folder_name = prompt_new_folder_dialog(app->window);
    if (folder_name == NULL) {
        g_free(target_dir);
        return;
    }

    if (strchr(folder_name, '/') != NULL || strchr(folder_name, '\\') != NULL ||
        g_strcmp0(folder_name, ".") == 0 || g_strcmp0(folder_name, "..") == 0) {
        g_free(folder_name);
        g_free(target_dir);
        return;
    }

    gchar* new_path = g_build_filename(target_dir, folder_name, NULL);
    g_mkdir_with_parents(new_path, 0755);

    if (app->expanded_paths != NULL) {
        if (g_strcmp0(target_dir, app->base_dir) != 0) {
            g_hash_table_add(app->expanded_paths, g_strdup(target_dir));
        }
        g_hash_table_add(app->expanded_paths, g_strdup(new_path));
    }

    refresh_listing_with_path(app, new_path);

    g_free(folder_name);
    g_free(new_path);
    g_free(target_dir);
}
