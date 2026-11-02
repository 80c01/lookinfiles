/* Workspace bookmarks (see session_bookmark.h) */
#include "session_bookmark.h"
#include "bookmark_dialog.h"
#include "bookmarks.h"
#include "dialog.h"
#include "session.h"
#include "session_store.h"

/* Name of the bookmark to create; NULL when cancelled (caller frees) */
static gchar* ask_name(AppData* app, const gchar* initial) {
    GtkWidget* dlg = app_dialog_new("Save Workspace as Bookmark", app->window, NULL, NULL);
    app_dialog_add_button(APP_DIALOG(dlg), "_Cancel", APP_RESPONSE_CANCEL);
    GtkWidget* ok = app_dialog_add_button(APP_DIALOG(dlg), "_Save", APP_RESPONSE_OK);
    gtk_widget_add_css_class(ok, "suggested-action");
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_OK);
    app_dialog_set_width(APP_DIALOG(dlg), 480);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), box);

    GtkWidget* label = gtk_label_new("Name");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "dim-label");
    gtk_box_append(GTK_BOX(box), label);
    GtkWidget* entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), initial);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_append(GTK_BOX(box), entry);
    GtkWidget* hint = gtk_label_new("Keeps this workspace's folder, open editor tabs and panels. "
                                    "Opening the bookmark restores them in a new workspace.");
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(hint), 60);
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_widget_add_css_class(hint, "dim-label");
    gtk_box_append(GTK_BOX(box), hint);

    gchar* name = NULL;
    if (app_dialog_run(APP_DIALOG(dlg), entry) == APP_RESPONSE_OK) {
        name = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry))));
        if (name[0] == '\0') {
            g_free(name);
            name = g_strdup(initial);
        }
    }
    app_dialog_destroy(APP_DIALOG(dlg));
    return name;
}

void session_bookmark_save(AppData* app) {
    if (app == NULL || app->bookmarks == NULL) {
        return;
    }
    if (app->base_dir == NULL || app->base_dir[0] == '\0') {
        bookmark_message(app, "Save Workspace", "This workspace has no folder to save.", "_Close", NULL);
        return;
    }
    gchar* initial = g_path_get_basename(app->base_dir);
    gchar* name = ask_name(app, initial);
    g_free(initial);
    if (name == NULL) {
        return;
    }

    gchar* id = session_snapshot_new_id();
    gchar* path = session_snapshot_path(id);
    gchar* target = session_target_make(id);
    GError* err = NULL;
    if (path != NULL && target != NULL && session_save_workspace(app, app->current_ws, path, &err)) {
        bookmarks_add_session(app, name, target);
    } else {
        gchar* msg = g_strdup_printf("Could not save the workspace:\n%s", err != NULL ? err->message : "unknown error");
        bookmark_message(app, "Save Workspace", msg, "_Close", NULL);
        g_free(msg);
    }
    g_clear_error(&err);
    g_free(target);
    g_free(path);
    g_free(id);
    g_free(name);
}

void session_bookmark_open(AppData* app) {
    if (app == NULL) {
        return;
    }
    gchar* target = app->session_open_target;
    app->session_open_target = NULL;
    gchar* path = session_snapshot_path(session_target_id(target));
    GError* err = NULL;
    if (path == NULL || !session_open_workspace(app, path, &err)) {
        gchar* msg = g_strdup_printf("Could not open this workspace:\n%s", err != NULL ? err->message : "unknown error");
        bookmark_message(app, "Open Workspace", msg, "_Close", NULL);
        g_free(msg);
    }
    g_clear_error(&err);
    g_free(path);
    g_free(target);
}
