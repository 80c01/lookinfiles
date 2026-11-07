/* The scrolled, zoomed view of a Canvas page (see canvas_viewport.h). */
#include "canvas_viewport.h"

#include <math.h>

#define WHEEL_FACTOR 1.1 /* zoom per wheel notch */
#define KEY_FACTOR 1.25  /* zoom per key press */
#define MAX_WHEEL_STEP 5.0

/* ------------------------------------------------------------------------- */
/* Zoom box: its child drawn (and picked) at a scale                         */
/* ------------------------------------------------------------------------- */

#define CANVAS_TYPE_ZOOM_BOX (canvas_zoom_box_get_type())
G_DECLARE_FINAL_TYPE(CanvasZoomBox, canvas_zoom_box, CANVAS, ZOOM_BOX, GtkWidget)

struct _CanvasZoomBox {
    GtkWidget parent_instance;
    GtkWidget* child;
    gdouble scale;
};

G_DEFINE_TYPE(CanvasZoomBox, canvas_zoom_box, GTK_TYPE_WIDGET)

static void zoom_box_measure(GtkWidget* widget, GtkOrientation orientation, int for_size, int* minimum,
                             int* natural, int* minimum_baseline, int* natural_baseline) {
    CanvasZoomBox* z = CANVAS_ZOOM_BOX(widget);
    int min = 0, nat = 0;
    if (z->child != NULL) {
        gtk_widget_measure(z->child, orientation, for_size < 0 ? -1 : (int)(for_size / z->scale),
                           &min, &nat, NULL, NULL);
    }
    *minimum = (int)ceil(min * z->scale);
    *natural = (int)ceil(nat * z->scale);
    *minimum_baseline = -1;
    *natural_baseline = -1;
}

static void zoom_box_allocate(GtkWidget* widget, int width, int height, int baseline) {
    (void)baseline;
    CanvasZoomBox* z = CANVAS_ZOOM_BOX(widget);
    if (z->child == NULL) return;
    GskTransform* t = gsk_transform_scale(NULL, (float)z->scale, (float)z->scale);
    gtk_widget_allocate(z->child, (int)floor(width / z->scale + 1e-6), (int)floor(height / z->scale + 1e-6),
                        -1, t);
}

static void zoom_box_dispose(GObject* object) {
    CanvasZoomBox* z = CANVAS_ZOOM_BOX(object);
    g_clear_pointer(&z->child, gtk_widget_unparent);
    G_OBJECT_CLASS(canvas_zoom_box_parent_class)->dispose(object);
}

static void canvas_zoom_box_class_init(CanvasZoomBoxClass* klass) {
    G_OBJECT_CLASS(klass)->dispose = zoom_box_dispose;
    GTK_WIDGET_CLASS(klass)->measure = zoom_box_measure;
    GTK_WIDGET_CLASS(klass)->size_allocate = zoom_box_allocate;
}

static void canvas_zoom_box_init(CanvasZoomBox* z) {
    z->scale = 1.0;
}

static GtkWidget* zoom_box_new(GtkWidget* child) {
    CanvasZoomBox* z = g_object_new(CANVAS_TYPE_ZOOM_BOX, NULL);
    z->child = child;
    gtk_widget_set_parent(child, GTK_WIDGET(z));
    gtk_widget_set_hexpand(GTK_WIDGET(z), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(z), TRUE);
    return GTK_WIDGET(z);
}

/* ------------------------------------------------------------------------- */
/* Viewport                                                                  */
/* ------------------------------------------------------------------------- */

struct CanvasViewport {
    GtkWidget* scroll;
    CanvasZoomBox* zoom;
    GtkWidget* surface;
    CanvasZoomNotify on_zoom;
    gpointer data;
    /* Pointer over the scrolled window */
    gboolean pointer_in;
    gdouble pointer_x, pointer_y;
    /* Pan: key held, drag in progress from these scroll values */
    gboolean pan_key;
    gboolean panning;
    gdouble pan_h, pan_v;
    gboolean cursor_saved;
    GdkCursor* saved_cursor; /* the page's own cursor (may be NULL) */
};

static GtkAdjustment* adjustment(CanvasViewport* v, GtkOrientation o) {
    GtkScrolledWindow* sw = GTK_SCROLLED_WINDOW(v->scroll);
    return o == GTK_ORIENTATION_HORIZONTAL ? gtk_scrolled_window_get_hadjustment(sw)
                                           : gtk_scrolled_window_get_vadjustment(sw);
}

