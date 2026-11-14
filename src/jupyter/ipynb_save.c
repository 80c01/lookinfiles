/* Saving the Jupyter notebook tabs (see ipynb_tab.h): written to their file
 * shortly after each change; a notebook made in memory (no file yet) is
 * written once a file name is chosen (Ctrl+S, or when it is closed) */
#include "jupyter/ipynb_tab_private.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "editor/editor.h"
#include "editor/file_save.h"
#include "widgets/dialog.h"

#include <string.h>

#define SAVE_DELAY_MS 1000

void ipynb_tab_save_now(IpynbTab* t) {
    if (t->save_source != 0) {
        g_source_remove(t->save_source);
        t->save_source = 0;
    }
    if (!t->dirty) return;
    if (t->path == NULL) {
        if (!t->closed) ipynb_tab_set_status(t, _("Not saved yet (Ctrl+S to choose a file)"), FALSE);
        return;
    }
    gchar* json = ipynb_doc_to_json(t->doc);
    GError* error = NULL;
    if (file_save_text(t->path, json, -1, &error)) {
        t->dirty = FALSE;
        if (!t->closed) ipynb_tab_set_status(t, _("Saved"), FALSE);
    } else {
        gchar* msg = g_strdup_printf(_("Not saved: %s"), error->message);
        if (!t->closed) ipynb_tab_set_status(t, msg, TRUE);
        g_warning("%s: %s", t->path, msg);
        g_free(msg);
        g_error_free(error);
    }
    g_free(json);
}

static gboolean on_save_timeout(gpointer data) {
    IpynbTab* t = data;
    t->save_source = 0;
    ipynb_tab_save_now(t);
    return G_SOURCE_REMOVE;
}

void ipynb_tab_mark_changed(IpynbTab* t) {
    t->dirty = TRUE;
    ipynb_tab_set_status(t, _("Modified"), FALSE);
    if (t->save_source != 0) g_source_remove(t->save_source);
    t->save_source = g_timeout_add(SAVE_DELAY_MS, on_save_timeout, t);
    ipynb_tagbar_changed(t);
}

/* --- A notebook without a file --- */

static GtkWidget* dialog_label(GtkWidget* box, const gchar* text) {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 60);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_append(GTK_BOX(box), label);
    return label;
}

static GtkWidget* dialog_body(GtkWidget* dlg) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), box);
    return box;
}

/* The file typed for the notebook: ~ and a relative name from its
 * directory, .ipynb added when missing; NULL when empty */
static gchar* typed_path(IpynbTab* t, const gchar* text) {
    gchar* name = g_strstrip(g_strdup(text));
    if (name[0] == '\0') {
        g_free(name);
        return NULL;
    }
    gchar* full = name[0] == '~' && (name[1] == '\0' || name[1] == '/') ? g_build_filename(g_get_home_dir(), name + 1, NULL)
                  : g_path_is_absolute(name)                          ? g_strdup(name)
                                                                      : g_build_filename(t->dir, name, NULL);
    g_free(name);
    gchar* lower = g_ascii_strdown(full, -1);
    if (!g_str_has_suffix(lower, ".ipynb")) {
        gchar* with_ext = g_strconcat(full, ".ipynb", NULL);
        g_free(full);
        full = with_ext;
    }
    g_free(lower);
    return full;
}

/* Replace the file at path? (Enter: no) */
static gboolean confirm_replace(IpynbTab* t, const gchar* path) {
    GtkWidget* dlg = app_dialog_new(_("Replace File"), t->app->window, _("_Cancel"), APP_RESPONSE_CANCEL,
                                    _("_Replace"), APP_RESPONSE_OK, NULL);
    gchar* message = g_strdup_printf(_("%s already exists. Replace it?"), path);
    dialog_label(dialog_body(dlg), message);
    g_free(message);
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL);
    gint response = app_dialog_run(APP_DIALOG(dlg),
                                   app_dialog_get_widget_for_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL));
    app_dialog_destroy(APP_DIALOG(dlg));
    return response == APP_RESPONSE_OK;
}

