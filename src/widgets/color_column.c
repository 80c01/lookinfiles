/* Vertical line at a column of a text view (see color_column.h) */
#include "widgets/color_column.h"
#include "theme/palette.h"

#define COLOR_COLUMN_KEY "lookin-color-column"
#define LINE_WIDTH 2.0

typedef struct {
    GtkWidget* view;
    GtkWidget* area;
    gint column;
} ColorColumn;

static ColorColumn* of_view(GtkWidget* view) {
    return view != NULL ? g_object_get_data(G_OBJECT(view), COLOR_COLUMN_KEY) : NULL;
}

/* Width of one character of the view's font, in pixels */
static double char_width(GtkWidget* view) {
    PangoLayout* layout = gtk_widget_create_pango_layout(view, "0000000000");
    PangoRectangle logical;
    pango_layout_get_extents(layout, NULL, &logical);
    g_object_unref(layout);
    return (double)logical.width / PANGO_SCALE / 10.0;
}

static void on_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer data) {
    (void)width;
    (void)height;
    ColorColumn* c = data;
    if (c->column <= 0 || !gtk_widget_get_mapped(c->view)) return;
    GtkTextView* tv = GTK_TEXT_VIEW(c->view);

    /* Start of the text: the first character's x, margins included */
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(gtk_text_view_get_buffer(tv), &start);
    GdkRectangle loc;
    gtk_text_view_get_iter_location(tv, &start, &loc);
    gint bx = loc.x + (gint)(char_width(c->view) * (c->column - 1) + 0.5);
    gint wx = 0, wy = 0;
    gtk_text_view_buffer_to_window_coords(tv, GTK_TEXT_WINDOW_WIDGET, bx, 0, &wx, &wy);

    /* Not over the line numbers */
    GtkWidget* gutter = gtk_text_view_get_gutter(tv, GTK_TEXT_WINDOW_LEFT);
    gint left = gutter != NULL && gtk_widget_get_visible(gutter) ? gtk_widget_get_width(gutter) : 0;
    if (wx < left || wx > gtk_widget_get_width(c->view)) return;

    graphene_point_t p = GRAPHENE_POINT_INIT((float)wx, 0.0f);
    graphene_point_t q;
    if (!gtk_widget_compute_point(c->view, GTK_WIDGET(area), &p, &q)) return;
    double r, g, b;
    palette_rgb(PAL_EDITOR_RULER, &r, &g, &b);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_rectangle(cr, q.x - LINE_WIDTH / 2.0, q.y, LINE_WIDTH, gtk_widget_get_height(c->view));
    cairo_fill(cr);
}

/* Scrolled, resized or relaid out (font, wrap, line numbers): redraw.
 * Connected swapped: the first argument is the drawing area. */
static void on_adjustment(GtkWidget* area) {
    ColorColumn* c = g_object_get_data(G_OBJECT(area), COLOR_COLUMN_KEY);
    if (c != NULL && c->column > 0) gtk_widget_queue_draw(area);
}

static void watch_adjustment(ColorColumn* c, GtkAdjustment* adj) {
    if (adj == NULL) return;
    g_signal_connect_object(adj, "value-changed", G_CALLBACK(on_adjustment), c->area, G_CONNECT_SWAPPED);
    g_signal_connect_object(adj, "changed", G_CALLBACK(on_adjustment), c->area, G_CONNECT_SWAPPED);
}

GtkWidget* color_column_wrap(GtkWidget* scroll, GtkWidget* view) {
    ColorColumn* c = g_new0(ColorColumn, 1);
    c->view = view;
    c->area = gtk_drawing_area_new();
    gtk_widget_set_can_target(c->area, FALSE);
    gtk_widget_set_can_focus(c->area, FALSE);
    gtk_widget_set_visible(c->area, FALSE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(c->area), on_draw, c, NULL);
    g_object_set_data(G_OBJECT(c->area), COLOR_COLUMN_KEY, c);
    g_object_set_data_full(G_OBJECT(view), COLOR_COLUMN_KEY, c, g_free);

    GtkWidget* overlay = gtk_overlay_new();
    gtk_widget_set_hexpand(overlay, TRUE);
    gtk_widget_set_vexpand(overlay, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), scroll);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), c->area);

    GtkScrollable* s = GTK_SCROLLABLE(view);
    watch_adjustment(c, gtk_scrollable_get_hadjustment(s));
    watch_adjustment(c, gtk_scrollable_get_vadjustment(s));
    return overlay;
}

void color_column_set(GtkWidget* view, gint column) {
    ColorColumn* c = of_view(view);
    if (c == NULL) return;
    c->column = column > 0 ? column : 0;
    gtk_widget_set_visible(c->area, c->column > 0);
    gtk_widget_queue_draw(c->area);
}

gint color_column_get(GtkWidget* view) {
    ColorColumn* c = of_view(view);
    return c != NULL ? c->column : 0;
}

static void on_buffer_changed(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    color_column_set(data, 0);
}

void color_column_hide_on_change(GtkWidget* view) {
    if (of_view(view) == NULL) return;
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    g_signal_connect_object(buffer, "changed", G_CALLBACK(on_buffer_changed), view, 0);
}