/* Scroll to value on one axis at the new scale. The view is laid out again
 * later: the range it will have is set now, so the value is not clamped to
 * the old one. */
static void scroll_axis(CanvasViewport* v, GtkOrientation o, gdouble value) {
    GtkAdjustment* adj = adjustment(v, o);
    int nat = 0;
    gtk_widget_measure(v->surface, o, -1, NULL, &nat, NULL, NULL);
    gdouble page = gtk_adjustment_get_page_size(adj);
    gdouble upper = MAX(page, ceil(nat * v->zoom->scale));
    gtk_adjustment_configure(adj, CLAMP(value, 0, MAX(upper - page, 0)), 0, upper,
                             gtk_adjustment_get_step_increment(adj), gtk_adjustment_get_page_increment(adj), page);
}

/* Zoom to scale keeping the page point under (sx, sy) of the view there */
static void zoom_at(CanvasViewport* v, gdouble scale, gdouble sx, gdouble sy) {
    scale = CLAMP(scale, CANVAS_ZOOM_MIN, CANVAS_ZOOM_MAX);
    gdouble old = v->zoom->scale;
    if (fabs(scale - old) < 1e-4) return;
    GtkAdjustment* h = adjustment(v, GTK_ORIENTATION_HORIZONTAL);
    GtkAdjustment* vv = adjustment(v, GTK_ORIENTATION_VERTICAL);
    gdouble px = (gtk_adjustment_get_value(h) + sx) / old;
    gdouble py = (gtk_adjustment_get_value(vv) + sy) / old;
    v->zoom->scale = scale;
    gtk_widget_queue_resize(GTK_WIDGET(v->zoom));
    scroll_axis(v, GTK_ORIENTATION_HORIZONTAL, px * scale - sx);
    scroll_axis(v, GTK_ORIENTATION_VERTICAL, py * scale - sy);
    if (v->on_zoom != NULL) v->on_zoom(scale, v->data);
}

static void zoom_at_centre(CanvasViewport* v, gdouble scale) {
    zoom_at(v, scale, gtk_adjustment_get_page_size(adjustment(v, GTK_ORIENTATION_HORIZONTAL)) / 2,
            gtk_adjustment_get_page_size(adjustment(v, GTK_ORIENTATION_VERTICAL)) / 2);
}

static gboolean on_scroll(GtkEventControllerScroll* ctrl, gdouble dx, gdouble dy, gpointer data) {
    (void)dx;
    CanvasViewport* v = data;
    GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(ctrl));
    if ((state & GDK_CONTROL_MASK) == 0) return FALSE;
    if (dy == 0) return TRUE;
    gdouble factor = pow(WHEEL_FACTOR, -CLAMP(dy, -MAX_WHEEL_STEP, MAX_WHEEL_STEP));
    if (v->pointer_in) {
        zoom_at(v, v->zoom->scale * factor, v->pointer_x, v->pointer_y);
    } else {
        zoom_at_centre(v, v->zoom->scale * factor);
    }
    return TRUE;
}

static void on_motion(GtkEventControllerMotion* ctrl, gdouble x, gdouble y, gpointer data) {
    (void)ctrl;
    CanvasViewport* v = data;
    v->pointer_in = TRUE;
    v->pointer_x = x;
    v->pointer_y = y;
}

static void on_leave(GtkEventControllerMotion* ctrl, gpointer data) {
    (void)ctrl;
    ((CanvasViewport*)data)->pointer_in = FALSE;
}

/* The page shows the pan cursor while the pan key is held or a pan goes on */
static void update_cursor(CanvasViewport* v) {
    gboolean pan = v->pan_key || v->panning;
    if (pan && !v->cursor_saved) {
        GdkCursor* c = gtk_widget_get_cursor(v->surface);
        v->saved_cursor = c != NULL ? g_object_ref(c) : NULL;
        v->cursor_saved = TRUE;
    }
    if (pan) {
        gtk_widget_set_cursor_from_name(v->surface, v->panning ? "grabbing" : "grab");
    } else if (v->cursor_saved) {
        gtk_widget_set_cursor(v->surface, v->saved_cursor);
        g_clear_object(&v->saved_cursor);
        v->cursor_saved = FALSE;
    }
}

