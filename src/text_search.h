#ifndef TEXT_SEARCH_H
#define TEXT_SEARCH_H

/* Search the files of a project for a text (Ctrl+Shift+F), one line at a
 * time. By default literal and case-insensitive (Unicode letters); options
 * match the case, whole words, a regular expression (PCRE, GRegex) or the
 * letters of the text in order, not next to each other ("fuzzy"). Binary
 * files (a NUL byte in their first 8 KB) and files over
 * CODE_INDEX_MAX_FILE_BYTES are skipped. Blocking: call it from a worker
 * thread. Pure GLib, unit-tested. */
#include "code_index.h"

typedef struct {
    gboolean match_case;
    gboolean whole_word; /* ignored with fuzzy */
    gboolean regex;      /* ignored with fuzzy */
    gboolean fuzzy;      /* letters in order on one line (spaces ignored),
                            the shortest span of each line */
} TextSearchOptions;

/* Why query cannot be searched with opts (an invalid regular expression),
 * or NULL when it can. Free with g_free(). */
gchar* text_search_check(const gchar* query, const TextSearchOptions* opts);

/* The options in words ("any case", "case sensitive, regex"...). Free with
 * g_free(). */
gchar* text_search_describe(const TextSearchOptions* opts);

/* Every match of query in files (absolute paths), as CodeReference*
 * (code_index.h, with the matched length) by path, line and column; no
 * overlaps, empty matches dropped. At most CODE_INDEX_MAX_REFERENCES and
 * CODE_INDEX_MAX_BYTES read; *truncated (may be NULL) tells when a limit
 * was hit. Nothing when text_search_check() fails. */
GPtrArray* text_search_files(const gchar* query, const TextSearchOptions* opts, GPtrArray* files,
                             GCancellable* cancellable, gboolean* truncated);

#endif /* TEXT_SEARCH_H */
