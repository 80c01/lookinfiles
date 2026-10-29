#include "pane_geometry.h"

/* Tolerance for edges that touch (paned handles, rounding) */
#define EDGE_EPSILON 2.0

static double overlap(double a0, double a1, double b0, double b1) {
    return MIN(a1, b1) - MAX(a0, b0);
}

gint pane_find_neighbor(const PaneRect* cur, const PaneRect* cands, guint n_cands,
                        PaneDirection dir) {
    if (cur == NULL || cands == NULL) return -1;

    gboolean horizontal = (dir == PANE_DIR_RIGHT || dir == PANE_DIR_LEFT);
    double cur_cx = cur->x + cur->width / 2.0;
    double cur_cy = cur->y + cur->height / 2.0;

    gint best = -1;
    double best_gap = 0.0;
    double best_offset = 0.0;

    for (guint i = 0; i < n_cands; i++) {
        const PaneRect* c = &cands[i];
        if (c->x == cur->x && c->y == cur->y &&
            c->width == cur->width && c->height == cur->height) {
            continue;
        }

        double gap;
        switch (dir) {
        case PANE_DIR_RIGHT: gap = c->x - (cur->x + cur->width); break;
        case PANE_DIR_LEFT: gap = cur->x - (c->x + c->width); break;
        case PANE_DIR_DOWN: gap = c->y - (cur->y + cur->height); break;
        case PANE_DIR_UP: default: gap = cur->y - (c->y + c->height); break;
        }
        if (gap < -EDGE_EPSILON) continue;
        gap = MAX(gap, 0.0);

        double ov, offset;
        if (horizontal) {
            ov = overlap(cur->y, cur->y + cur->height, c->y, c->y + c->height);
            offset = ABS((c->y + c->height / 2.0) - cur_cy);
        } else {
            ov = overlap(cur->x, cur->x + cur->width, c->x, c->x + c->width);
            offset = ABS((c->x + c->width / 2.0) - cur_cx);
        }
        if (ov <= EDGE_EPSILON) continue;

        gboolean better;
        if (best < 0) {
            better = TRUE;
        } else if (ABS(gap - best_gap) > EDGE_EPSILON) {
            better = gap < best_gap;
        } else {
            better = offset < best_offset;
        }
        if (better) {
            best = (gint)i;
            best_gap = gap;
            best_offset = offset;
        }
    }
    return best;
}

PaneZone pane_drop_zone(const PaneRect* area, double x, double y) {
    if (area == NULL || area->width <= 0.0 || area->height <= 0.0) {
        return PANE_ZONE_CENTER;
    }
    double fx = (x - area->x) / area->width;
    double fy = (y - area->y) / area->height;
    PaneZone zone = PANE_ZONE_CENTER;
    double nearest = PANE_ZONE_EDGE_FRACTION;
    const struct {
        PaneZone zone;
        double dist;
    } edges[] = {
        {PANE_ZONE_LEFT, fx},
        {PANE_ZONE_RIGHT, 1.0 - fx},
        {PANE_ZONE_TOP, fy},
        {PANE_ZONE_BOTTOM, 1.0 - fy},
    };
    for (guint i = 0; i < G_N_ELEMENTS(edges); i++) {
        if (edges[i].dist < nearest) {
            nearest = edges[i].dist;
            zone = edges[i].zone;
        }
    }
    return zone;
}

PaneRect pane_zone_rect(const PaneRect* area, PaneZone zone) {
    PaneRect r = *area;
    switch (zone) {
    case PANE_ZONE_LEFT:
        r.width = area->width / 2.0;
        break;
    case PANE_ZONE_RIGHT:
        r.width = area->width / 2.0;
        r.x = area->x + r.width;
        break;
    case PANE_ZONE_TOP:
        r.height = area->height / 2.0;
        break;
    case PANE_ZONE_BOTTOM:
        r.height = area->height / 2.0;
        r.y = area->y + r.height;
        break;
    case PANE_ZONE_CENTER:
    default:
        break;
    }
    return r;
}
