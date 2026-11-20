/* Side-by-side line diff with UTF-8 intra-line change spans and hunk index.
 * Pure GLib (layer 1), unit-tested. */
#ifndef SIDE_DIFF_H
#define SIDE_DIFF_H

#include <glib.h>

#define SIDE_DIFF_MAX_LINES 50000

typedef enum {
    SIDE_DIFF_EQUAL = 0,
    SIDE_DIFF_ADDED,    /* right only; left is a filler (left_line == 0) */
    SIDE_DIFF_REMOVED,  /* left only; right is a filler (right_line == 0) */
    SIDE_DIFF_MODIFIED  /* left and right paired, with optional intra-line spans */
} SideDiffKind;

typedef struct {
    guint start; /* byte offset on a valid UTF-8 boundary */
    guint end;
} SideDiffSpan;

typedef struct {
    SideDiffKind kind;
    gint left_line;   /* 1-based source line number, 0 for a filler */
    gint right_line;  /* 1-based source line number, 0 for a filler */
    gchar* left_text; /* valid UTF-8 without trailing '\r' / '\n' */
    gchar* right_text;
    GArray* left_spans;  /* SideDiffSpan elements, or NULL */
    GArray* right_spans; /* SideDiffSpan elements, or NULL */
} SideDiffRow;

typedef struct {
    GPtrArray* rows;    /* SideDiffRow* */
    GArray* hunks;      /* guint row indices where each changed block starts */
    guint added;        /* total added lines (right-only + modified) */
    guint removed;      /* total removed lines (left-only + modified) */
    guint modified;     /* paired modified rows */
    gboolean truncated; /* input exceeded SIDE_DIFF_MAX_LINES */
} SideDiffDoc;

/* Compute a side-by-side diff of left[0..left_len) against right[0..right_len).
 * NULL inputs are treated as empty. Caller frees with side_diff_free(). */
SideDiffDoc* side_diff_compute(const gchar* left, gsize left_len,
                               const gchar* right, gsize right_len);

void side_diff_free(SideDiffDoc* doc);

#endif /* SIDE_DIFF_H */
