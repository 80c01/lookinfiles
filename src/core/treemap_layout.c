/* Layout of a treemap (see treemap_layout.h) */
#include "core/treemap_layout.h"

/* Worst aspect ratio of the areas [start, end) laid in a row along side */
static gdouble worst_ratio(const gdouble* areas, guint start, guint end, gdouble side) {
    gdouble sum = 0.0, max = 0.0, min = G_MAXDOUBLE;
    for (guint i = start; i < end; i++) {
        sum += areas[i];
        max = MAX(max, areas[i]);
        min = MIN(min, areas[i]);
    }
    if (sum <= 0.0 || min <= 0.0 || side <= 0.0) return G_MAXDOUBLE;
    gdouble s2 = side * side;
    gdouble sum2 = sum * sum;
    return MAX(s2 * max / sum2, sum2 / (s2 * min));
}

void treemap_squarify(const gdouble* weights, guint n, TreemapRect area, TreemapRect* out) {
    if (n == 0 || out == NULL) return;
    gdouble total = 0.0;
    for (guint i = 0; i < n; i++) {
        out[i] = (TreemapRect){ area.x, area.y, 0.0, 0.0 };
        if (weights[i] > 0.0) total += weights[i];
    }
    if (total <= 0.0 || area.w <= 0.0 || area.h <= 0.0) return;

    /* The weights as areas, the empty ones left out */
    gdouble* areas = g_new(gdouble, n);
    guint* index = g_new(guint, n);
    guint m = 0;
    gdouble scale = area.w * area.h / total;
    for (guint i = 0; i < n; i++) {
        if (weights[i] <= 0.0) continue;
        areas[m] = weights[i] * scale;
        index[m] = i;
        m++;
    }

    TreemapRect r = area;
    guint i = 0;
    while (i < m) {
        /* A row along the shorter side grows while its worst ratio improves */
        gdouble side = MIN(r.w, r.h);
        guint end = i + 1;
        while (end < m && worst_ratio(areas, i, end + 1, side) <= worst_ratio(areas, i, end, side)) end++;
        gdouble sum = 0.0;
        for (guint k = i; k < end; k++) sum += areas[k];
        if (end == m) sum = r.w * r.h; /* the last row takes what is left (rounding) */
        if (r.w >= r.h) {
            /* A column on the left */
            gdouble cw = r.h > 0.0 ? sum / r.h : 0.0;
            gdouble y = r.y;
            for (guint k = i; k < end; k++) {
                gdouble h = k + 1 == end ? r.y + r.h - y : (cw > 0.0 ? areas[k] / cw : 0.0);
                out[index[k]] = (TreemapRect){ r.x, y, cw, h };
                y += h;
            }
            r.x += cw;
            r.w -= cw;
        } else {
            /* A row on the top */
            gdouble rh = r.w > 0.0 ? sum / r.w : 0.0;
            gdouble x = r.x;
            for (guint k = i; k < end; k++) {
                gdouble w = k + 1 == end ? r.x + r.w - x : (rh > 0.0 ? areas[k] / rh : 0.0);
                out[index[k]] = (TreemapRect){ x, r.y, w, rh };
                x += w;
            }
            r.y += rh;
            r.h -= rh;
        }
        i = end;
    }
    g_free(areas);
    g_free(index);
}

gboolean treemap_rect_contains(const TreemapRect* r, gdouble x, gdouble y) {
    return r != NULL && x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}
