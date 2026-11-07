#ifndef CANVAS_ROUGH_DRAW_H
#define CANVAS_ROUGH_DRAW_H

#include "canvas_doc.h"
#include <cairo.h>

/* Render a Canvas shape block with Cairo (hand-drawn sketch or clean style) */
void canvas_rough_draw_shape(cairo_t* cr, const CanvasBlock* b, gint width, gint height);

/* Handles of a selected line or arrow, drawn over it: its points and the
 * middles of its segments; point active filled (-1: none) */
void canvas_rough_draw_line_handles(cairo_t* cr, const CanvasBlock* b, gint width, gint height, gint active);

#endif /* CANVAS_ROUGH_DRAW_H */
