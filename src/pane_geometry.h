#ifndef PANE_GEOMETRY_H
#define PANE_GEOMETRY_H

#include <glib.h>

/* Rectangle of a pane in a common coordinate space (y grows downward) */
typedef struct {
    double x, y, width, height;
} PaneRect;

typedef enum {
    PANE_DIR_RIGHT,
    PANE_DIR_LEFT,
    PANE_DIR_UP,
    PANE_DIR_DOWN
} PaneDirection;

/* Index of the pane next to `cur` in direction `dir` among `cands`, or -1.
 * A candidate qualifies when it lies past the matching edge of `cur` and
 * overlaps it across the move (same row for left/right, same column for
 * up/down). The nearest one along the move wins, then the one best aligned
 * with `cur`. Entries equal to `cur` are skipped. */
gint pane_find_neighbor(const PaneRect* cur, const PaneRect* cands, guint n_cands,
                        PaneDirection dir);

/* Where a dragged tab lands on a pane: the whole pane, or a new pane split
 * off one of its sides */
typedef enum {
    PANE_ZONE_CENTER,
    PANE_ZONE_LEFT,
    PANE_ZONE_RIGHT,
    PANE_ZONE_TOP,
    PANE_ZONE_BOTTOM
} PaneZone;

/* Share of the pane size, from each edge, that splits that side */
#define PANE_ZONE_EDGE_FRACTION 0.25

/* Zone of `area` under the point (x, y): the side whose edge is nearest,
 * when closer than PANE_ZONE_EDGE_FRACTION of the pane size, else the
 * center. */
PaneZone pane_drop_zone(const PaneRect* area, double x, double y);

/* Part of `area` a zone would take: the whole pane, or the half on that side */
PaneRect pane_zone_rect(const PaneRect* area, PaneZone zone);

#endif /* PANE_GEOMETRY_H */
