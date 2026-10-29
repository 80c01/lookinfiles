#ifndef LINE_DIFF_H
#define LINE_DIFF_H

#include <glib.h>

/* Per-line change marks of a new text against an old one (bit flags) */
typedef enum {
    LINE_DIFF_ADDED = 1 << 0,         /* line inserted */
    LINE_DIFF_MODIFIED = 1 << 1,      /* line replacing old lines */
    LINE_DIFF_DELETED_ABOVE = 1 << 2, /* old lines removed just before this line */
    LINE_DIFF_DELETED_BELOW = 1 << 3, /* old lines removed after this last line */
} LineDiffMark;

/* Number of lines of text ("" has 0, "a" and "a\n" have 1, "a\nb" has 2) */
guint line_diff_count_lines(const gchar* text, gsize len);

/* Line diff (Myers) of new_text against old_text: one LineDiffMark set per
 * line of new_text, *n_lines receives their number. Lines are compared without
 * their end of line. Very different texts (beyond an internal edit budget) mark
 * the whole changed region as modified. Returns NULL for an empty new text.
 * Caller frees. */
guint8* line_diff_marks(const gchar* old_text, gsize old_len,
                        const gchar* new_text, gsize new_len, guint* n_lines);

#endif /* LINE_DIFF_H */
