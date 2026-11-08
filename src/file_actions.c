/* Explorer file actions on the files view: new file / new folder (a / Shift+A),
 * rename (F2 / r / e) and move to trash (d / Delete), with their dialogs. */
#include "file_actions.h"
#include "core/i18n.h"
#include "dialog.h"
#include "editor.h"
#include "file_moves.h"

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
                                    _("_Cancel"), APP_RESPONSE_CANCEL,
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
    app_dialog_present(APP_DIALOG(dlg));
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
    app_dialog_destroy(APP_DIALOG(dlg));

    if (out_text != NULL) {
        *out_text = run.result;
    } else {
        g_free(run.result);
    }
    return run.response;
}

static void show_error(GtkWindow* parent, const gchar* title, const gchar* message) {
    GtkWidget* dlg = app_dialog_new(title, parent,
                                    _("_Close"), APP_RESPONSE_CLOSE, NULL);
    GtkWidget* label = gtk_label_new(message);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_set_margin_top(label, 14);
    gtk_widget_set_margin_bottom(label, 14);
    gtk_widget_set_margin_start(label, 16);
    gtk_widget_set_margin_end(label, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), label);
    g_signal_connect(dlg, "response", G_CALLBACK(app_dialog_destroy), NULL);
    app_dialog_present(APP_DIALOG(dlg));
}

/* A plain name: no path separator, not "." or ".." */
static gboolean is_valid_name(const gchar* name) {
    return name != NULL && name[0] != '\0' && strchr(name, '/') == NULL &&
           strchr(name, '\\') == NULL && g_strcmp0(name, ".") != 0 && g_strcmp0(name, "..") != 0;
}

/* Selected file or directory row of the files view (not the base directory);
 * returns its path, or NULL. */
static gchar* selected_entry_path(AppData* app) {
    if (app == NULL || app->tree == NULL || app->sidebar_view != SIDEBAR_VIEW_FILES) {
        return NULL;
    }
    ExplorerRow* row = explorer_tree_get_selected(app->tree);
    gboolean ok = (explorer_row_is(row, "dir") || explorer_row_is(row, "file")) &&
                  row->path[0] != '\0' && g_strcmp0(row->path, app->base_dir) != 0;
    return ok ? g_strdup(row->path) : NULL;
}

static gchar* get_target_files_dir(AppData* app) {
    ExplorerRow* row = (app != NULL && app->tree != NULL) ? explorer_tree_get_selected(app->tree) : NULL;
    if (row == NULL || row->path[0] == '\0') {
        return (app && app->base_dir) ? g_strdup(app->base_dir) : g_strdup(g_get_home_dir());
    }
    if (explorer_row_is(row, "dir") && g_file_test(row->path, G_FILE_TEST_IS_DIR)) {
        return g_strdup(row->path);
    }
    return g_path_get_dirname(row->path);
}

/* "~/..." for paths under the home directory, unchanged otherwise */
static gchar* display_dir(const gchar* path) {
    const gchar* home = g_get_home_dir();
    gsize home_len = (home != NULL) ? strlen(home) : 0;
    if (home_len > 1 && g_str_has_prefix(path, home) &&
        (path[home_len] == '\0' || path[home_len] == '/' || path[home_len] == '\\')) {
        return g_strdup_printf("~%s", path + home_len);
    }
    return g_strdup(path);
}

/* Parent folder entry text to an absolute canonical path ("~" expanded,
 * relative paths against base_dir), or NULL when blank */
static gchar* parse_dir(const gchar* text, const gchar* base_dir) {
    gchar* t = g_strstrip(g_strdup(text != NULL ? text : ""));
    if (t[0] == '\0') {
        g_free(t);
        return NULL;
    }
    gchar* abs = (t[0] == '~' && (t[1] == '\0' || t[1] == '/'))
                     ? g_build_filename(g_get_home_dir(), t + 1, NULL)
                     : g_strdup(t);
    const gchar* rel = (base_dir != NULL && g_path_is_absolute(base_dir)) ? base_dir : NULL;
    gchar* canon = g_canonicalize_filename(abs, rel);
    g_free(abs);
    g_free(t);
    return canon;
}

