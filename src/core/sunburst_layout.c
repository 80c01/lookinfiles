/* Geometry of a sunburst (see sunburst_layout.h) */
#include "core/sunburst_layout.h"

#include <math.h>

SunburstFrame sunburst_frame(gdouble width, gdouble height, guint rings, gdouble margin) {
    SunburstFrame f;
    f.cx = width / 2.0;
    f.cy = height / 2.0;
    f.rings = MAX(rings, 1);
    gdouble radius = MAX(MIN(width, height) / 2.0 - margin, 1.0);
    /* The hole takes as much as one ring and a half */
    f.ring = radius / ((gdouble)f.rings + 1.5);
    f.hole = radius - f.ring * (gdouble)f.rings;
    return f;
}

void sunburst_split(const gdouble* weights, guint n, gdouble a0, gdouble a1, gdouble* starts) {
    gdouble total = 0.0;
    for (guint i = 0; i < n; i++) total += weights[i] > 0.0 ? weights[i] : 0.0;
    gdouble a = a0;
    starts[0] = a0;
    for (guint i = 0; i < n; i++) {
        if (total > 0.0 && weights[i] > 0.0) a += (a1 - a0) * weights[i] / total;
        starts[i + 1] = i + 1 == n && total > 0.0 ? a1 : a;
    }
}

gboolean sunburst_locate(const SunburstFrame* f, gdouble x, gdouble y, gint* ring, gdouble* angle) {
    gdouble dx = x - f->cx, dy = y - f->cy;
    gdouble r = sqrt(dx * dx + dy * dy);
    gdouble a = atan2(dx, -dy); /* 0 at the top, clockwise */
    if (a < 0.0) a += 2.0 * G_PI;
    if (angle != NULL) *angle = a;
    if (r < f->hole) {
        if (ring != NULL) *ring = -1;
        return TRUE;
    }
    gint k = (gint)((r - f->hole) / f->ring);
    if (k >= (gint)f->rings) return FALSE;
    if (ring != NULL) *ring = k;
    return TRUE;
}

void sunburst_ring_radii(const SunburstFrame* f, guint ring, gdouble* inner, gdouble* outer) {
    if (inner != NULL) *inner = f->hole + f->ring * (gdouble)ring;
    if (outer != NULL) *outer = f->hole + f->ring * (gdouble)(ring + 1);
}
