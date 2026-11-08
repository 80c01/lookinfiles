#include "canvas_crop.h"
#include "dialog.h"
#include "i18n.h"
#include "theme.h"

#include <gdk-pixbuf/gdk-pixbuf.h>

typedef struct {
    GtkWidget* dialog;
    gchar* full_path;
    CanvasCropCallback on_done;
    gpointer user_data;
    GDestroyNotify destroy;
    GtkWidget* preview_area;
    GtkWidget* scale_x;
    GtkWidget* scale_y;
    GtkWidget* scale_w;
    GtkWidget* scale_h;
    double cx, cy, cw, ch;
} CropState;

void canvas_draw_cropped_image(cairo_t* cr, const gchar* full_path,
                               double crop_x, double crop_y, double crop_w, double crop_h,
                               int dest_w, int dest_h) {
    if (cr == NULL || full_path == NULL || dest_w <= 0 || dest_h <= 0) return;
    GError* err = NULL;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file(full_path, &err);
    if (pixbuf == NULL) {
        g_clear_error(&err);
        return;
    }

    int pw = gdk_pixbuf_get_width(pixbuf);
    int ph = gdk_pixbuf_get_height(pixbuf);
    int sub_x = CLAMP((int)(crop_x * pw), 0, pw - 1);
    int sub_y = CLAMP((int)(crop_y * ph), 0, ph - 1);
    int sub_w = CLAMP((int)(crop_w * pw), 1, pw - sub_x);
    int sub_h = CLAMP((int)(crop_h * ph), 1, ph - sub_y);

    GdkPixbuf* sub = gdk_pixbuf_new_subpixbuf(pixbuf, sub_x, sub_y, sub_w, sub_h);
    GdkPixbuf* scaled = gdk_pixbuf_scale_simple(sub, dest_w, dest_h, GDK_INTERP_BILINEAR);

    gdk_cairo_set_source_pixbuf(cr, scaled, 0, 0);
    cairo_paint(cr);

    g_object_unref(scaled);
    g_object_unref(sub);
    g_object_unref(pixbuf);
}

static void on_crop_preview_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)area;
    CropState* st = user_data;
    if (st == NULL || st->full_path == NULL) return;

    /* Draw the cropped sub-image in preview */
    canvas_draw_cropped_image(cr, st->full_path, st->cx, st->cy, st->cw, st->ch, width, height);
}

static void on_slider_changed(GtkRange* range, gpointer user_data) {
    (void)range;
    CropState* st = user_data;
    st->cx = gtk_range_get_value(GTK_RANGE(st->scale_x)) / 100.0;
    st->cy = gtk_range_get_value(GTK_RANGE(st->scale_y)) / 100.0;
    st->cw = gtk_range_get_value(GTK_RANGE(st->scale_w)) / 100.0;
    st->ch = gtk_range_get_value(GTK_RANGE(st->scale_h)) / 100.0;

    if (st->cx + st->cw > 1.0) st->cw = 1.0 - st->cx;
    if (st->cy + st->ch > 1.0) st->ch = 1.0 - st->cy;

    gtk_widget_queue_draw(st->preview_area);
}

static void on_crop_dialog_response(AppDialog* dialog, gint response, gpointer user_data) {
    CropState* st = user_data;
    if (response == APP_RESPONSE_APPLY || response == APP_RESPONSE_OK) {
        if (st->on_done != NULL) st->on_done(st->cx, st->cy, st->cw, st->ch, st->user_data);
        app_dialog_destroy(dialog);
    } else if (response == APP_RESPONSE_REJECT) {
        /* Reset */
        st->cx = 0.0; st->cy = 0.0; st->cw = 1.0; st->ch = 1.0;
        gtk_range_set_value(GTK_RANGE(st->scale_x), 0);
        gtk_range_set_value(GTK_RANGE(st->scale_y), 0);
        gtk_range_set_value(GTK_RANGE(st->scale_w), 100);
        gtk_range_set_value(GTK_RANGE(st->scale_h), 100);
        gtk_widget_queue_draw(st->preview_area);
    } else {
        app_dialog_destroy(dialog);
    }
}

