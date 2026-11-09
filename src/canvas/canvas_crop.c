#include "canvas/canvas_crop.h"
#include "widgets/dialog.h"
#include "core/i18n.h"
#include "theme/palette.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>

/* Crop dialog: the whole picture, as large as the window allows, under a
 * crop frame. Dragging a corner handle resizes the frame (the opposite
 * corner stays), dragging inside it moves it. */

#define CROP_MARGIN 14.0   /* room around the picture to grab the handles */
#define CROP_HANDLE_R 6.0  /* drawn radius of a corner handle */
#define CROP_HIT_R 14.0    /* grab radius of a corner handle */
#define CROP_MIN_PX 8.0    /* smallest frame, in picture pixels */

typedef enum {
    CROP_DRAG_NONE = -1,
    CROP_DRAG_TL = 0,
    CROP_DRAG_TR,
    CROP_DRAG_BR,
    CROP_DRAG_BL,
    CROP_DRAG_MOVE
} CropDrag;

typedef struct {
    GtkWidget* dialog;
    CanvasCropCallback on_done;
    gpointer user_data;
    GDestroyNotify destroy;
    GtkWidget* area;
    GtkWidget* size_label;
    GdkPixbuf* pixbuf;  /* the picture, loaded once */
    GdkPixbuf* scaled;  /* the picture at its shown size (cache) */
    double cx, cy, cw, ch;
    /* Picture placement in the area, from the last draw */
    double ox, oy, scale;
    CropDrag drag;
    double start_x, start_y, start_w, start_h;
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

/* ------------------------------------------------------------------------- */
/* Geometry                                                                  */
/* ------------------------------------------------------------------------- */

static int pic_w(const CropState* st) {
    return gdk_pixbuf_get_width(st->pixbuf);
}

static int pic_h(const CropState* st) {
    return gdk_pixbuf_get_height(st->pixbuf);
}

/* Fit the picture in the area, centred, keeping its proportions */
static void update_placement(CropState* st, int width, int height) {
    double aw = MAX(1.0, width - 2 * CROP_MARGIN);
    double ah = MAX(1.0, height - 2 * CROP_MARGIN);
    st->scale = MIN(aw / pic_w(st), ah / pic_h(st));
    st->ox = (width - pic_w(st) * st->scale) / 2.0;
    st->oy = (height - pic_h(st) * st->scale) / 2.0;
}

/* Frame in area coordinates */
static void frame_rect(const CropState* st, double* x, double* y, double* w, double* h) {
    double sw = pic_w(st) * st->scale;
    double sh = pic_h(st) * st->scale;
    *x = st->ox + st->cx * sw;
    *y = st->oy + st->cy * sh;
    *w = st->cw * sw;
    *h = st->ch * sh;
}

static void corner_point(const CropState* st, int corner, double* px, double* py) {
    double x, y, w, h;
    frame_rect(st, &x, &y, &w, &h);
    *px = (corner == CROP_DRAG_TR || corner == CROP_DRAG_BR) ? x + w : x;
    *py = (corner == CROP_DRAG_BL || corner == CROP_DRAG_BR) ? y + h : y;
}

/* What a press at (x, y) grabs: a corner (the nearest), the frame, or nothing */
static CropDrag hit_test(const CropState* st, double x, double y) {
    if (st->scale <= 0) return CROP_DRAG_NONE;
    CropDrag best = CROP_DRAG_NONE;
    double best_d = CROP_HIT_R;
    for (int c = CROP_DRAG_TL; c <= CROP_DRAG_BL; c++) {
        double px, py;
        corner_point(st, c, &px, &py);
        double d = hypot(x - px, y - py);
        if (d <= best_d) {
            best_d = d;
            best = (CropDrag)c;
        }
    }
    if (best != CROP_DRAG_NONE) return best;
    double fx, fy, fw, fh;
    frame_rect(st, &fx, &fy, &fw, &fh);
    if (x >= fx && x <= fx + fw && y >= fy && y <= fy + fh) return CROP_DRAG_MOVE;
    return CROP_DRAG_NONE;
}

static const gchar* drag_cursor(CropDrag drag) {
    switch (drag) {
        case CROP_DRAG_TL:
        case CROP_DRAG_BR: return "nwse-resize";
        case CROP_DRAG_TR:
        case CROP_DRAG_BL: return "nesw-resize";
        case CROP_DRAG_MOVE: return "move";
        default: return NULL;
    }
}

static void update_size_label(CropState* st) {
    int w = MAX(1, (int)lround(st->cw * pic_w(st)));
    int h = MAX(1, (int)lround(st->ch * pic_h(st)));
    /* TRANSLATORS: size of the cropped picture, in pixels */
    gchar* text = g_strdup_printf(_("%1$d × %2$d pixels"), w, h);
    gtk_label_set_text(GTK_LABEL(st->size_label), text);
    g_free(text);
}

static void crop_changed(CropState* st) {
    update_size_label(st);
    gtk_widget_queue_draw(st->area);
}

/* ------------------------------------------------------------------------- */
/* Drawing                                                                   */
/* ------------------------------------------------------------------------- */

static void draw_handle(cairo_t* cr, double x, double y) {
    double fr, fg, fb, br, bg, bb;
    palette_rgb(PAL_FOCUS_RING, &fr, &fg, &fb);
    palette_rgb(PAL_BG_VIEW, &br, &bg, &bb);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x, y, CROP_HANDLE_R, 0, 2 * M_PI);
    cairo_set_source_rgb(cr, br, bg, bb);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, fr, fg, fb);
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);
}

