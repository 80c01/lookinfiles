/* Shapes drawn with the mouse on the Canvas page (see canvas_draw.h) */
#include "canvas/canvas_draw.h"
#include "canvas/canvas_geometry.h"

#include <math.h>

#define DEFAULT_W 160      /* shape made by a click */
#define DEFAULT_H 100
#define CLICK_SIZE 6       /* a drag smaller than this is a click */
#define PENCIL_STEP 2.0    /* pencil points closer than this are dropped */

static gboolean uses_points(const CanvasItem* it) {
    return it->shape_type == CANVAS_SHAPE_LINE || it->shape_type == CANVAS_SHAPE_ARROW ||
           it->shape_type == CANVAS_SHAPE_FREEHAND;
}

static void ensure_points(CanvasItem* it) {
    if (it->points == NULL) it->points = g_array_new(FALSE, FALSE, sizeof(CanvasPoint));
}

static void redraw(CanvasItem* it) {
    canvas_item_place(it);
    if (it->content != NULL) gtk_widget_queue_draw(it->content);
}

/* The block around the points (page coordinates), CANVAS_LINE_PAD away;
 * for a line or an arrow, around the curve through them */
static void fit_points(CanvasItem* it) {
    if (it->points == NULL || it->points->len == 0) return;
    gdouble x0, y0, x1, y1;
    if (it->shape_type != CANVAS_SHAPE_FREEHAND) {
        canvas_curve_bounds((const gdouble*)it->points->data, it->points->len, &x0, &y0, &x1, &y1);
    } else {
        const CanvasPoint* p0 = &g_array_index(it->points, CanvasPoint, 0);
        x0 = x1 = p0->x;
        y0 = y1 = p0->y;
        for (guint i = 1; i < it->points->len; i++) {
            const CanvasPoint* p = &g_array_index(it->points, CanvasPoint, i);
            x0 = MIN(x0, p->x);
            y0 = MIN(y0, p->y);
            x1 = MAX(x1, p->x);
            y1 = MAX(y1, p->y);
        }
    }
    it->x = (gint)floor(x0) - CANVAS_LINE_PAD;
    it->y = (gint)floor(y0) - CANVAS_LINE_PAD;
    it->w = (gint)ceil(x1 - floor(x0)) + 2 * CANVAS_LINE_PAD;
    it->h = (gint)ceil(y1 - floor(y0)) + 2 * CANVAS_LINE_PAD;
}

/* Points made relative to the block, so the note does not depend on where
 * it was drawn */
static void points_to_block(CanvasItem* it) {
    for (guint i = 0; i < it->points->len; i++) {
        CanvasPoint* p = &g_array_index(it->points, CanvasPoint, i);
        p->x -= it->x;
        p->y -= it->y;
    }
}

void canvas_draw_update(CanvasItem* it, gdouble x0, gdouble y0, gdouble x1, gdouble y1, gboolean shift) {
    if (it == NULL || it->kind != CANVAS_BLOCK_SHAPE) return;
    if (uses_points(it)) {
        if (shift) canvas_snap_angle(x0, y0, &x1, &y1);
        ensure_points(it);
        g_array_set_size(it->points, 0);
        CanvasPoint a = {x0, y0};
        CanvasPoint b = {x1, y1};
        g_array_append_val(it->points, a);
        g_array_append_val(it->points, b);
        fit_points(it);
    } else {
        CanvasRect r;
        canvas_drag_box(x0, y0, x1, y1, shift, &r);
        it->x = r.x;
        it->y = r.y;
        it->w = MAX(r.w, 1);
        it->h = MAX(r.h, 1);
    }
    redraw(it);
}

void canvas_draw_add_point(CanvasItem* it, gdouble x, gdouble y) {
    if (it == NULL || it->kind != CANVAS_BLOCK_SHAPE) return;
    ensure_points(it);
    if (it->points->len > 0) {
        const CanvasPoint* last = &g_array_index(it->points, CanvasPoint, it->points->len - 1);
        if (hypot(x - last->x, y - last->y) < PENCIL_STEP) return;
    }
    CanvasPoint p = {x, y};
    g_array_append_val(it->points, p);
    fit_points(it);
    redraw(it);
}

