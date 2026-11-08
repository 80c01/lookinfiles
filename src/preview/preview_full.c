/* "Load Whole File" bar of the preview (see preview_full.h). */
#include "preview/preview_full.h"

#include "core/app_events.h"
#include "widgets/dialog.h"
#include "core/i18n.h"

/* Above this size, ask first: showing the text can freeze the window a while */
#define PREVIEW_FULL_CONFIRM_BYTES (64 * 1024 * 1024)

typedef struct PreviewFull {
    AppData* app;
    GtkWidget* bar;
    GtkWidget* label;
    GtkWidget* button;
    gchar* shown_path;   /* file of the partial preview the bar is about */
    gint64 total;        /* its size */
    gchar* wanted_path;  /* file to read to the end, or NULL */
} PreviewFull;

static void preview_full_free(gpointer data) {
    PreviewFull* pf = data;
    g_free(pf->shown_path);
    g_free(pf->wanted_path);
    g_free(pf);
}

/* TRUE if the user still wants a file of this size in the preview */
static gboolean confirm_large(PreviewFull* pf) {
    GtkWidget* dlg = app_dialog_new(_("Load Whole File"), pf->app->window,
                                    _("_Cancel"), APP_RESPONSE_CANCEL,
                                    _("_Load"), APP_RESPONSE_OK,
                                    NULL);
    gchar* size = g_format_size((guint64)pf->total);
    gchar* message = g_strdup_printf(_("This file is %s. Showing all of it may make the window "
                                       "unresponsive for a while. Load it anyway?"),
                                     size);
    GtkWidget* label = gtk_label_new(message);
    g_free(message);
    g_free(size);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 60);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_margin_top(label, 14);
    gtk_widget_set_margin_bottom(label, 14);
    gtk_widget_set_margin_start(label, 16);
    gtk_widget_set_margin_end(label, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), label);
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL);
    gint response = app_dialog_run(APP_DIALOG(dlg),
                                   app_dialog_get_widget_for_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL));
    app_dialog_destroy(APP_DIALOG(dlg));
    return response == APP_RESPONSE_OK;
}

static void on_load_clicked(GtkButton* button, gpointer data) {
    (void)button;
    PreviewFull* pf = data;
    if (pf->shown_path == NULL) {
        return;
    }
    gchar* path = g_strdup(pf->shown_path);
    gboolean go = pf->total <= PREVIEW_FULL_CONFIRM_BYTES || confirm_large(pf);
    /* Still the same preview after the dialog */
    go = go && g_strcmp0(pf->shown_path, path) == 0;
    g_free(path);
    if (!go) {
        return;
    }
    g_free(pf->wanted_path);
    pf->wanted_path = g_strdup(pf->shown_path);
    gtk_label_set_text(GTK_LABEL(pf->label), _("Loading the whole file…"));
    gtk_widget_set_sensitive(pf->button, FALSE);
    app_emit(pf->app, APP_EVENT_PREVIEW_STALE);
}

GtkWidget* preview_full_wrap(AppData* app, GtkWidget* content) {
    PreviewFull* pf = g_new0(PreviewFull, 1);
    pf->app = app;
    app->preview_full = pf;

    pf->bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(pf->bar, "preview-full-bar");
    pf->label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(pf->label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(pf->label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(pf->label, TRUE);
    gtk_box_append(GTK_BOX(pf->bar), pf->label);
    pf->button = gtk_button_new_with_label(_("Load Whole File"));
    gtk_widget_set_focusable(pf->button, FALSE);
    gtk_widget_set_tooltip_text(pf->button, _("Load Whole File into the Preview"));
    g_signal_connect(pf->button, "clicked", G_CALLBACK(on_load_clicked), pf);
    gtk_box_append(GTK_BOX(pf->bar), pf->button);
    gtk_widget_set_visible(pf->bar, FALSE);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_vexpand(box, TRUE);
    gtk_box_append(GTK_BOX(box), pf->bar);
    gtk_box_append(GTK_BOX(box), content);
    g_object_set_data_full(G_OBJECT(box), "preview-full", pf, preview_full_free);
    return box;
}

void preview_full_update(AppData* app, const gchar* path, gint64 shown, gint64 total) {
    PreviewFull* pf = app->preview_full;
    if (pf == NULL) {
        return;
    }
    if (path == NULL || shown < 0 || shown >= total) {
        gtk_widget_set_visible(pf->bar, FALSE);
        g_clear_pointer(&pf->shown_path, g_free);
        return;
    }
    if (g_strcmp0(pf->shown_path, path) != 0) {
        g_free(pf->shown_path);
        pf->shown_path = g_strdup(path);
    }
    pf->total = total;
    gchar* s = g_format_size((guint64)shown);
    gchar* t = g_format_size((guint64)total);
    gchar* text = g_strdup_printf(_("Partial preview: %1$s of %2$s shown"), s, t);
    gtk_label_set_text(GTK_LABEL(pf->label), text);
    g_free(text);
    g_free(t);
    g_free(s);
    gtk_widget_set_sensitive(pf->button, TRUE);
    gtk_widget_set_visible(pf->bar, TRUE);
}

gboolean preview_full_wanted(AppData* app, const gchar* path) {
    PreviewFull* pf = app->preview_full;
    return pf != NULL && path != NULL && g_strcmp0(pf->wanted_path, path) == 0;
}

void preview_full_reset(AppData* app, const gchar* path) {
    PreviewFull* pf = app->preview_full;
    if (pf == NULL) {
        return;
    }
    if (g_strcmp0(pf->wanted_path, path) != 0) {
        g_clear_pointer(&pf->wanted_path, g_free);
    }
    if (g_strcmp0(pf->shown_path, path) != 0) {
        preview_full_update(app, NULL, -1, 0);
    }
}