static void on_crop_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)area;
    CropState* st = user_data;
    if (st->pixbuf == NULL) return;
    update_placement(st, width, height);

    int sw = MAX(1, (int)lround(pic_w(st) * st->scale));
    int sh = MAX(1, (int)lround(pic_h(st) * st->scale));
    if (st->scaled == NULL || gdk_pixbuf_get_width(st->scaled) != sw || gdk_pixbuf_get_height(st->scaled) != sh) {
        g_clear_object(&st->scaled);
        st->scaled = gdk_pixbuf_scale_simple(st->pixbuf, sw, sh, GDK_INTERP_BILINEAR);
    }
    if (st->scaled == NULL) return;
    double px = round(st->ox), py = round(st->oy);
    gdk_cairo_set_source_pixbuf(cr, st->scaled, px, py);
    cairo_rectangle(cr, px, py, sw, sh);
    cairo_fill(cr);

    /* Dim what is cut off */
    double x, y, w, h;
    frame_rect(st, &x, &y, &w, &h);
    double br, bg, bb;
    palette_rgb(PAL_BG_VIEW, &br, &bg, &bb);
    cairo_save(cr);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_rectangle(cr, px, py, sw, sh);
    cairo_rectangle(cr, x, y, w, h);
    cairo_set_source_rgba(cr, br, bg, bb, 0.65);
    cairo_fill(cr);
    cairo_restore(cr);

    /* Frame, rule-of-thirds guides, corner handles */
    double fr, fg, fb;
    palette_rgb(PAL_FOCUS_RING, &fr, &fg, &fb);
    cairo_set_source_rgba(cr, fr, fg, fb, 0.45);
    cairo_set_line_width(cr, 1.0);
    for (int i = 1; i <= 2; i++) {
        cairo_move_to(cr, x + w * i / 3.0, y);
        cairo_line_to(cr, x + w * i / 3.0, y + h);
        cairo_move_to(cr, x, y + h * i / 3.0);
        cairo_line_to(cr, x + w, y + h * i / 3.0);
    }
    cairo_stroke(cr);
    cairo_set_source_rgb(cr, fr, fg, fb);
    cairo_set_line_width(cr, 1.5);
    cairo_rectangle(cr, x, y, w, h);
    cairo_stroke(cr);
    for (int c = CROP_DRAG_TL; c <= CROP_DRAG_BL; c++) {
        double hx, hy;
        corner_point(st, c, &hx, &hy);
        draw_handle(cr, hx, hy);
    }
}

/* ------------------------------------------------------------------------- */
/* Pointer                                                                   */
/* ------------------------------------------------------------------------- */

static void on_motion(GtkEventControllerMotion* motion, double x, double y, gpointer user_data) {
    (void)motion;
    CropState* st = user_data;
    if (st->drag != CROP_DRAG_NONE) return;
    gtk_widget_set_cursor_from_name(st->area, drag_cursor(hit_test(st, x, y)));
}

static void on_drag_begin(GtkGestureDrag* gesture, double x, double y, gpointer user_data) {
    CropState* st = user_data;
    st->drag = hit_test(st, x, y);
    if (st->drag == CROP_DRAG_NONE) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    st->start_x = st->cx;
    st->start_y = st->cy;
    st->start_w = st->cw;
    st->start_h = st->ch;
}

static void on_drag_update(GtkGestureDrag* gesture, double dx, double dy, gpointer user_data) {
    (void)gesture;
    CropState* st = user_data;
    if (st->drag == CROP_DRAG_NONE || st->scale <= 0) return;
    double ux = dx / (pic_w(st) * st->scale);
    double uy = dy / (pic_h(st) * st->scale);
    double min_w = MIN(1.0, CROP_MIN_PX / pic_w(st));
    double min_h = MIN(1.0, CROP_MIN_PX / pic_h(st));
    double left = st->start_x, top = st->start_y;
    double right = st->start_x + st->start_w, bottom = st->start_y + st->start_h;

    if (st->drag == CROP_DRAG_MOVE) {
        st->cx = CLAMP(st->start_x + ux, 0.0, 1.0 - st->start_w);
        st->cy = CLAMP(st->start_y + uy, 0.0, 1.0 - st->start_h);
        crop_changed(st);
        return;
    }
    if (st->drag == CROP_DRAG_TL || st->drag == CROP_DRAG_BL) {
        left = CLAMP(left + ux, 0.0, right - min_w);
    } else {
        right = CLAMP(right + ux, left + min_w, 1.0);
    }
    if (st->drag == CROP_DRAG_TL || st->drag == CROP_DRAG_TR) {
        top = CLAMP(top + uy, 0.0, bottom - min_h);
    } else {
        bottom = CLAMP(bottom + uy, top + min_h, 1.0);
    }
    st->cx = left;
    st->cy = top;
    st->cw = right - left;
    st->ch = bottom - top;
    crop_changed(st);
}

