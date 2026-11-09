#ifndef CANVAS_MENU_H
#define CANVAS_MENU_H

/* Context menu of the Canvas page: a right click on the page opens it at
 * the pointer. Paste Image (Ctrl+V) puts the picture of the clipboard
 * there, as a new picture block (greyed out without a picture). */
#include <gtk/gtk.h>

typedef struct CanvasPage CanvasPage;

/* The right click on the page surface (after canvas_drag_attach()) */
void canvas_menu_attach(CanvasPage* p);

#endif /* CANVAS_MENU_H */
