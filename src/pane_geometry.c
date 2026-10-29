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
