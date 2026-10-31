#ifndef FUZZY_MATCH_H
#define FUZZY_MATCH_H

/* Fuzzy matching of file paths for quick open (Ctrl+P): the query's
 * characters must appear in order in the path, case-insensitively (unless
 * the query has a capital letter). Matches in the file name, at the start
 * of a word (after / _ - . or a space, or a lower-to-upper case change) and
 * runs of consecutive characters score higher; shorter paths win ties.
 * Spaces in the query are ignored. Pure, unit-tested. */
#include <glib.h>

/* Score of path for query (higher is better), or -1 when it does not match.
 * An empty query matches everything with a score from the path length. */
gint fuzzy_match_score(const gchar* query, const gchar* path);

/* Byte offsets in path of the query's characters in the best match found,
 * for highlighting (caller frees, G_MAXSIZE-terminated), or NULL */
gsize* fuzzy_match_positions(const gchar* query, const gchar* path);

#endif /* FUZZY_MATCH_H */