static void on_pan_begin(GtkGestureDrag* gesture, gdouble x, gdouble y, gpointer data) {
    (void)x;
    (void)y;
    CanvasViewport* v = data;
    guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
    if (button != GDK_BUTTON_MIDDLE && !(button == GDK_BUTTON_PRIMARY && v->pan_key)) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    v->panning = TRUE;
    v->pan_h = gtk_adjustment_get_value(adjustment(v, GTK_ORIENTATION_HORIZONTAL));
    v->pan_v = gtk_adjustment_get_value(adjustment(v, GTK_ORIENTATION_VERTICAL));
    update_cursor(v);
}

static void on_pan_update(GtkGestureDrag* gesture, gdouble dx, gdouble dy, gpointer data) {
    (void)gesture;
    CanvasViewport* v = data;
    if (!v->panning) return;
    gtk_adjustment_set_value(adjustment(v, GTK_ORIENTATION_HORIZONTAL), v->pan_h - dx);
    gtk_adjustment_set_value(adjustment(v, GTK_ORIENTATION_VERTICAL), v->pan_v - dy);
}

static void on_pan_end(GtkGesture* gesture, GdkEventSequence* sequence, gpointer data) {
    (void)gesture;
    (void)sequence;
    CanvasViewport* v = data;
    if (!v->panning) return;
    v->panning = FALSE;
    update_cursor(v);
}

CanvasViewport* canvas_viewport_new(GtkWidget* surface, CanvasZoomNotify on_zoom, gpointer data) {
    CanvasViewport* v = g_new0(CanvasViewport, 1);
    v->surface = surface;
    v->on_zoom = on_zoom;
    v->data = data;
    v->zoom = CANVAS_ZOOM_BOX(zoom_box_new(surface));

    v->scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(v->scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(v->scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(v->scroll), GTK_WIDGET(v->zoom));

    GtkEventController* scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    gtk_event_controller_set_propagation_phase(scroll, GTK_PHASE_CAPTURE);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), v);
    gtk_widget_add_controller(v->scroll, scroll);

    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), v);
    g_signal_connect(motion, "leave", G_CALLBACK(on_leave), v);
    gtk_widget_add_controller(v->scroll, motion);

    /* Before the page's own drag (capture phase, on an ancestor) */
    GtkGesture* pan = gtk_gesture_drag_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(pan), 0);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(pan), GTK_PHASE_CAPTURE);
    g_signal_connect(pan, "drag-begin", G_CALLBACK(on_pan_begin), v);
    g_signal_connect(pan, "drag-update", G_CALLBACK(on_pan_update), v);
    g_signal_connect(pan, "end", G_CALLBACK(on_pan_end), v);
    gtk_widget_add_controller(v->scroll, GTK_EVENT_CONTROLLER(pan));
    return v;
}

void canvas_viewport_free(CanvasViewport* v) {
    if (v == NULL) return;
    g_clear_object(&v->saved_cursor);
    g_free(v);
}

GtkWidget* canvas_viewport_get_widget(CanvasViewport* v) {
    return v->scroll;
}

gdouble canvas_viewport_get_scale(CanvasViewport* v) {
    return v->zoom->scale;
}

void canvas_viewport_zoom_step(CanvasViewport* v, gint step) {
    gdouble scale = step == 0 ? 1.0 : v->zoom->scale * pow(KEY_FACTOR, step);
    /* Stop at 100 % on the way, so it is easy to get back to */
    if (step != 0 && (v->zoom->scale - 1.0) * (scale - 1.0) < 0) scale = 1.0;
    zoom_at_centre(v, scale);
}

void canvas_viewport_reset(CanvasViewport* v) {
    zoom_at(v, 1.0, 0, 0);
    gtk_adjustment_set_value(adjustment(v, GTK_ORIENTATION_HORIZONTAL), 0);
    gtk_adjustment_set_value(adjustment(v, GTK_ORIENTATION_VERTICAL), 0);
}

void canvas_viewport_origin(CanvasViewport* v, gint* x, gint* y) {
    *x = (gint)(gtk_adjustment_get_value(adjustment(v, GTK_ORIENTATION_HORIZONTAL)) / v->zoom->scale);
    *y = (gint)(gtk_adjustment_get_value(adjustment(v, GTK_ORIENTATION_VERTICAL)) / v->zoom->scale);
}

void canvas_viewport_set_pan_key(CanvasViewport* v, gboolean held) {
    if (v->pan_key == held) return;
    v->pan_key = held;
    update_cursor(v);
}
