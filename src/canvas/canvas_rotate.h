#ifndef CANVAS_ROTATE_H
#define CANVAS_ROTATE_H

/* Shapes turned on the Canvas page, as in Excalidraw: a round handle above
 * the shape selected alone (a rectangle, an ellipse or a diamond,
 * canvas_item_rotatable()) turns it around its centre while dragged; Shift
 * keeps the angle to multiples of 15 degrees. The handle is a child of the
 * page surface, on top of the blocks, turned with the shape; it follows it
 * each frame while shown. */
#include "canvas/canvas_item.h"

typedef struct CanvasRotate CanvasRotate;

CanvasRotate* canvas_rotate_new(CanvasPage* p);
void canvas_rotate_free(CanvasRotate* r);

/* Show the handle over the current block if it can be turned, or hide it
 * (after a change of focus, selection, tool or lock) */
void canvas_rotate_update(CanvasRotate* r);

/* Hide the handle (the blocks are about to be removed) */
void canvas_rotate_hide(CanvasRotate* r);

/* The block whose handle is at (x, y) on the page, NULL if none */
CanvasItem* canvas_rotate_handle_at(CanvasRotate* r, gdouble x, gdouble y);

/* Turn it so its handle points at (x, y); snap: multiples of 15 degrees */
void canvas_rotate_drag(CanvasItem* it, gdouble x, gdouble y, gboolean snap);

#endif /* CANVAS_ROTATE_H */
