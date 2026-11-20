#ifndef SPLIT_RATIO_H
#define SPLIT_RATIO_H

/* Where the handle of a split goes for a share of its room, and whether a
 * side is collapsed (the split handle menu, split_menu.h). A position is the
 * size of the start side, from 0 to max (the room left by the handle), as in
 * GtkPaned. Pure logic, no widgets: unit-tested in tests/. */
#include <glib.h>

/* The shares the menu offers, from the start side */
#define SPLIT_RATIO_COUNT 3
extern const gdouble SPLIT_RATIOS[SPLIT_RATIO_COUNT]; /* 1/3, 1/2, 2/3 */

/* A side this close to 0 px (or less) is collapsed */
#define SPLIT_COLLAPSE_SLACK 2
/* A position this close to a share's is at that share */
#define SPLIT_RATIO_SLACK 3

typedef enum {
    SPLIT_SIDE_NONE = 0,
    SPLIT_SIDE_START = 1 << 0, /* left or top */
    SPLIT_SIDE_END = 1 << 1,   /* right or bottom */
} SplitSide;

/* Position for share (0..1) of max, kept at least min_start from 0 and
 * min_end from max when the room allows both (the sides stay usable) */
gint split_ratio_position(gint max, gdouble share, gint min_start, gint min_end);

/* TRUE when share can be reached exactly (within SPLIT_RATIO_SLACK) with
 * those minimum sizes */
gboolean split_ratio_reachable(gint max, gdouble share, gint min_start, gint min_end);

/* TRUE when pos is the position of share (within SPLIT_RATIO_SLACK) */
gboolean split_ratio_matches(gint pos, gint max, gdouble share, gint min_start, gint min_end);

/* The side collapsed at pos: START at 0, END at max, NONE otherwise (also when
 * max is too small to tell) */
SplitSide split_ratio_collapsed(gint pos, gint max);

/* Position that expands a collapsed split back: the share it had before
 * (before_share, < 0 when unknown), or a half when that was collapsed too */
gint split_ratio_expand(gint max, gdouble before_share, gint min_start, gint min_end);

#endif /* SPLIT_RATIO_H */