typedef struct {
    GMainLoop* loop;
    gint response;
    GtkWidget* name_entry;
    GtkWidget* dir_entry;
    const gchar* base_dir;
    gchar* name;   /* trimmed name on OK */
    gchar* dir;    /* canonical parent folder on OK */
} CreateRun;

static void mark_invalid(GtkWidget* entry) {
    gtk_widget_add_css_class(entry, "error");
    gtk_widget_grab_focus(entry);
}

static void on_create_response(AppDialog* dlg, gint response_id, gpointer user_data) {
    (void)dlg;
    CreateRun* run = (CreateRun*)user_data;
    if (response_id == APP_RESPONSE_OK) {
        gchar* name = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(run->name_entry))));
        gchar* dir = parse_dir(gtk_editable_get_text(GTK_EDITABLE(run->dir_entry)), run->base_dir);
        if (!is_valid_name(name)) {
            mark_invalid(run->name_entry);
        } else if (dir == NULL || !g_file_test(dir, G_FILE_TEST_IS_DIR)) {
            mark_invalid(run->dir_entry);
        } else {
            run->name = name;
            run->dir = dir;
            name = NULL;
            dir = NULL;
        }
        g_free(name);
        g_free(dir);
        if (run->name == NULL) {
            return; /* keep the dialog open */
        }
    }
    run->response = response_id;
    if (g_main_loop_is_running(run->loop)) {
        g_main_loop_quit(run->loop);
    }
}

static void on_create_entry_changed(GtkEditable* editable, gpointer data) {
    (void)data;
    gtk_widget_remove_css_class(GTK_WIDGET(editable), "error");
}

static GtkWidget* field_label(const gchar* text) {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "dim-label");
    return label;
}

static GtkWidget* field_entry(const gchar* text, const gchar* placeholder, CreateRun* run) {
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), placeholder);
    gtk_editable_set_text(GTK_EDITABLE(entry), text);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    g_signal_connect(entry, "changed", G_CALLBACK(on_create_entry_changed), run);
    return entry;
}

/* New file / folder dialog: name and parent folder (both editable, like the
 * new bookmark folder dialog). Returns TRUE with *out_name and *out_dir set. */
static gboolean run_create_dialog(AppData* app, gboolean is_folder, const gchar* init_dir,
                                  gchar** out_name, gchar** out_dir) {
    GtkWidget* dlg = app_dialog_new(is_folder ? _("New Folder") : _("New File"), app->window,
                                    NULL, NULL);
    app_dialog_add_button(APP_DIALOG(dlg), _("_Cancel"), APP_RESPONSE_CANCEL);
    GtkWidget* ok = app_dialog_add_button(APP_DIALOG(dlg), _("_Create"), APP_RESPONSE_OK);
    gtk_widget_add_css_class(ok, "suggested-action");
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_OK);
    app_dialog_set_width(APP_DIALOG(dlg), 480);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), box);

    CreateRun run = { g_main_loop_new(NULL, FALSE), APP_RESPONSE_CANCEL, NULL, NULL,
                      app->base_dir, NULL, NULL };

    gtk_box_append(GTK_BOX(box), field_label(_("Name")));
    run.name_entry = field_entry(is_folder ? "folder" : "",
                                 is_folder ? _("Folder name") : _("File name"), &run);
    gtk_box_append(GTK_BOX(box), run.name_entry);

    gtk_box_append(GTK_BOX(box), field_label(_("Parent folder")));
    gchar* shown = display_dir(init_dir);
    run.dir_entry = field_entry(shown, _("~/projects or /tmp"), &run);
    g_free(shown);
    gtk_box_append(GTK_BOX(box), run.dir_entry);

    g_signal_connect(dlg, "response", G_CALLBACK(on_create_response), &run);
    app_dialog_present(APP_DIALOG(dlg));
    gtk_widget_grab_focus(run.name_entry);
    gtk_editable_select_region(GTK_EDITABLE(run.name_entry), 0, -1);
    g_main_loop_run(run.loop);
    g_main_loop_unref(run.loop);
    app_dialog_destroy(APP_DIALOG(dlg));

    if (run.response != APP_RESPONSE_OK) {
        g_free(run.name);
        g_free(run.dir);
        return FALSE;
    }
    *out_name = run.name;
    *out_dir = run.dir;
    return TRUE;
}

