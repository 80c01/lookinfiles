#ifndef CANVAS_ITEM_SHAPE_H
#define CANVAS_ITEM_SHAPE_H

#include "canvas_doc.h"
#include <gtk/gtk.h>

/* Creates the drawing area widget for a shape block */
GtkWidget* canvas_item_shape_new(const CanvasBlock* b);

/* Redraw the shape widget when properties change */
void canvas_item_shape_queue_draw(GtkWidget* shape_widget);

#endif /* CANVAS_ITEM_SHAPE_H */
