/* Shapes drawn with the mouse on the Canvas page (see canvas_draw.h) */
#include "canvas_draw.h"
#include "canvas_geometry.h"

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

/* The block around the points (page coordinates), CANVAS_LINE_PAD away */
static void fit_points(CanvasItem* it) {
    if (it->points == NULL || it->points->len == 0) return;
    const CanvasPoint* p0 = &g_array_index(it->points, CanvasPoint, 0);
    gdouble x0 = p0->x, y0 = p0->y, x1 = p0->x, y1 = p0->y;
    for (guint i = 1; i < it->points->len; i++) {
        const CanvasPoint* p = &g_array_index(it->points, CanvasPoint, i);
        x0 = MIN(x0, p->x);
        y0 = MIN(y0, p->y);
        x1 = MAX(x1, p->x);
        y1 = MAX(y1, p->y);
    }
    it->x = (gint)floor(x0) - CANVAS_LINE_PAD;
    it->y = (gint)floor(y0) - CANVAS_LINE_PAD;
    it->w = (gint)ceil(x1 - floor(x0)) + 2 * CANVAS_LINE_PAD;
    it->h = (gint)ceil(y1 - floor(y0)) + 2 * CANVAS_LINE_PAD;
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
        /* Points relative to the block, so the note does not depend on
         * where it was drawn */
        for (guint i = 0; i < it->points->len; i++) {
            CanvasPoint* p = &g_array_index(it->points, CanvasPoint, i);
            p->x -= it->x;
            p->y -= it->y;
        }
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
 * the points fills the block, CANVAS_LINE_PAD from its sides */
gboolean canvas_draw_get_ends(const CanvasItem* it, gdouble* x0, gdouble* y0, gdouble* x1, gdouble* y1) {
    if (!canvas_draw_is_line(it) || it->points == NULL || it->points->len < 2) return FALSE;
    const CanvasPoint* a = &g_array_index(it->points, CanvasPoint, 0);
    const CanvasPoint* b = &g_array_index(it->points, CanvasPoint, it->points->len - 1);
    gdouble bx = MIN(a->x, b->x), by = MIN(a->y, b->y);
    gdouble bw = fabs(b->x - a->x), bh = fabs(b->y - a->y);
    gdouble pad = CANVAS_LINE_PAD;
    gdouble tw = MAX(it->w - 2 * pad, 0), th = MAX(it->h - 2 * pad, 0);
    *x0 = it->x + canvas_map_coord(a->x, bx, bw, pad, tw);
    *y0 = it->y + canvas_map_coord(a->y, by, bh, pad, th);
    *x1 = it->x + canvas_map_coord(b->x, bx, bw, pad, tw);
    *y1 = it->y + canvas_map_coord(b->y, by, bh, pad, th);
    return TRUE;
}

void canvas_draw_set_ends(CanvasItem* it, gdouble x0, gdouble y0, gdouble x1, gdouble y1) {
    if (!canvas_draw_is_line(it)) return;
    ensure_points(it);
    g_array_set_size(it->points, 0);
    CanvasPoint a = {x0, y0};
    CanvasPoint b = {x1, y1};
    g_array_append_val(it->points, a);
    g_array_append_val(it->points, b);
    fit_points(it);
    for (guint i = 0; i < it->points->len; i++) {
        CanvasPoint* p = &g_array_index(it->points, CanvasPoint, i);
        p->x -= it->x;
        p->y -= it->y;
    }
    redraw(it);
}
