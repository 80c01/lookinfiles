#include "canvas_item_shape.h"
#include "canvas_rough_draw.h"

#define SHAPE_DATA_KEY "canvas-shape-block"

static void on_shape_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)area;
    const CanvasBlock* b = user_data;
    if (b != NULL) {
        canvas_rough_draw_shape(cr, b, width, height);
    }
}

GtkWidget* canvas_item_shape_new(const CanvasBlock* b) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), MAX(b->w, 20));
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), MAX(b->h, 20));
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_shape_draw, (gpointer)b, NULL);
    g_object_set_data((GObject*)area, SHAPE_DATA_KEY, (gpointer)b);
    return area;
}

void canvas_item_shape_queue_draw(GtkWidget* shape_widget) {
    if (shape_widget != NULL) {
        gtk_widget_queue_draw(shape_widget);
    }
}
