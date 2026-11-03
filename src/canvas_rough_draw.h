#ifndef CANVAS_ROUGH_DRAW_H
#define CANVAS_ROUGH_DRAW_H

#include "canvas_doc.h"
#include <cairo.h>

/* Render a Canvas shape block with Cairo (hand-drawn sketch or clean style) */
void canvas_rough_draw_shape(cairo_t* cr, const CanvasBlock* b, gint width, gint height);

#endif /* CANVAS_ROUGH_DRAW_H */
