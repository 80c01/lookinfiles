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

#endif /* PANE_GEOMETRY_H */
