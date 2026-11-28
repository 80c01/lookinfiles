#ifndef SPLIT_RATIO_H
#define SPLIT_RATIO_H

/* Where the handle of a split goes for a share of its room, and whether a
 * side is collapsed (the split handle menu, split_menu.h). A position is the
 * size of the start side, from 0 to max (the room left by the handle), as in
 * GtkPaned. Pure logic, no widgets: unit-tested in tests/. */
#include <glib.h>

/* The shares the menu offers, from the start side (at most SPLIT_RATIO_COUNT) */
#define SPLIT_RATIO_COUNT 3
typedef enum {
    SPLIT_SHARES_EVEN, /* 1/3, 1/2, 2/3: two sides of the same weight */
    SPLIT_SHARES_SIDE, /* 1/5, 1/2: a side panel at the start (the explorer), whose
                        * other widths are absolute (its default width, fitted) */
} SplitShares;

/* The shares of set, smallest first; *count gets their number */
const gdouble* split_ratio_shares(SplitShares set, gint* count);

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

/* Default width of a side panel (the explorer), in pixels: absolute, so it
 * does not grow with the window */
#define SPLIT_SIDE_DEFAULT_WIDTH 260
/* About that width as a share of the window, where a share is needed before
 * the width can be measured */
#define SPLIT_SIDE_DEFAULT_SHARE 0.25

/* Position giving the start side width px, kept at least min_start from 0 and
 * min_end from max when the room allows both; -1 when width or max is
 * unknown (<= 0) */
gint split_ratio_width(gint width, gint max, gint min_start, gint min_end);

/* Largest share of the window the explorer gets when it is fitted to its
 * content (the fit button of its handle, the Auto layout) */
#define SPLIT_SIDE_MAX_SHARE 0.40

/* Position that fits the start side to its natural size (natural px), at
 * most max_share of total; -1 when natural or total is unknown (<= 0) */
gint split_ratio_fit(gint natural, gint total, gdouble max_share);

#endif /* SPLIT_RATIO_H */
