#ifndef TREEMAP_LAYOUT_H
#define TREEMAP_LAYOUT_H

/* Layout of a treemap: rectangles whose areas are proportional to weights,
 * as close to squares as can be (the "squarified" layout of Bruls, Huizing
 * and van Wijk). Pure geometry, no widgets. */
#include <glib.h>

typedef struct {
    gdouble x, y, w, h;
} TreemapRect;

/* Lay out n weights (largest first, each > 0) in area: out[i] gets the
 * rectangle of weights[i]. The rectangles tile area without overlapping.
 * A weight <= 0, or an empty area, gets an empty rectangle. */
void treemap_squarify(const gdouble* weights, guint n, TreemapRect area, TreemapRect* out);

/* Is (x, y) inside r? (left and top edges inside, right and bottom out) */
gboolean treemap_rect_contains(const TreemapRect* r, gdouble x, gdouble y);

#endif /* TREEMAP_LAYOUT_H */