static void on_drag_end(GtkGestureDrag* gesture, double dx, double dy, gpointer user_data) {
    (void)gesture;
    (void)dx;
    (void)dy;
    CropState* st = user_data;
    st->drag = CROP_DRAG_NONE;
}

/* ------------------------------------------------------------------------- */
/* Dialog                                                                    */
/* ------------------------------------------------------------------------- */

static void on_crop_dialog_response(AppDialog* dialog, gint response, gpointer user_data) {
    CropState* st = user_data;
    if (response == APP_RESPONSE_APPLY || response == APP_RESPONSE_OK) {
        if (st->on_done != NULL) st->on_done(st->cx, st->cy, st->cw, st->ch, st->user_data);
        app_dialog_destroy(dialog);
    } else if (response == APP_RESPONSE_REJECT) {
        st->cx = 0.0;
        st->cy = 0.0;
        st->cw = 1.0;
        st->ch = 1.0;
        crop_changed(st);
    } else {
        app_dialog_destroy(dialog);
    }
}

static void on_crop_state_destroy(gpointer data) {
    CropState* st = data;
    if (st->destroy != NULL) st->destroy(st->user_data);
    g_clear_object(&st->scaled);
    g_clear_object(&st->pixbuf);
    g_free(st);
}

void canvas_crop_dialog_show(GtkWindow* parent_window, const gchar* full_path,
                             double x, double y, double w, double h,
                             CanvasCropCallback on_done, gpointer user_data, GDestroyNotify destroy) {
    GdkPixbuf* pixbuf = NULL;
    if (parent_window != NULL && full_path != NULL) {
        pixbuf = gdk_pixbuf_new_from_file(full_path, NULL);
    }
    if (pixbuf == NULL) {
        if (destroy != NULL) destroy(user_data);
        return;
    }

    CropState* st = g_new0(CropState, 1);
    st->pixbuf = pixbuf;
    st->on_done = on_done;
    st->user_data = user_data;
    st->destroy = destroy;
    st->drag = CROP_DRAG_NONE;
    gboolean valid = w > 0 && h > 0 && x >= 0 && y >= 0 && x + w <= 1.0001 && y + h <= 1.0001;
    st->cx = valid ? x : 0.0;
    st->cy = valid ? y : 0.0;
    st->cw = valid ? MIN(w, 1.0 - st->cx) : 1.0;
    st->ch = valid ? MIN(h, 1.0 - st->cy) : 1.0;

    GtkWidget* dlg = app_dialog_new(_("Crop Image"), parent_window,
                                    _("_Reset"), APP_RESPONSE_REJECT,
                                    _("_Cancel"), APP_RESPONSE_CANCEL,
                                    _("_Apply"), APP_RESPONSE_APPLY,
                                    NULL);
    st->dialog = dlg;
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_APPLY);
    g_object_set_data_full(G_OBJECT(dlg), "crop-state", st, on_crop_state_destroy);
    g_signal_connect(dlg, "response", G_CALLBACK(on_crop_dialog_response), st);

    /* As large as the window allows, around the dialog's frame and buttons */
    int win_w = gtk_widget_get_width(GTK_WIDGET(parent_window));
    int win_h = gtk_widget_get_height(GTK_WIDGET(parent_window));
    int area_w = MAX(360, (int)(win_w * 0.85) - 48);
    int area_h = MAX(240, (int)(win_h * 0.85) - 160);
    app_dialog_set_width(APP_DIALOG(dlg), area_w + 32);

    GtkWidget* content = app_dialog_get_content_area(APP_DIALOG(dlg));
    gtk_box_set_spacing(GTK_BOX(content), 8);

    st->area = gtk_drawing_area_new();
    gtk_widget_set_size_request(st->area, area_w, area_h);
    gtk_widget_set_hexpand(st->area, TRUE);
    gtk_widget_set_vexpand(st->area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(st->area), on_crop_draw, st, NULL);
    gtk_box_append(GTK_BOX(content), st->area);

    GtkGesture* drag = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), st);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), st);
    g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), st);
    gtk_widget_add_controller(st->area, GTK_EVENT_CONTROLLER(drag));

    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), st);
    gtk_widget_add_controller(st->area, motion);

    GtkWidget* hint = gtk_label_new(_("Drag a corner to crop, the inside of the frame to move it."));
    gtk_widget_add_css_class(hint, "dim-label");
    gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    st->size_label = gtk_label_new(NULL);
    gtk_widget_add_css_class(st->size_label, "dim-label");
    gtk_widget_set_hexpand(st->size_label, TRUE);
    gtk_widget_set_halign(st->size_label, GTK_ALIGN_END);
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append(GTK_BOX(row), hint);
    gtk_box_append(GTK_BOX(row), st->size_label);
    gtk_box_append(GTK_BOX(content), row);
    update_size_label(st);

    app_dialog_present(APP_DIALOG(dlg));
}
