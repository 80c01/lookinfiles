#ifndef CANVAS_MENU_H
#define CANVAS_MENU_H

/* Context menu of the Canvas page: a right click on the page opens it at
 * the pointer. Paste (Ctrl+V) puts what the clipboard holds there: copied
 * blocks, a picture, or text as a new text box (greyed out when empty). */
#include <gtk/gtk.h>

typedef struct CanvasPage CanvasPage;

/* The right click on the page surface (after canvas_drag_attach()) */
void canvas_menu_attach(CanvasPage* p);

#endif /* CANVAS_MENU_H */
