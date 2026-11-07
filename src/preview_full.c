/* "Load Whole File" bar of the preview (see preview_full.h). */
#include "preview_full.h"

#include "app_events.h"

typedef struct {
    AppData* app;
    GtkWidget* bar;
    GtkWidget* label;
    GtkWidget* button;
    gchar* shown_path;   /* file of the partial preview the bar is about */
    gchar* wanted_path;  /* file to read to the end, or NULL */
} PreviewFull;

static void preview_full_free(gpointer data) {
    PreviewFull* pf = data;
    g_free(pf->shown_path);
    g_free(pf->wanted_path);
    g_free(pf);
}

static void on_load_clicked(GtkButton* button, gpointer data) {
    (void)button;
    PreviewFull* pf = data;
    if (pf->shown_path == NULL) {
        return;
    }
    g_free(pf->wanted_path);
    pf->wanted_path = g_strdup(pf->shown_path);
    gtk_label_set_text(GTK_LABEL(pf->label), "Loading the whole file…");
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
    pf->button = gtk_button_new_with_label("Load Whole File");
    gtk_widget_set_focusable(pf->button, FALSE);
    gtk_widget_set_tooltip_text(pf->button, "Load Whole File into the Preview");
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
    gchar* s = g_format_size((guint64)shown);
    gchar* t = g_format_size((guint64)total);
    gchar* text = g_strdup_printf("Partial preview: %s of %s shown", s, t);
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
