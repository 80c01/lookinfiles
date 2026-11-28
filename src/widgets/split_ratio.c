/* Split shares and collapsed sides (see split_ratio.h) */
#include "widgets/split_ratio.h"

#include <math.h>

static const gdouble EVEN[SPLIT_RATIO_COUNT] = {1.0 / 3.0, 0.5, 2.0 / 3.0};
static const gdouble SIDE[] = {0.5};

const gdouble* split_ratio_shares(SplitShares set, gint* count) {
    gboolean side = set == SPLIT_SHARES_SIDE;
    if (count != NULL) *count = side ? (gint)G_N_ELEMENTS(SIDE) : SPLIT_RATIO_COUNT;
    return side ? SIDE : EVEN;
}

static gint keep_minimums(gint pos, gint max, gint min_start, gint min_end) {
    min_start = MAX(min_start, 0);
    min_end = MAX(min_end, 0);
    if (min_start + min_end <= max) {
        pos = CLAMP(pos, min_start, max - min_end);
    }
    return CLAMP(pos, 0, max);
}

gint split_ratio_position(gint max, gdouble share, gint min_start, gint min_end) {
    if (max <= 0) return 0;
    return keep_minimums((gint)lround(CLAMP(share, 0.0, 1.0) * max), max, min_start, min_end);
}

gint split_ratio_width(gint width, gint max, gint min_start, gint min_end) {
    if (width <= 0 || max <= 0) return -1;
    return keep_minimums(width, max, min_start, min_end);
}

gboolean split_ratio_reachable(gint max, gdouble share, gint min_start, gint min_end) {
    if (max <= 0) return FALSE;
    gint exact = (gint)lround(CLAMP(share, 0.0, 1.0) * max);
    return ABS(split_ratio_position(max, share, min_start, min_end) - exact) <= SPLIT_RATIO_SLACK;
}

gboolean split_ratio_matches(gint pos, gint max, gdouble share, gint min_start, gint min_end) {
    if (max <= 0) return FALSE;
    return ABS(pos - split_ratio_position(max, share, min_start, min_end)) <= SPLIT_RATIO_SLACK;
}

SplitSide split_ratio_collapsed(gint pos, gint max) {
    if (max <= 4 * SPLIT_COLLAPSE_SLACK) return SPLIT_SIDE_NONE;
    if (pos <= SPLIT_COLLAPSE_SLACK) return SPLIT_SIDE_START;
    if (pos >= max - SPLIT_COLLAPSE_SLACK) return SPLIT_SIDE_END;
    return SPLIT_SIDE_NONE;
}

gint split_ratio_expand(gint max, gdouble before_share, gint min_start, gint min_end) {
    if (max <= 0) return 0;
    gdouble share = 0.5;
    if (before_share >= 0.0) {
        gint before = (gint)lround(CLAMP(before_share, 0.0, 1.0) * max);
        if (split_ratio_collapsed(before, max) == SPLIT_SIDE_NONE) share = before_share;
    }
    return split_ratio_position(max, share, min_start, min_end);
}

gint split_ratio_fit(gint natural, gint total, gdouble max_share) {
    if (natural <= 0 || total <= 0) return -1;
    gint cap = (gint)lround(CLAMP(max_share, 0.0, 1.0) * total);
    return MIN(natural, cap);
}
