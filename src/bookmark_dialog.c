/* Bookmark dialogs (see bookmark_dialog.h) */
#include "bookmark_dialog.h"
#include "bookmark_icons.h"
#include "dialog.h"
#include "icon_picker.h"
#include "notebook_icons.h"
#include "session_store.h"

#include <string.h>

/* "~/..." for paths under the home directory, unchanged otherwise */
gchar* bookmark_display_location(const gchar* target) {
    if (target == NULL) {
        return g_strdup("");
    }
    if (session_target_id(target) != NULL) {
        return g_strdup("Saved workspace");
    }
    if (bookmark_target_is_uri(target)) {
        return g_strdup(target);
    }
    const gchar* home = g_get_home_dir();
    gsize home_len = (home != NULL) ? strlen(home) : 0;
    if (home_len > 1 && g_str_has_prefix(target, home) &&
        (target[home_len] == '\0' || target[home_len] == '/' || target[home_len] == '\\')) {
        return g_strdup_printf("~%s", target + home_len);
    }
    return g_strdup(target);
}

/* Turn user input into a canonical absolute path, or keep it as a URI */
gchar* bookmark_normalize_target(const gchar* text, const gchar* base_dir) {
    gchar* t = g_strstrip(g_strdup(text != NULL ? text : ""));
    if (t[0] == '\0') {
        g_free(t);
        return NULL;
    }
    if (bookmark_target_is_uri(t)) {
        if (g_ascii_strcasecmp(g_uri_peek_scheme(t), "file") != 0) {
            return t;
        }
        gchar* local = g_filename_from_uri(t, NULL, NULL);
        if (local == NULL) {
            return t;
        }
        g_free(t);
        t = local;
    }
    gchar* abs = NULL;
    if (t[0] == '~' && (t[1] == '\0' || t[1] == '/')) {
        abs = g_build_filename(g_get_home_dir(), t + 1, NULL);
    } else {
        abs = g_strdup(t);
    }
    const gchar* rel = (base_dir != NULL && g_path_is_absolute(base_dir)) ? base_dir : NULL;
    gchar* canon = g_canonicalize_filename(abs, rel);
    g_free(abs);
    g_free(t);
    return canon;
}

gchar* bookmark_default_name(const gchar* target) {
    if (target == NULL || target[0] == '\0') {
        return g_strdup("");
    }
    if (bookmark_target_is_uri(target)) {
        GUri* uri = g_uri_parse(target, G_URI_FLAGS_NONE, NULL);
        gchar* name = NULL;
        if (uri != NULL) {
            const gchar* host = g_uri_get_host(uri);
            if (host != NULL && host[0] != '\0') {
                name = g_strdup(host);
            }
            g_uri_unref(uri);
        }
        return (name != NULL) ? name : g_strdup(target);
    }
    return g_path_get_basename(target);
}

typedef struct {
    GMainLoop* loop;
    gint response;
    GtkWidget* name_entry;   /* required when validating a folder */
    GtkWidget* target_entry; /* required when validating a link */
} BmDialog;

static gboolean bm_entry_is_blank(GtkWidget* entry) {
    const gchar* text = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (text == NULL) {
        return TRUE;
    }
    for (const gchar* p = text; *p != '\0'; p++) {
        if (!g_ascii_isspace(*p)) {
            return FALSE;
        }
    }
    return TRUE;
}

static void bm_on_dialog_response(AppDialog* dlg, gint response_id, gpointer user_data) {
    (void)dlg;
    BmDialog* d = (BmDialog*)user_data;
    if (response_id == APP_RESPONSE_OK) {
        GtkWidget* required = (d->target_entry != NULL) ? d->target_entry : d->name_entry;
        if (required != NULL && bm_entry_is_blank(required)) {
            gtk_widget_add_css_class(required, "error");
            gtk_widget_grab_focus(required);
            return; /* keep the dialog open */
        }
    }
    d->response = response_id;
    if (g_main_loop_is_running(d->loop)) {
        g_main_loop_quit(d->loop);
    }
}

