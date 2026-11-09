#ifndef CANVAS_VIEWPORT_H
#define CANVAS_VIEWPORT_H

#include <gtk/gtk.h>

/* The scrolled view of a Canvas page, zoomed and panned.
 *
 * The page (surface) is drawn scaled; its own coordinates stay page
 * coordinates, so the pointer positions its controllers get need no change.
 * - Ctrl+wheel zooms around the pointer (10 % to 400 %).
 * - A drag with the middle button, or with the left one while the pan key
 *   (Space) is held, moves the view. */

#define CANVAS_ZOOM_MIN 0.1
#define CANVAS_ZOOM_MAX 4.0

typedef struct CanvasViewport CanvasViewport;

/* Called when the scale changes */
typedef void (*CanvasZoomNotify)(gdouble scale, gpointer data);

CanvasViewport* canvas_viewport_new(GtkWidget* surface, CanvasZoomNotify on_zoom, gpointer data);
void canvas_viewport_free(CanvasViewport* v);

/* The scrolled window to put in the page */
GtkWidget* canvas_viewport_get_widget(CanvasViewport* v);

gdouble canvas_viewport_get_scale(CanvasViewport* v);

/* step > 0: zoom in, < 0: out, around the view centre; 0: back to 100 % */
void canvas_viewport_zoom_step(CanvasViewport* v, gint step);

/* Back to 100 %, at the top-left corner of the page (another note opened) */
void canvas_viewport_reset(CanvasViewport* v);

/* Room above the page, in view pixels (not zoomed), so that its top is not
 * under the floating tool bar at first */
void canvas_viewport_set_top_inset(CanvasViewport* v, gint inset);

/* Page point at the top-left corner of the view, below the top inset */
void canvas_viewport_origin(CanvasViewport* v, gint* x, gint* y);

/* The pan key (Space) is held down / released */
void canvas_viewport_set_pan_key(CanvasViewport* v, gboolean held);

#endif /* CANVAS_VIEWPORT_H */