gchar* file_action_new_file(AppData* app) {
    if (app == NULL) {
        return NULL;
    }
    gchar* init_dir = get_target_files_dir(app);
    gchar* filename = NULL;
    gchar* target_dir = NULL;
    gboolean ok = run_create_dialog(app, FALSE, init_dir, &filename, &target_dir);
    g_free(init_dir);
    if (!ok) {
        return NULL;
    }
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
    gchar* init_dir = get_target_files_dir(app);
    gchar* folder_name = NULL;
    gchar* target_dir = NULL;
    gboolean ok = run_create_dialog(app, TRUE, init_dir, &folder_name, &target_dir);
    g_free(init_dir);
    if (!ok) {
        return NULL;
    }
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

gchar* file_action_rename_selected(AppData* app) {
    gchar* path = selected_entry_path(app);
    if (path == NULL) {
        return NULL;
    }
    gchar* select_path = NULL;
    gchar* old_name = g_path_get_basename(path);
    gchar* message = g_strdup_printf(_("Rename “%s” to:"), old_name);
    gchar* new_name = NULL;
    gboolean is_dir = g_file_test(path, G_FILE_TEST_IS_DIR);
    gint response = run_dialog(app->window, is_dir ? _("Rename Folder") : _("Rename File"), message,
                               old_name, !is_dir, _("_Rename"), &new_name);
    g_free(message);

    if (response == APP_RESPONSE_OK && new_name != NULL && g_strcmp0(new_name, old_name) != 0) {
        gchar* dir = g_path_get_dirname(path);
        gchar* new_path = g_build_filename(dir, new_name, NULL);
        if (!is_valid_name(new_name)) {
            show_error(app->window, _("Cannot Rename"), _("A name cannot contain “/” or be “.” or “..”."));
        } else if (g_file_test(new_path, G_FILE_TEST_EXISTS)) {
            gchar* msg = g_strdup_printf(_("“%s” already exists in this folder."), new_name);
            show_error(app->window, _("Cannot Rename"), msg);
            g_free(msg);
        } else {
            GError* err = NULL;
            if (file_moves_rename(app, path, new_path, &err)) {
                select_path = g_strdup(new_path);
            } else {
                gchar* msg = g_strdup_printf(_("Could not rename “%s”: %s"), old_name,
                                             err != NULL ? err->message : _("unknown error"));
                show_error(app->window, _("Cannot Rename"), msg);
                g_free(msg);
                g_clear_error(&err);
            }
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
    gchar* message = is_dir ? g_strdup_printf(_("Move the folder “%s” to the trash?"), name)
                             : g_strdup_printf(_("Move the file “%s” to the trash?"), name);
    gint response = run_dialog(app->window, is_dir ? _("Delete Folder") : _("Delete File"), message,
                               NULL, FALSE, _("_Move to Trash"), NULL);
    g_free(message);

    if (response == APP_RESPONSE_OK) {
        GError* err = NULL;
        if (!file_moves_trash(app, path, &err)) {
            gchar* msg = g_strdup_printf(_("Could not move “%s” to the trash: %s"), name,
                                         err != NULL ? err->message : _("unknown error"));
            show_error(app->window, _("Cannot Delete"), msg);
            g_free(msg);
            g_clear_error(&err);
        } else {
            select_path = g_path_get_dirname(path);
        }
    }
    g_free(name);
    g_free(path);
    return select_path;
}