static void on_crop_state_destroy(gpointer data) {
    CropState* st = data;
    if (st->destroy != NULL) st->destroy(st->user_data);
    g_free(st->full_path);
    g_free(st);
}

void canvas_crop_dialog_show(GtkWindow* parent_window, const gchar* full_path,
                             double x, double y, double w, double h,
                             CanvasCropCallback on_done, gpointer user_data, GDestroyNotify destroy) {
    if (parent_window == NULL || full_path == NULL) {
        if (destroy != NULL) destroy(user_data);
        return;
    }

    CropState* st = g_new0(CropState, 1);
    st->full_path = g_strdup(full_path);
    st->on_done = on_done;
    st->user_data = user_data;
    st->destroy = destroy;
    st->cx = (w > 0) ? x : 0.0;
    st->cy = (h > 0) ? y : 0.0;
    st->cw = (w > 0) ? w : 1.0;
    st->ch = (h > 0) ? h : 1.0;

    GtkWidget* dlg = app_dialog_new(_("Crop Image"), parent_window,
                                    _("Apply"), APP_RESPONSE_APPLY,
                                    _("Reset"), APP_RESPONSE_REJECT,
                                    _("Cancel"), APP_RESPONSE_CANCEL,
                                    NULL);
    st->dialog = dlg;
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_APPLY);
    app_dialog_set_width(APP_DIALOG(dlg), 420);
    g_object_set_data_full(G_OBJECT(dlg), "crop-state", st, on_crop_state_destroy);
    g_signal_connect(dlg, "response", G_CALLBACK(on_crop_dialog_response), st);

    GtkWidget* area = app_dialog_get_content_area(APP_DIALOG(dlg));
    gtk_box_set_spacing(GTK_BOX(area), 8);

    /* Preview area */
    st->preview_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(st->preview_area, 360, 240);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(st->preview_area), on_crop_preview_draw, st, NULL);
    gtk_box_append(GTK_BOX(area), st->preview_area);

    /* Sliders grid: Left, Top, Width, Height */
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);

    GtkWidget* lbl_x = gtk_label_new(_("Left %"));
    st->scale_x = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 90, 1);
    gtk_range_set_value(GTK_RANGE(st->scale_x), st->cx * 100.0);
    gtk_widget_set_hexpand(st->scale_x, TRUE);
    g_signal_connect(st->scale_x, "value-changed", G_CALLBACK(on_slider_changed), st);
    gtk_grid_attach(GTK_GRID(grid), lbl_x, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), st->scale_x, 1, 0, 1, 1);

    GtkWidget* lbl_y = gtk_label_new(_("Top %"));
    st->scale_y = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 90, 1);
    gtk_range_set_value(GTK_RANGE(st->scale_y), st->cy * 100.0);
    gtk_widget_set_hexpand(st->scale_y, TRUE);
    g_signal_connect(st->scale_y, "value-changed", G_CALLBACK(on_slider_changed), st);
    gtk_grid_attach(GTK_GRID(grid), lbl_y, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), st->scale_y, 1, 1, 1, 1);

    GtkWidget* lbl_w = gtk_label_new(_("Width %"));
    st->scale_w = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 10, 100, 1);
    gtk_range_set_value(GTK_RANGE(st->scale_w), st->cw * 100.0);
    gtk_widget_set_hexpand(st->scale_w, TRUE);
    g_signal_connect(st->scale_w, "value-changed", G_CALLBACK(on_slider_changed), st);
    gtk_grid_attach(GTK_GRID(grid), lbl_w, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), st->scale_w, 1, 2, 1, 1);

    GtkWidget* lbl_h = gtk_label_new(_("Height %"));
    st->scale_h = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 10, 100, 1);
    gtk_range_set_value(GTK_RANGE(st->scale_h), st->ch * 100.0);
    gtk_widget_set_hexpand(st->scale_h, TRUE);
    g_signal_connect(st->scale_h, "value-changed", G_CALLBACK(on_slider_changed), st);
    gtk_grid_attach(GTK_GRID(grid), lbl_h, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), st->scale_h, 1, 3, 1, 1);

    gtk_box_append(GTK_BOX(area), grid);
    app_dialog_present(APP_DIALOG(dlg));
}