static gint bm_run_modal(GtkWidget* dlg, BmDialog* d, GtkWidget* focus) {
    d->loop = g_main_loop_new(NULL, FALSE);
    d->response = APP_RESPONSE_CANCEL;
    g_signal_connect(dlg, "response", G_CALLBACK(bm_on_dialog_response), d);
    app_dialog_present(APP_DIALOG(dlg));
    if (focus != NULL) {
        gtk_widget_grab_focus(focus);
    }
    g_main_loop_run(d->loop);
    g_main_loop_unref(d->loop);
    return d->response;
}

static GtkWidget* bm_dialog_body(GtkWidget* dlg) {
    GtkWidget* content = app_dialog_get_content_area(APP_DIALOG(dlg));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_box_append(GTK_BOX(content), box);
    return box;
}

/* Simple message / confirmation dialog; returns TRUE when ok_label was chosen */
gboolean bookmark_message(AppData* app, const gchar* title, const gchar* message,
                           const gchar* ok_label, const gchar* cancel_label) {
    GtkWidget* dlg = app_dialog_new(title, app->window,
                                    NULL, NULL);
    if (cancel_label != NULL) {
        app_dialog_add_button(APP_DIALOG(dlg), cancel_label, APP_RESPONSE_CANCEL);
    }
    app_dialog_add_button(APP_DIALOG(dlg), ok_label, APP_RESPONSE_OK);
    app_dialog_set_default_response(APP_DIALOG(dlg), cancel_label != NULL ? APP_RESPONSE_CANCEL : APP_RESPONSE_OK);
    GtkWidget* box = bm_dialog_body(dlg);
    GtkWidget* label = gtk_label_new(message);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 60);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_append(GTK_BOX(box), label);

    BmDialog d = { NULL, APP_RESPONSE_CANCEL, NULL, NULL };
    gint resp = bm_run_modal(dlg, &d, NULL);
    app_dialog_destroy(APP_DIALOG(dlg));
    return resp == APP_RESPONSE_OK;
}

static void bm_on_use_current_dir(GtkButton* btn, gpointer data) {
    GtkWidget* entry = GTK_WIDGET(data);
    const gchar* dir = (const gchar*)g_object_get_data(G_OBJECT(btn), "base-dir");
    if (dir != NULL) {
        gchar* shown = bookmark_display_location(dir);
        gtk_editable_set_text(GTK_EDITABLE(entry), shown);
        g_free(shown);
    }
}

static void bm_on_entry_changed(GtkEditable* editable, gpointer data) {
    (void)data;
    gtk_widget_remove_css_class(GTK_WIDGET(editable), "error");
}

static GtkWidget* bm_field_label(const gchar* text) {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "dim-label");
    return label;
}

