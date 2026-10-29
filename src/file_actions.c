/* Explorer file actions on the files view: new file / new folder (a / Shift+A),
 * rename (F2 / r / e) and move to trash (d / Delete), with their dialogs. */
#include "file_actions.h"
#include "dialog.h"
#include "editor.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <string.h>

typedef struct {
    GMainLoop* loop;
    gint response;
    gchar* result;
    GtkWidget* entry;
} DialogRun;

static void on_dialog_response(AppDialog* dlg, gint response_id, gpointer user_data) {
    (void)dlg;
    DialogRun* run = (DialogRun*)user_data;
    run->response = response_id;
    if (response_id == APP_RESPONSE_OK && run->entry != NULL) {
        gchar* trimmed = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(run->entry))));
        if (trimmed[0] != '\0') {
            run->result = trimmed;
        } else {
            g_free(trimmed);
        }
    }
    if (g_main_loop_is_running(run->loop)) {
        g_main_loop_quit(run->loop);
    }
}

/* Modal dialog with a message and, when entry_text is not NULL, a text entry.
 * Returns the GTK response; *out_text receives the trimmed entry text on OK. */
static gint run_dialog(GtkWindow* parent, const gchar* title, const gchar* message,
                       const gchar* entry_text, gboolean select_stem,
                       const gchar* ok_label, gchar** out_text) {
    GtkWidget* dlg = app_dialog_new(title, parent,
                                    "_Cancel", APP_RESPONSE_CANCEL,
                                    ok_label, APP_RESPONSE_OK,
                                    NULL);
    /* Confirmations (no entry) default to Cancel, like the other delete dialogs */
    app_dialog_set_default_response(APP_DIALOG(dlg),
                                    entry_text != NULL ? APP_RESPONSE_OK : APP_RESPONSE_CANCEL);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);

    GtkWidget* label = gtk_label_new(message);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget* entry = NULL;
    if (entry_text != NULL) {
        entry = gtk_entry_new();
        gtk_editable_set_text(GTK_EDITABLE(entry), entry_text);
        gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
        gtk_box_append(GTK_BOX(box), entry);
    }
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), box);

    DialogRun run = { g_main_loop_new(NULL, FALSE), APP_RESPONSE_CANCEL, NULL, entry };
    g_signal_connect(dlg, "response", G_CALLBACK(on_dialog_response), &run);
    gtk_window_present(GTK_WINDOW(dlg));
    if (entry != NULL) {
        gtk_widget_grab_focus(entry);
        /* Select the name without its extension, like file managers do */
        const gchar* dot = select_stem ? strrchr(entry_text, '.') : NULL;
        gint end = (dot != NULL && dot != entry_text)
                       ? (gint)g_utf8_pointer_to_offset(entry_text, dot) : -1;
        gtk_editable_select_region(GTK_EDITABLE(entry), 0, end);
    } else {
        gtk_widget_grab_focus(app_dialog_get_widget_for_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL));
    }
    g_main_loop_run(run.loop);
    g_main_loop_unref(run.loop);
    gtk_window_destroy(GTK_WINDOW(dlg));

    if (out_text != NULL) {
        *out_text = run.result;
    } else {
        g_free(run.result);
    }
    return run.response;
}

static void show_error(GtkWindow* parent, const gchar* title, const gchar* message) {
    GtkWidget* dlg = app_dialog_new(title, parent,
                                    "_Close", APP_RESPONSE_CLOSE, NULL);
    GtkWidget* label = gtk_label_new(message);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_set_margin_top(label, 14);
    gtk_widget_set_margin_bottom(label, 14);
    gtk_widget_set_margin_start(label, 16);
    gtk_widget_set_margin_end(label, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), label);
    g_signal_connect(dlg, "response", G_CALLBACK(gtk_window_destroy), NULL);
    gtk_window_present(GTK_WINDOW(dlg));
}

/* A plain name: no path separator, not "." or ".." */
static gboolean is_valid_name(const gchar* name) {
    return name != NULL && name[0] != '\0' && strchr(name, '/') == NULL &&
           strchr(name, '\\') == NULL && g_strcmp0(name, ".") != 0 && g_strcmp0(name, "..") != 0;
}

/* Selected file or directory row of the files view (not the base directory);
 * returns its path, or NULL. */
static gchar* selected_entry_path(AppData* app) {
    if (app == NULL || app->selection == NULL || app->sidebar_view != SIDEBAR_VIEW_FILES ||
        app->show_bookmarks) {
        return NULL;
    }
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
        return NULL;
    }
    gchar* type = NULL;
    gchar* path = NULL;
    gtk_tree_model_get(app->model, &it, COL_TYPE, &type, COL_PATH, &path, -1);
    gboolean ok = (g_strcmp0(type, "dir") == 0 || g_strcmp0(type, "file") == 0) &&
                  path != NULL && path[0] != '\0' && g_strcmp0(path, app->base_dir) != 0;
    g_free(type);
    if (!ok) {
        g_free(path);
        return NULL;
    }
    return path;
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

