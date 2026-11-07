#include "canvas_geometry.h"
#include <math.h>
#include <stdlib.h>

gint canvas_snap_coord(gint val, gint grid) {
    if (grid <= 1) return val;
    return (val >= 0) ? ((val + grid / 2) / grid) * grid : ((val - grid / 2) / grid) * grid;
}

void canvas_snap_box(gint* x, gint* y, gint w, gint h,
                     const CanvasRect* others, guint count,
                     gboolean snap_grid, gint grid,
                     CanvasGuides* guides) {
    if (guides != NULL) {
        guides->active_x = FALSE;
        guides->guide_x = 0;
        guides->active_y = FALSE;
        guides->guide_y = 0;
    }
    if (x == NULL || y == NULL) return;

    gint best_dx = CANVAS_SNAP_THRESHOLD + 1;
    gint snap_x = *x;
    gint guide_x = 0;

    gint best_dy = CANVAS_SNAP_THRESHOLD + 1;
    gint snap_y = *y;
    gint guide_y = 0;

    /* Smart alignment against other blocks */
    for (guint i = 0; i < count; i++) {
        const CanvasRect* o = &others[i];

        /* Align Left to Left */
        gint d = abs(*x - o->x);
        if (d < best_dx) { best_dx = d; snap_x = o->x; guide_x = o->x; }
        /* Align Left to Right */
        d = abs(*x - (o->x + o->w));
        if (d < best_dx) { best_dx = d; snap_x = o->x + o->w; guide_x = o->x + o->w; }
        /* Align Center to Center */
        d = abs((*x + w / 2) - (o->x + o->w / 2));
        if (d < best_dx) { best_dx = d; snap_x = (o->x + o->w / 2) - w / 2; guide_x = o->x + o->w / 2; }
        /* Align Right to Left */
        d = abs((*x + w) - o->x);
        if (d < best_dx) { best_dx = d; snap_x = o->x - w; guide_x = o->x; }
        /* Align Right to Right */
        d = abs((*x + w) - (o->x + o->w));
        if (d < best_dx) { best_dx = d; snap_x = o->x + o->w - w; guide_x = o->x + o->w; }

        /* Align Top to Top */
        d = abs(*y - o->y);
        if (d < best_dy) { best_dy = d; snap_y = o->y; guide_y = o->y; }
        /* Align Top to Bottom */
        d = abs(*y - (o->y + o->h));
        if (d < best_dy) { best_dy = d; snap_y = o->y + o->h; guide_y = o->y + o->h; }
        /* Align Middle to Middle */
        d = abs((*y + h / 2) - (o->y + o->h / 2));
        if (d < best_dy) { best_dy = d; snap_y = (o->y + o->h / 2) - h / 2; guide_y = o->y + o->h / 2; }
        /* Align Bottom to Top */
        d = abs((*y + h) - o->y);
        if (d < best_dy) { best_dy = d; snap_y = o->y - h; guide_y = o->y; }
        /* Align Bottom to Bottom */
        d = abs((*y + h) - (o->y + o->h));
        if (d < best_dy) { best_dy = d; snap_y = o->y + o->h - h; guide_y = o->y + o->h; }
    }

    if (best_dx <= CANVAS_SNAP_THRESHOLD) {
        *x = snap_x;
        if (guides != NULL) {
            guides->active_x = TRUE;
            guides->guide_x = guide_x;
        }
    } else if (snap_grid && grid > 1) {
        *x = canvas_snap_coord(*x, grid);
    }

    if (best_dy <= CANVAS_SNAP_THRESHOLD) {
        *y = snap_y;
        if (guides != NULL) {
            guides->active_y = TRUE;
            guides->guide_y = guide_y;
        }
    } else if (snap_grid && grid > 1) {
        *y = canvas_snap_coord(*y, grid);
    }
}

/* Indices of rects ordered by their left (vertical: top) edge, ties kept in
 * input order; the rects themselves stay in place so the caller can map
 * them back to its items */
static guint* sorted_indices(const CanvasRect* rects, guint count, gboolean vertical) {
    guint* order = g_new(guint, count);
    for (guint i = 0; i < count; i++) {
        guint j = i;
        gint key = vertical ? rects[i].y : rects[i].x;
        while (j > 0) {
            const CanvasRect* prev = &rects[order[j - 1]];
            if ((vertical ? prev->y : prev->x) <= key) break;
            order[j] = order[j - 1];
            j--;
        }
        order[j] = i;
    }
    return order;
}

/* Spread the rects between the leftmost and rightmost edges (vertical: top
 * and bottom) with equal gaps, keeping their order */
