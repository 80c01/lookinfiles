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

static gint compare_rect_x(gconstpointer a, gconstpointer b) {
    const CanvasRect* r1 = a;
    const CanvasRect* r2 = b;
    return r1->x - r2->x;
}

static gint compare_rect_y(gconstpointer a, gconstpointer b) {
    const CanvasRect* r1 = a;
    const CanvasRect* r2 = b;
    return r1->y - r2->y;
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
    case CANVAS_DISTRIBUTE_H: {
        qsort(rects, count, sizeof(CanvasRect), compare_rect_x);
        gint total_w = 0;
        for (guint i = 0; i < count; i++) total_w += rects[i].w;
        gint span = (rects[count - 1].x + rects[count - 1].w) - rects[0].x;
        gint free_space = span - total_w;
        gint gap = (count > 1) ? free_space / (gint)(count - 1) : 0;
        gint cur_x = rects[0].x;
        for (guint i = 0; i < count; i++) {
            rects[i].x = cur_x;
            cur_x += rects[i].w + gap;
        }
        break;
    }
    case CANVAS_DISTRIBUTE_V: {
        qsort(rects, count, sizeof(CanvasRect), compare_rect_y);
        gint total_h = 0;
        for (guint i = 0; i < count; i++) total_h += rects[i].h;
        gint span = (rects[count - 1].y + rects[count - 1].h) - rects[0].y;
        gint free_space = span - total_h;
        gint gap = (count > 1) ? free_space / (gint)(count - 1) : 0;
        gint cur_y = rects[0].y;
        for (guint i = 0; i < count; i++) {
            rects[i].y = cur_y;
            cur_y += rects[i].h + gap;
        }
        break;
    }
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