gint bookmark_item_dialog(AppData* app, const BookmarkDialogSpec* spec,
                          gchar** out_name, gchar** out_target, const gchar** out_icon,
                          guint* out_folder) {
    gboolean is_folder = spec->is_folder;
    GtkWidget* dlg = app_dialog_new(spec->title, app->window,
                                    NULL, NULL);
    if (spec->allow_remove) {
        GtkWidget* rm = app_dialog_add_button(APP_DIALOG(dlg), "_Remove Bookmark", APP_RESPONSE_REJECT);
        gtk_widget_add_css_class(rm, "destructive-action");
    }
    app_dialog_add_button(APP_DIALOG(dlg), "_Cancel", APP_RESPONSE_CANCEL);
    GtkWidget* ok = app_dialog_add_button(APP_DIALOG(dlg), spec->ok_label, APP_RESPONSE_OK);
    gtk_widget_add_css_class(ok, "suggested-action");
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_OK);
    app_dialog_set_width(APP_DIALOG(dlg), 480);

    GtkWidget* box = bm_dialog_body(dlg);

    /* Name */
    gtk_box_append(GTK_BOX(box), bm_field_label("Name"));
    GtkWidget* name_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry),
                                   is_folder ? "Folder name" : "Defaults to the location's name");
    if (spec->name != NULL) {
        gtk_editable_set_text(GTK_EDITABLE(name_entry), spec->name);
    }
    gtk_entry_set_activates_default(GTK_ENTRY(name_entry), TRUE);
    g_signal_connect(name_entry, "changed", G_CALLBACK(bm_on_entry_changed), NULL);
    gtk_box_append(GTK_BOX(box), name_entry);

    /* Icon (folders) or location (links) */
    GtkWidget* target_entry = NULL;
    GtkWidget* picker = NULL;
    /* A saved workspace keeps its target: only its name and folder change */
    gboolean fixed_target = !is_folder && session_target_id(spec->target) != NULL;
    if (is_folder) {
        gtk_box_append(GTK_BOX(box), bm_field_label("Icon"));
        picker = icon_picker_new(NOTEBOOK_ICONS, spec->icon, NULL, NULL);
        gtk_box_append(GTK_BOX(box), picker);
    } else {
        gtk_box_append(GTK_BOX(box), bm_field_label("Location"));
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        target_entry = gtk_entry_new();
        gtk_widget_set_hexpand(target_entry, TRUE);
        gtk_entry_set_placeholder_text(GTK_ENTRY(target_entry), "~/projects, /etc/hosts or https://…");
        if (spec->target != NULL) {
            gchar* shown = bookmark_display_location(spec->target);
            gtk_editable_set_text(GTK_EDITABLE(target_entry), shown);
            g_free(shown);
        }
        gtk_entry_set_activates_default(GTK_ENTRY(target_entry), TRUE);
        g_signal_connect(target_entry, "changed", G_CALLBACK(bm_on_entry_changed), NULL);
        gtk_box_append(GTK_BOX(row), target_entry);

        if (fixed_target) {
            gtk_editable_set_editable(GTK_EDITABLE(target_entry), FALSE);
        } else {
            GtkWidget* cur_btn = gtk_button_new_with_label("Current Directory");
            gtk_widget_set_tooltip_text(cur_btn, app->base_dir);
            g_object_set_data_full(G_OBJECT(cur_btn), "base-dir", g_strdup(app->base_dir), g_free);
            g_signal_connect(cur_btn, "clicked", G_CALLBACK(bm_on_use_current_dir), target_entry);
            gtk_box_append(GTK_BOX(row), cur_btn);
        }
        gtk_box_append(GTK_BOX(box), row);
    }

    /* Parent folder */
    gtk_box_append(GTK_BOX(box), bm_field_label(is_folder ? "Parent folder" : "Folder"));
    GtkWidget* folder_dd = gtk_drop_down_new_from_strings(spec->folders);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(folder_dd), spec->folder);
    gtk_box_append(GTK_BOX(box), folder_dd);

    BmDialog d = { NULL, APP_RESPONSE_CANCEL, is_folder ? name_entry : NULL, target_entry };
    GtkWidget* focus = (!is_folder && spec->target == NULL) ? target_entry : name_entry;
    gint resp = bm_run_modal(dlg, &d, focus);

    if (resp == APP_RESPONSE_OK) {
        *out_folder = gtk_drop_down_get_selected(GTK_DROP_DOWN(folder_dd));
        *out_target = fixed_target ? g_strdup(spec->target)
                      : (target_entry != NULL)
                          ? bookmark_normalize_target(gtk_editable_get_text(GTK_EDITABLE(target_entry)),
                                                      app->base_dir)
                          : NULL;
        gchar* name = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(name_entry))));
        if (name[0] == '\0') {
            g_free(name);
            name = bookmark_default_name(*out_target);
        }
        *out_name = name;
        *out_icon = (picker != NULL) ? icon_picker_get_icon(picker) : NULL;
    }

    app_dialog_destroy(APP_DIALOG(dlg));
    return resp;
}