static void distribute(CanvasRect* rects, guint count, gboolean vertical, gint start, gint end) {
    guint* order = sorted_indices(rects, count, vertical);
    gint total = 0;
    for (guint i = 0; i < count; i++) total += vertical ? rects[i].h : rects[i].w;
    gint free_space = (end - start) - total;
    gint used = 0;
    for (guint k = 0; k < count; k++) {
        CanvasRect* r = &rects[order[k]];
        gint pos = start + used + (gint)((gint64)free_space * k / (gint)(count - 1));
        if (vertical) {
            r->y = pos;
            used += r->h;
        } else {
            r->x = pos;
            used += r->w;
        }
    }
    g_free(order);
}

void canvas_align_boxes(CanvasRect* rects, guint count, CanvasAlignOp op) {
    if (rects == NULL || count < 2) return;

    gint min_x = rects[0].x;
    gint max_r = rects[0].x + rects[0].w;
    gint min_y = rects[0].y;
    gint max_b = rects[0].y + rects[0].h;

    for (guint i = 1; i < count; i++) {
        if (rects[i].x < min_x) min_x = rects[i].x;
        if (rects[i].x + rects[i].w > max_r) max_r = rects[i].x + rects[i].w;
        if (rects[i].y < min_y) min_y = rects[i].y;
        if (rects[i].y + rects[i].h > max_b) max_b = rects[i].y + rects[i].h;
    }

    switch (op) {
    case CANVAS_ALIGN_LEFT:
        for (guint i = 0; i < count; i++) rects[i].x = min_x;
        break;
    case CANVAS_ALIGN_CENTER_H: {
        gint center = (min_x + max_r) / 2;
        for (guint i = 0; i < count; i++) rects[i].x = center - rects[i].w / 2;
        break;
    }
    case CANVAS_ALIGN_RIGHT:
        for (guint i = 0; i < count; i++) rects[i].x = max_r - rects[i].w;
        break;
    case CANVAS_ALIGN_TOP:
        for (guint i = 0; i < count; i++) rects[i].y = min_y;
        break;
    case CANVAS_ALIGN_MIDDLE_V: {
        gint middle = (min_y + max_b) / 2;
        for (guint i = 0; i < count; i++) rects[i].y = middle - rects[i].h / 2;
        break;
    }
    case CANVAS_ALIGN_BOTTOM:
        for (guint i = 0; i < count; i++) rects[i].y = max_b - rects[i].h;
        break;
    case CANVAS_DISTRIBUTE_H:
        distribute(rects, count, FALSE, min_x, max_r);
        break;
    case CANVAS_DISTRIBUTE_V:
        distribute(rects, count, TRUE, min_y, max_b);
        break;
    }
}

void canvas_drag_box(gdouble x0, gdouble y0, gdouble x1, gdouble y1, gboolean square, CanvasRect* out) {
    if (out == NULL) return;
    gdouble dx = x1 - x0;
    gdouble dy = y1 - y0;
    if (square) {
        gdouble side = MAX(fabs(dx), fabs(dy));
        dx = dx < 0 ? -side : side;
        dy = dy < 0 ? -side : side;
    }
    out->x = (gint)lround(MIN(x0, x0 + dx));
    out->y = (gint)lround(MIN(y0, y0 + dy));
    out->w = (gint)lround(fabs(dx));
    out->h = (gint)lround(fabs(dy));
}

void canvas_snap_angle(gdouble x0, gdouble y0, gdouble* x1, gdouble* y1) {
    if (x1 == NULL || y1 == NULL) return;
    gdouble dx = *x1 - x0;
    gdouble dy = *y1 - y0;
    gdouble len = hypot(dx, dy);
    if (len == 0) return;
    gdouble step = G_PI / 4.0;
    gdouble angle = round(atan2(dy, dx) / step) * step;
    gdouble c = cos(angle);
    gdouble s = sin(angle);
    /* Exact zeros: a horizontal line stays exactly horizontal */
    if (fabs(c) < 1e-9) c = 0;
    if (fabs(s) < 1e-9) s = 0;
    *x1 = x0 + len * c;
    *y1 = y0 + len * s;
}

gdouble canvas_angle_normalize(gdouble a) {
    a = fmod(a, 360.0);
    if (a < 0) a += 360.0;
    return a >= 360.0 ? 0.0 : a;
}

void canvas_rotate_point(gdouble cx, gdouble cy, gdouble angle, gdouble* x, gdouble* y) {
    if (x == NULL || y == NULL || angle == 0) return;
    gdouble r = angle * G_PI / 180.0;
    gdouble c = cos(r), s = sin(r);
    gdouble dx = *x - cx, dy = *y - cy;
    *x = cx + dx * c - dy * s;
    *y = cy + dx * s + dy * c;
}

