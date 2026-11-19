#ifndef SUNBURST_LAYOUT_H
#define SUNBURST_LAYOUT_H

/* Geometry of a sunburst (the wheel of the Disk Usage tab): a hole at the
 * centre, then rings of equal width, each entry an arc whose angle is its
 * share of its parent's. Angles in radians, 0 at the top, clockwise. Pure,
 * unit-tested. */
#include <glib.h>

typedef struct {
    gdouble cx, cy;  /* centre */
    gdouble hole;    /* radius of the hole */
    gdouble ring;    /* width of a ring */
    guint rings;     /* rings drawn */
} SunburstFrame;

/* The frame filling a width x height area (a margin all around) */
SunburstFrame sunburst_frame(gdouble width, gdouble height, guint rings, gdouble margin);

/* Split [a0, a1] among n weights: starts[i] to starts[i + 1] for weight i
 * (starts has n + 1 values; weights <= 0 get nothing) */
void sunburst_split(const gdouble* weights, guint n, gdouble a0, gdouble a1, gdouble* starts);

/* Where (x, y) falls: *ring -1 in the hole, 0 the first ring...; *angle
 * from the top, clockwise, in [0, 2 pi). FALSE outside the last ring. */
gboolean sunburst_locate(const SunburstFrame* f, gdouble x, gdouble y, gint* ring, gdouble* angle);

/* Inner and outer radius of ring */
void sunburst_ring_radii(const SunburstFrame* f, guint ring, gdouble* inner, gdouble* outer);

#endif /* SUNBURST_LAYOUT_H */