/* The tab edits the file at path from now on */
static void set_path(IpynbTab* t, const gchar* path) {
    g_free(t->path);
    t->path = g_strdup(path);
    g_free(t->name);
    t->name = g_path_get_basename(path);
    g_free(t->dir);
    t->dir = g_path_get_dirname(path);
    gtk_label_set_text(GTK_LABEL(t->label_text), t->name);
    gtk_label_set_width_chars(GTK_LABEL(t->label_text), (gint)MIN(g_utf8_strlen(t->name, -1), IPYNB_TAB_MAX_CHARS));
    gtk_widget_set_tooltip_text(t->tab_label, t->path);
    editor_page_set_file(t->root, t->path);
}

gboolean ipynb_save_as(IpynbTab* t) {
    if (t == NULL || t->closed) return FALSE;
    gchar* text = g_build_filename(t->dir, t->name, NULL);
    gchar* problem = NULL;
    gboolean saved = FALSE;
    for (;;) {
        GtkWidget* dlg = app_dialog_new(_("Save Notebook"), t->app->window, _("_Cancel"), APP_RESPONSE_CANCEL,
                                        _("_Save"), APP_RESPONSE_OK, NULL);
        app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_OK);
        GtkWidget* box = dialog_body(dlg);
        dialog_label(box, _("File name (from the notebook's folder):"));
        GtkWidget* entry = gtk_entry_new();
        gtk_editable_set_text(GTK_EDITABLE(entry), text);
        gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
        gtk_widget_set_size_request(entry, 360, -1);
        gtk_box_append(GTK_BOX(box), entry);
        if (problem != NULL) gtk_widget_add_css_class(dialog_label(box, problem), "error");
        gint response = app_dialog_run(APP_DIALOG(dlg), entry);
        g_free(text);
        text = g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry)));
        app_dialog_destroy(APP_DIALOG(dlg));
        g_clear_pointer(&problem, g_free);
        if (response != APP_RESPONSE_OK || t->closed) break;

        gchar* path = typed_path(t, text);
        IpynbTab* open = path != NULL ? ipynb_tab_find(t->app, path) : NULL;
        if (path == NULL) {
            problem = g_strdup(_("Type a file name."));
        } else if (open != NULL && open != t) {
            problem = g_strdup_printf(_("%s is open in another tab."), path);
        } else if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
            problem = g_strdup_printf(_("%s is a folder."), path);
        } else if (!g_file_test(path, G_FILE_TEST_EXISTS) || confirm_replace(t, path)) {
            gchar* json = ipynb_doc_to_json(t->doc);
            GError* error = NULL;
            if (file_save_text(path, json, -1, &error)) {
                set_path(t, path);
                t->dirty = FALSE;
                ipynb_tab_set_status(t, _("Saved"), FALSE);
                app_emit(t->app, APP_EVENT_FILES_CHANGED);
                saved = TRUE;
            } else {
                problem = g_strdup_printf(_("Not saved: %s"), error->message);
                g_error_free(error);
            }
            g_free(json);
        }
        g_free(path);
        if (saved) break;
        if (problem == NULL) continue; /* not replaced: asked again */
    }
    g_free(text);
    g_free(problem);
    return saved;
}

gboolean ipynb_save_may_close(IpynbTab* t) {
    if (t == NULL || t->closed || t->path != NULL || !t->dirty) return TRUE;
    GtkWidget* dlg = app_dialog_new(_("Unsaved Notebook"), t->app->window, _("_Cancel"), APP_RESPONSE_CANCEL,
                                    _("Close _Without Saving"), APP_RESPONSE_NO, _("_Save…"), APP_RESPONSE_OK, NULL);
    gchar* message = g_strdup_printf(_("%s has never been saved. Save it before closing?"), t->name);
    dialog_label(dialog_body(dlg), message);
    g_free(message);
    /* Destructive choice: Enter keeps the notebook */
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL);
    gint response = app_dialog_run(APP_DIALOG(dlg),
                                   app_dialog_get_widget_for_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL));
    app_dialog_destroy(APP_DIALOG(dlg));
    if (response == APP_RESPONSE_OK) return ipynb_save_as(t);
    if (response != APP_RESPONSE_NO) return FALSE;
    t->dirty = FALSE; /* given up */
    return TRUE;
}

gboolean ipynb_tab_confirm_quit(AppData* app) {
    for (guint i = 0; app != NULL && app->ipynb_pages != NULL && i < app->ipynb_pages->len; i++) {
        if (!ipynb_save_may_close(ipynb_tab_of(g_ptr_array_index(app->ipynb_pages, i)))) return FALSE;
    }
    return TRUE;
}