void canvas_rotated_bounds(const CanvasRect* box, gdouble angle, CanvasRect* out) {
    if (box == NULL || out == NULL) return;
    if (canvas_angle_normalize(angle) == 0) {
        *out = *box;
        return;
    }
    gdouble cx = box->x + box->w / 2.0, cy = box->y + box->h / 2.0;
    gdouble x0 = G_MAXDOUBLE, y0 = G_MAXDOUBLE, x1 = -G_MAXDOUBLE, y1 = -G_MAXDOUBLE;
    for (guint k = 0; k < 4; k++) {
        gdouble x = box->x + ((k & 1) ? box->w : 0);
        gdouble y = box->y + ((k & 2) ? box->h : 0);
        canvas_rotate_point(cx, cy, angle, &x, &y);
        x0 = MIN(x0, x);
        y0 = MIN(y0, y);
        x1 = MAX(x1, x);
        y1 = MAX(y1, y);
    }
    /* Rounding errors (1e-12) do not grow the box by a pixel */
    out->x = (gint)floor(x0 + 1e-6);
    out->y = (gint)floor(y0 + 1e-6);
    out->w = (gint)ceil(x1 - 1e-6) - out->x;
    out->h = (gint)ceil(y1 - 1e-6) - out->y;
}

gdouble canvas_handle_angle(gdouble cx, gdouble cy, gdouble x, gdouble y, gboolean snap) {
    gdouble dx = x - cx, dy = y - cy;
    if (dx == 0 && dy == 0) return 0;
    /* The handle is straight above the centre at angle 0 */
    gdouble a = atan2(dx, -dy) * 180.0 / G_PI;
    if (snap) a = round(a / 15.0) * 15.0;
    return canvas_angle_normalize(a);
}

void canvas_resize_box(const CanvasRect* box, gdouble angle, gint sx, gint sy, gdouble dx, gdouble dy, gint min,
                       gboolean keep, CanvasRect* out) {
    if (box == NULL || out == NULL) return;
    sx = CLAMP(sx, -1, 1);
    sy = CLAMP(sy, -1, 1);
    /* The pointer move along the sides of the turned box */
    gdouble lx = dx, ly = dy;
    canvas_rotate_point(0, 0, -angle, &lx, &ly);
    gint w = MAX(box->w + sx * (gint)lround(lx), min);
    gint h = MAX(box->h + sy * (gint)lround(ly), min);
    if (keep && box->w > 0 && box->h > 0) {
        /* A side handle stretches the other side along */
        if (sx == 0) w = (gint)lround((gdouble)box->w * h / box->h);
        if (sy == 0) h = (gint)lround((gdouble)box->h * w / box->w);
        canvas_keep_ratio(box->w, box->h, min, &w, &h);
    }
    /* The opposite handle stays put on the page: the new centre from it */
    gdouble cx = box->x + box->w / 2.0, cy = box->y + box->h / 2.0;
    gdouble ax = cx - sx * box->w / 2.0, ay = cy - sy * box->h / 2.0;
    canvas_rotate_point(cx, cy, angle, &ax, &ay);
    gdouble ox = sx * w / 2.0, oy = sy * h / 2.0;
    canvas_rotate_point(0, 0, angle, &ox, &oy);
    out->x = (gint)lround(ax + ox - w / 2.0);
    out->y = (gint)lround(ay + oy - h / 2.0);
    out->w = w;
    out->h = h;
}

gdouble canvas_map_coord(gdouble v, gdouble from, gdouble from_len, gdouble to, gdouble to_len) {
    if (from_len <= 0) return to + to_len / 2.0;
    return to + (v - from) * to_len / from_len;
}

gboolean canvas_edge_point(CanvasOutline outline, const CanvasRect* box, gdouble tx, gdouble ty,
                           gdouble gap, gdouble* x, gdouble* y) {
    gdouble hw = MAX(box->w / 2.0, 1.0), hh = MAX(box->h / 2.0, 1.0);
    gdouble cx = box->x + box->w / 2.0, cy = box->y + box->h / 2.0;
    gdouble dx = tx - cx, dy = ty - cy;
    gdouble len = sqrt(dx * dx + dy * dy);
    if (len < 1e-9) return FALSE;
    /* t: the part of (dx, dy) from the centre to the outline */
    gdouble t;
    if (outline == CANVAS_OUTLINE_ELLIPSE) {
        t = 1.0 / sqrt((dx / hw) * (dx / hw) + (dy / hh) * (dy / hh));
    } else if (outline == CANVAS_OUTLINE_DIAMOND) {
        t = 1.0 / (fabs(dx) / hw + fabs(dy) / hh);
    } else {
        gdouble tx_ = fabs(dx) > 1e-9 ? hw / fabs(dx) : G_MAXDOUBLE;
        gdouble ty_ = fabs(dy) > 1e-9 ? hh / fabs(dy) : G_MAXDOUBLE;
        t = MIN(tx_, ty_);
    }
    *x = cx + dx * t + dx / len * gap;
    *y = cy + dy * t + dy / len * gap;
    return TRUE;
}

