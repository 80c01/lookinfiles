#ifndef CANVAS_DRAW_H
#define CANVAS_DRAW_H

/* Shapes drawn with the mouse on the Canvas page: the shape block follows
 * the pointer while the button is held, then gets its final geometry. */
#include "canvas_item.h"

/* Shape it dragged from (x0, y0) to (x1, y1), page coordinates. shift:
 * square / circle, or a line turned to a multiple of 45 degrees. */
void canvas_draw_update(CanvasItem* it, gdouble x0, gdouble y0, gdouble x1, gdouble y1, gboolean shift);

/* Pencil stroke it: one more point (page coordinates) */
void canvas_draw_add_point(CanvasItem* it, gdouble x, gdouble y);

/* The button is released: a click without a drag gives a shape of the
 * default size; points become relative to the block */
void canvas_draw_finish(CanvasItem* it);

/* A line or an arrow: TRUE if it is one */
gboolean canvas_draw_is_line(const CanvasItem* it);

/* Ends of a line or an arrow, page coordinates, as drawn; FALSE if it is not
 * one */
gboolean canvas_draw_get_ends(const CanvasItem* it, gdouble* x0, gdouble* y0, gdouble* x1, gdouble* y1);

/* Move the ends of a line or an arrow (page coordinates); its block fits
 * them again */
void canvas_draw_set_ends(CanvasItem* it, gdouble x0, gdouble y0, gdouble x1, gdouble y1);

#endif /* CANVAS_DRAW_H */
