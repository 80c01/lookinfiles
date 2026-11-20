/* Vim's "~" filler lines below the end of a text view's buffer (see
 * filler_lines.h) */
#include "widgets/filler_lines.h"
#include "theme/palette.h"

#define FILLER_LINES_KEY "lookin-filler-lines"

typedef struct {
    GtkWidget* view;
    GtkWidget* area;
} FillerLines;

static FillerLines* of_view(GtkWidget* view) {
    return view != NULL ? g_object_get_data(G_OBJECT(view), FILLER_LINES_KEY) : NULL;
}

static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    (void)width;
    FillerLines* f = data;
    if (!gtk_widget_get_mapped(f->view)) return;
    GtkTextView* tv = GTK_TEXT_VIEW(f->view);
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(tv);

    /* The screen line after the last one of the buffer, and its height */
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    GdkRectangle last;
    gtk_text_view_get_iter_location(tv, &end, &last);
    if (last.height <= 0) return;
    gint wx = 0, wy = 0;
    gtk_text_view_buffer_to_window_coords(tv, GTK_TEXT_WINDOW_WIDGET, 0, last.y + last.height, &wx, &wy);
    /* In the first column of the view, over the gutter, as Vim draws them
     * whatever the number column or the horizontal scroll */
    graphene_point_t p = GRAPHENE_POINT_INIT(0.0f, (float)wy);
    graphene_point_t q;
    if (!gtk_widget_compute_point(f->view, GTK_WIDGET(area), &p, &q)) return;

    /* Not under the view's bottom edge (a horizontal scrollbar) */
    graphene_point_t bottom = GRAPHENE_POINT_INIT(0.0f, (float)gtk_widget_get_height(f->view));
    graphene_point_t qb;
    double max_y = height;
    if (gtk_widget_compute_point(f->view, GTK_WIDGET(area), &bottom, &qb)) max_y = MIN(max_y, qb.y);

    PangoLayout* layout = gtk_widget_create_pango_layout(f->view, "~");
    double r, g, b;
    palette_rgb(PAL_ACCENT_FG, &r, &g, &b);
    cairo_set_source_rgb(cr, r, g, b);
    for (double y = q.y; y + last.height <= max_y; y += last.height) {
        if (y < 0) continue;
        cairo_move_to(cr, q.x, y);
        pango_cairo_show_layout(cr, layout);
    }
    g_object_unref(layout);
}

/* Scrolled, resized, edited or relaid out: redraw. Connected swapped: the
 * first argument is the drawing area. */
static void queue_redraw(GtkWidget* area) {
    if (gtk_widget_get_visible(area)) gtk_widget_queue_draw(area);
}

static void watch_adjustment(FillerLines* f, GtkAdjustment* adj) {
    if (adj == NULL) return;
    g_signal_connect_object(adj, "value-changed", G_CALLBACK(queue_redraw), f->area, G_CONNECT_SWAPPED);
    g_signal_connect_object(adj, "changed", G_CALLBACK(queue_redraw), f->area, G_CONNECT_SWAPPED);
}

GtkWidget* filler_lines_wrap(GtkWidget* child, GtkWidget* view) {
    FillerLines* f = g_new0(FillerLines, 1);
    f->view = view;
    f->area = gtk_drawing_area_new();
    gtk_widget_set_can_target(f->area, FALSE);
    gtk_widget_set_can_focus(f->area, FALSE);
    gtk_widget_set_visible(f->area, FALSE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(f->area), on_draw, f, NULL);
    g_object_set_data_full(G_OBJECT(view), FILLER_LINES_KEY, f, g_free);

    GtkWidget* overlay = gtk_overlay_new();
    gtk_widget_set_hexpand(overlay, TRUE);
    gtk_widget_set_vexpand(overlay, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), child);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), f->area);

    GtkScrollable* s = GTK_SCROLLABLE(view);
    watch_adjustment(f, gtk_scrollable_get_vadjustment(s));
    /* A short text does not change the adjustments when lines come and go */
    g_signal_connect_object(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), "changed", G_CALLBACK(queue_redraw),
                            f->area, G_CONNECT_SWAPPED);
    return overlay;
}

void filler_lines_set(GtkWidget* view, gboolean shown) {
    FillerLines* f = of_view(view);
    if (f == NULL) return;
    gtk_widget_set_visible(f->area, shown);
    gtk_widget_queue_draw(f->area);
}