#define CURVE_SAMPLES 24 /* per segment, for bounds and distances */

static void curve_pt(const gdouble* xy, guint n, gint i, gdouble* x, gdouble* y) {
    i = CLAMP(i, 0, (gint)n - 1);
    *x = xy[2 * i];
    *y = xy[2 * i + 1];
}

void canvas_curve_controls(const gdouble* xy, guint n, guint i, gdouble c[4]) {
    gdouble x0, y0, x1, y1, x2, y2, x3, y3;
    curve_pt(xy, n, (gint)i - 1, &x0, &y0);
    curve_pt(xy, n, (gint)i, &x1, &y1);
    curve_pt(xy, n, (gint)i + 1, &x2, &y2);
    curve_pt(xy, n, (gint)i + 2, &x3, &y3);
    if (n <= 2) {
        /* A straight segment */
        c[0] = x1 + (x2 - x1) / 3.0;
        c[1] = y1 + (y2 - y1) / 3.0;
        c[2] = x1 + 2.0 * (x2 - x1) / 3.0;
        c[3] = y1 + 2.0 * (y2 - y1) / 3.0;
        return;
    }
    c[0] = x1 + (x2 - x0) / 6.0;
    c[1] = y1 + (y2 - y0) / 6.0;
    c[2] = x2 - (x3 - x1) / 6.0;
    c[3] = y2 - (y3 - y1) / 6.0;
}

void canvas_curve_point(const gdouble* xy, guint n, guint i, gdouble t, gdouble* x, gdouble* y) {
    if (n == 0) return;
    if (n == 1 || i + 1 >= n) {
        curve_pt(xy, n, (gint)MIN(i, n - 1), x, y);
        return;
    }
    gdouble c[4];
    canvas_curve_controls(xy, n, i, c);
    gdouble ax = xy[2 * i], ay = xy[2 * i + 1];
    gdouble bx = xy[2 * i + 2], by = xy[2 * i + 3];
    gdouble u = 1.0 - t;
    gdouble k0 = u * u * u, k1 = 3 * u * u * t, k2 = 3 * u * t * t, k3 = t * t * t;
    *x = k0 * ax + k1 * c[0] + k2 * c[2] + k3 * bx;
    *y = k0 * ay + k1 * c[1] + k2 * c[3] + k3 * by;
}

void canvas_curve_bounds(const gdouble* xy, guint n, gdouble* x0, gdouble* y0, gdouble* x1, gdouble* y1) {
    if (n == 0) {
        *x0 = *y0 = *x1 = *y1 = 0;
        return;
    }
    *x0 = *x1 = xy[0];
    *y0 = *y1 = xy[1];
    for (guint i = 0; i + 1 < n; i++) {
        for (gint s = 1; s <= CURVE_SAMPLES; s++) {
            gdouble x, y;
            canvas_curve_point(xy, n, i, (gdouble)s / CURVE_SAMPLES, &x, &y);
            *x0 = MIN(*x0, x);
            *y0 = MIN(*y0, y);
            *x1 = MAX(*x1, x);
            *y1 = MAX(*y1, y);
        }
    }
}

/* Distance from (px, py) to the segment (ax, ay) - (bx, by) */
static gdouble segment_distance(gdouble ax, gdouble ay, gdouble bx, gdouble by, gdouble px, gdouble py) {
    gdouble dx = bx - ax, dy = by - ay;
    gdouble len2 = dx * dx + dy * dy;
    gdouble t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0;
    t = CLAMP(t, 0.0, 1.0);
    return hypot(px - (ax + t * dx), py - (ay + t * dy));
}

gdouble canvas_curve_distance(const gdouble* xy, guint n, gdouble px, gdouble py) {
    if (n == 0) return G_MAXDOUBLE;
    gdouble best = hypot(px - xy[0], py - xy[1]);
    for (guint i = 0; i + 1 < n; i++) {
        gdouble ax = xy[2 * i], ay = xy[2 * i + 1];
        for (gint s = 1; s <= CURVE_SAMPLES; s++) {
            gdouble x, y;
            canvas_curve_point(xy, n, i, (gdouble)s / CURVE_SAMPLES, &x, &y);
            best = MIN(best, segment_distance(ax, ay, x, y, px, py));
            ax = x;
            ay = y;
        }
    }
    return best;
}

void canvas_keep_ratio(gint w0, gint h0, gint min, gint* w, gint* h) {
    if (w == NULL || h == NULL || w0 <= 0 || h0 <= 0) return;
    gdouble scale = MAX((gdouble)*w / w0, (gdouble)*h / h0);
    scale = MAX(scale, (gdouble)min / MIN(w0, h0));
    *w = MAX((gint)lround(w0 * scale), min);
    *h = MAX((gint)lround(h0 * scale), min);
}
