#ifndef CANVAS_FLOAT_H
#define CANVAS_FLOAT_H

/* A bar floating over the Canvas page, next to one block: just above it
 * (below it when the tool bar is in the way), clear of the other floating
 * panels, following it each frame while it moves, the view scrolls or
 * zooms. Shared by the text format bar and the
 * table bar; also gives them the same look (.canvas-float-bar, and
 * .canvas-fmt for their buttons). */
#include <gtk/gtk.h>

typedef struct CanvasFloat CanvasFloat;

/* box: the bar, hidden until shown; add it as an overlay of the GtkOverlay
 * over the page */
CanvasFloat* canvas_float_new(GtkWidget* box);
void canvas_float_free(CanvasFloat* f);

/* Show the bar next to target (a block frame on the page); NULL hides it */
void canvas_float_show_for(CanvasFloat* f, GtkWidget* target);

/* A button of the bar; it never takes the focus, the block keeps it */
GtkWidget* canvas_float_button(GtkWidget* child, const gchar* tip, GCallback cb, gpointer data);
void canvas_float_add_separator(GtkWidget* bar);

#endif /* CANVAS_FLOAT_H */
