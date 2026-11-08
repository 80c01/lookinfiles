#ifndef TEXT_SEARCH_H
#define TEXT_SEARCH_H

/* Search the files of a project for a text (Ctrl+Shift+F), one line at a
 * time. By default literal and case-insensitive (Unicode letters); options
 * match the case, whole words, a regular expression (PCRE, GRegex) or the
 * letters of the text in order, not next to each other ("fuzzy"). Binary
 * files (a NUL byte in their first 8 KB) and files over
 * TEXT_SEARCH_MAX_FILE_BYTES are skipped. Blocking: call it from a worker
 * thread. Pure GLib, unit-tested. */
#include "code/code_index.h"

/* Limits of a search in files (higher than those of code navigation) */
#define TEXT_SEARCH_MAX_FILES 100000              /* files listed */
#define TEXT_SEARCH_MAX_BYTES (512 * 1024 * 1024) /* bytes read in all */
#define TEXT_SEARCH_MAX_FILE_BYTES CODE_INDEX_MAX_FILE_BYTES
#define TEXT_SEARCH_MAX_RESULTS CODE_INDEX_MAX_REFERENCES

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

/* Why a search stopped before the last file */
typedef enum {
    TEXT_SEARCH_DONE,            /* every file searched (or cancelled) */
    TEXT_SEARCH_MAX_RESULTS_HIT, /* TEXT_SEARCH_MAX_RESULTS found */
    TEXT_SEARCH_MAX_BYTES_HIT,   /* TEXT_SEARCH_MAX_BYTES read */
} TextSearchStop;

typedef struct {
    TextSearchStop stop;
    guint searched;      /* files read and searched */
    guint skipped_large; /* files over TEXT_SEARCH_MAX_FILE_BYTES */
    guint too_complex;   /* files where the regular expression gave up (PCRE
                            match limit): their matches may be missing */
} TextSearchStats;

/* Every match of query in files (absolute paths), as CodeReference*
 * (code_index.h, with the matched length) by path, line and column; no
 * overlaps, empty matches dropped. At most TEXT_SEARCH_MAX_RESULTS and
 * TEXT_SEARCH_MAX_BYTES read; stats (may be NULL) tells why it stopped and
 * what was skipped. Nothing when text_search_check() fails. */
GPtrArray* text_search_files(const gchar* query, const TextSearchOptions* opts, GPtrArray* files,
                             GCancellable* cancellable, TextSearchStats* stats);

#endif /* TEXT_SEARCH_H */