void canvas_draw_finish(CanvasItem* it) {
    if (it == NULL || it->kind != CANVAS_BLOCK_SHAPE) return;
    if (uses_points(it)) {
        ensure_points(it);
        if (it->points->len == 0) return;
        CanvasPoint first = g_array_index(it->points, CanvasPoint, 0);
        CanvasPoint last = g_array_index(it->points, CanvasPoint, it->points->len - 1);
        if (it->shape_type == CANVAS_SHAPE_FREEHAND) {
            /* A single point: a dot */
            if (it->points->len == 1) g_array_append_val(it->points, first);
        } else if (hypot(last.x - first.x, last.y - first.y) < CLICK_SIZE) {
            CanvasPoint end = {first.x + DEFAULT_W, first.y};
            g_array_index(it->points, CanvasPoint, it->points->len - 1) = end;
        }
        fit_points(it);
        points_to_block(it);
    } else if (it->w < CLICK_SIZE && it->h < CLICK_SIZE) {
        it->w = DEFAULT_W;
        it->h = DEFAULT_H;
    } else {
        it->w = MAX(it->w, MIN_SHAPE_SIZE);
        it->h = MAX(it->h, MIN_SHAPE_SIZE);
    }
    redraw(it);
}

gboolean canvas_draw_is_line(const CanvasItem* it) {
    return it != NULL && it->kind == CANVAS_BLOCK_SHAPE &&
           (it->shape_type == CANVAS_SHAPE_LINE || it->shape_type == CANVAS_SHAPE_ARROW);
}

/* Same placement as the drawing (canvas_rough_draw.c): the bounding box of
 * the curve through the points fills the block, CANVAS_LINE_PAD from its
 * sides */
GArray* canvas_draw_get_points(const CanvasItem* it) {
    if (!canvas_draw_is_line(it) || it->points == NULL || it->points->len < 2) return NULL;
    guint n = it->points->len;
    gdouble bx0, by0, bx1, by1;
    canvas_curve_bounds((const gdouble*)it->points->data, n, &bx0, &by0, &bx1, &by1);
    gdouble pad = CANVAS_LINE_PAD;
    gdouble tw = MAX(it->w - 2 * pad, 0), th = MAX(it->h - 2 * pad, 0);
    GArray* out = g_array_sized_new(FALSE, FALSE, sizeof(CanvasPoint), n);
    for (guint i = 0; i < n; i++) {
        const CanvasPoint* a = &g_array_index(it->points, CanvasPoint, i);
        CanvasPoint q = {it->x + canvas_map_coord(a->x, bx0, bx1 - bx0, pad, tw),
                         it->y + canvas_map_coord(a->y, by0, by1 - by0, pad, th)};
        g_array_append_val(out, q);
    }
    return out;
}

gboolean canvas_draw_get_ends(const CanvasItem* it, gdouble* x0, gdouble* y0, gdouble* x1, gdouble* y1) {
    GArray* pts = canvas_draw_get_points(it);
    if (pts == NULL) return FALSE;
    const CanvasPoint* a = &g_array_index(pts, CanvasPoint, 0);
    const CanvasPoint* b = &g_array_index(pts, CanvasPoint, pts->len - 1);
    *x0 = a->x;
    *y0 = a->y;
    *x1 = b->x;
    *y1 = b->y;
    g_array_unref(pts);
    return TRUE;
}

void canvas_draw_set_points(CanvasItem* it, const GArray* pts) {
    if (!canvas_draw_is_line(it) || pts == NULL || pts->len < 2) return;
    ensure_points(it);
    g_array_set_size(it->points, 0);
    g_array_append_vals(it->points, pts->data, pts->len);
    fit_points(it);
    points_to_block(it);
    redraw(it);
}