gchar* file_action_new_file(AppData* app) {
    if (app == NULL) {
        return NULL;
    }
    gchar* filename = NULL;
    if (run_dialog(app->window, "New File", "Enter file name:", "", FALSE, "_Create", &filename) !=
            APP_RESPONSE_OK || !is_valid_name(filename)) {
        g_free(filename);
        return NULL;
    }
    gchar* target_dir = get_target_files_dir(app);
    gchar* full_path = g_build_filename(target_dir, filename, NULL);
    if (!g_file_test(full_path, G_FILE_TEST_EXISTS)) {
        g_file_set_contents(full_path, "", 0, NULL);
    }

    if (app->expanded_paths != NULL && g_strcmp0(target_dir, app->base_dir) != 0) {
        g_hash_table_add(app->expanded_paths, g_strdup(target_dir));
    }

    editor_open_file(app, filename, full_path);

    g_free(filename);
    g_free(target_dir);
    return full_path;
}

gchar* file_action_new_folder(AppData* app) {
    if (app == NULL) {
        return NULL;
    }
    gchar* folder_name = NULL;
    if (run_dialog(app->window, "New Folder", "Enter folder name:", "folder", FALSE, "_Create",
                   &folder_name) != APP_RESPONSE_OK || !is_valid_name(folder_name)) {
        g_free(folder_name);
        return NULL;
    }
    gchar* target_dir = get_target_files_dir(app);
    gchar* new_path = g_build_filename(target_dir, folder_name, NULL);
    g_mkdir_with_parents(new_path, 0755);

    if (app->expanded_paths != NULL) {
        if (g_strcmp0(target_dir, app->base_dir) != 0) {
            g_hash_table_add(app->expanded_paths, g_strdup(target_dir));
        }
        g_hash_table_add(app->expanded_paths, g_strdup(new_path));
    }

    g_free(folder_name);
    g_free(target_dir);
    return new_path;
}

/* old_path became new_path: retarget editor tabs on it (or inside it) */
static void retarget_editor_tabs(AppData* app, const gchar* old_path, const gchar* new_path) {
    gsize old_len = strlen(old_path);
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* tab = (EditorTab*)l->data;
        if (tab == NULL || tab->path == NULL) {
            continue;
        }
        gboolean same = (g_strcmp0(tab->path, old_path) == 0);
        gboolean inside = g_str_has_prefix(tab->path, old_path) && tab->path[old_len] == '/';
        if (!same && !inside) {
            continue;
        }
        gchar* moved = g_strconcat(new_path, tab->path + old_len, NULL);
        g_free(tab->path);
        tab->path = moved;
        g_free(tab->name);
        tab->name = g_path_get_basename(moved);
        editor_tab_label_update(tab);
    }
}

gchar* file_action_rename_selected(AppData* app) {
    gchar* path = selected_entry_path(app);
    if (path == NULL) {
        return NULL;
    }
    gchar* select_path = NULL;
    gchar* old_name = g_path_get_basename(path);
    gchar* message = g_strdup_printf("Rename “%s” to:", old_name);
    gchar* new_name = NULL;
    gboolean is_dir = g_file_test(path, G_FILE_TEST_IS_DIR);
    gint response = run_dialog(app->window, is_dir ? "Rename Folder" : "Rename File", message,
                               old_name, !is_dir, "_Rename", &new_name);
    g_free(message);

    if (response == APP_RESPONSE_OK && new_name != NULL && g_strcmp0(new_name, old_name) != 0) {
        gchar* dir = g_path_get_dirname(path);
        gchar* new_path = g_build_filename(dir, new_name, NULL);
        if (!is_valid_name(new_name)) {
            show_error(app->window, "Cannot Rename", "A name cannot contain “/” or be “.” or “..”.");
        } else if (g_file_test(new_path, G_FILE_TEST_EXISTS)) {
            gchar* msg = g_strdup_printf("“%s” already exists in this folder.", new_name);
            show_error(app->window, "Cannot Rename", msg);
            g_free(msg);
        } else if (g_rename(path, new_path) != 0) {
            gchar* msg = g_strdup_printf("Could not rename “%s”: %s", old_name, g_strerror(errno));
            show_error(app->window, "Cannot Rename", msg);
            g_free(msg);
        } else {
            retarget_editor_tabs(app, path, new_path);
            if (is_dir && app->expanded_paths != NULL &&
                g_hash_table_contains(app->expanded_paths, path)) {
                g_hash_table_add(app->expanded_paths, g_strdup(new_path));
            }
            select_path = g_strdup(new_path);
        }
        g_free(new_path);
        g_free(dir);
    }
    g_free(new_name);
    g_free(old_name);
    g_free(path);
    return select_path;
}

gchar* file_action_trash_selected(AppData* app) {
    gchar* path = selected_entry_path(app);
    if (path == NULL) {
        return NULL;
    }
    gchar* select_path = NULL;
    gchar* name = g_path_get_basename(path);
    gboolean is_dir = g_file_test(path, G_FILE_TEST_IS_DIR);
    gchar* message = g_strdup_printf("Move %s “%s” to the trash?", is_dir ? "the folder" : "the file", name);
    gint response = run_dialog(app->window, is_dir ? "Delete Folder" : "Delete File", message,
                               NULL, FALSE, "_Move to Trash", NULL);
    g_free(message);

    if (response == APP_RESPONSE_OK) {
        GFile* file = g_file_new_for_path(path);
        GError* err = NULL;
        if (!g_file_trash(file, NULL, &err)) {
            gchar* msg = g_strdup_printf("Could not move “%s” to the trash: %s", name,
                                         err != NULL ? err->message : "unknown error");
            show_error(app->window, "Cannot Delete", msg);
            g_free(msg);
            g_clear_error(&err);
        } else {
            select_path = g_path_get_dirname(path);
        }
        g_object_unref(file);
    }
    g_free(name);
    g_free(path);
    return select_path;
}
