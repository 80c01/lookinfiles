#ifndef CANVAS_DRAG_H
#define CANVAS_DRAG_H

#include "canvas/canvas_item.h"

#include <gtk/gtk.h>

/* The pointer on the Canvas page (see canvas_view.h): the drawing tools,
 * moving and resizing blocks with snapping, clicks on the page, the pointer
 * position and dropped image files. Controllers added to the page surface. */
void canvas_drag_attach(CanvasPage* p);

/* Remove point index (not an end) of the line or arrow it, selected alone,
 * with undo; FALSE when it cannot */
gboolean canvas_drag_remove_point(CanvasPage* p, CanvasItem* it, guint index);

#endif /* CANVAS_DRAG_H */
