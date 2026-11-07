#ifndef CANVAS_DRAG_H
#define CANVAS_DRAG_H

#include <gtk/gtk.h>

typedef struct CanvasPage CanvasPage;

/* The pointer on the Canvas page (see canvas_view.h): the drawing tools,
 * moving and resizing blocks with snapping, clicks on the page, the pointer
 * position and dropped image files. Controllers added to the page surface. */
void canvas_drag_attach(CanvasPage* p);

#endif /* CANVAS_DRAG_H */
