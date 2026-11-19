#ifndef DEEP_QUERY_H
#define DEEP_QUERY_H

/* The criteria of a deep search (deep_search.h): name, extensions, kind,
 * size, date of last change, content and where to look, as typed in the
 * Deep Search tab, and the test of one entry against them. The cheap
 * criteria (everything but the content) are tested on the name and the
 * lstat() values; the content is read only for the files that pass them.
 * Pure GLib, unit-tested. */
#include "search/text_search.h"

typedef enum {
    DEEP_KIND_ANY,
    DEEP_KIND_FILES,   /* files (and links) only */
    DEEP_KIND_FOLDERS, /* folders only */
} DeepKind;

typedef struct {
    gchar* name;       /* part of the name, any case; with * or ? a pattern
                          of the whole name ("*.tar.gz", "IMG_????.jpg") */
    gchar* extensions; /* "c, h, .md, tar.gz", any case: one of them ends the name */
    gchar* exclude;    /* paths left out, folders not entered (path_globs.h) */
    gchar* content;    /* text the file holds (text_search.h), or NULL */
    TextSearchOptions content_opts;
    DeepKind kind;
    gint64 min_size;   /* bytes, -1: none (files only once set) */
    gint64 max_size;
    gint64 after;      /* last change at or after (Unix seconds), 0: none */
    gint64 before;     /* last change before, 0: none */
    gboolean hidden;   /* look at the names starting with a dot */
    gboolean one_filesystem; /* do not enter other filesystems */
    gint max_depth;    /* levels below the folder (1: its entries only), 0: any */
} DeepQuery;

/* A query that matches everything: no criteria, hidden entries left out */
void deep_query_init(DeepQuery* q);
void deep_query_clear(DeepQuery* q);
void deep_query_copy(DeepQuery* dst, const DeepQuery* src);

/* A size typed by the user: "", "42", "500k", "1.5 MB", "2G" (powers of
 * 1024, any case, "B" optional). FALSE when it is not one; *out is -1 for
 * an empty text. */
gboolean deep_query_parse_size(const gchar* text, gint64* out);

/* A date typed by the user, as Unix seconds: "" (0), "2026-11-01" or
 * "2026-11-01 14:30" (local time; a day alone is its start, or with
 * end_of_day the start of the next day), "today", "yesterday", or a time
 * ago: "30min", "3h", "7d", "2w", "6m" (months), "1y". now is the current
 * local time. FALSE when it is not one. */
gboolean deep_query_parse_date(const gchar* text, GDateTime* now, gboolean end_of_day, gint64* out);

/* Why the query cannot run (an invalid regular expression, a minimum over
 * the maximum...), or NULL. Free with g_free(). */
gchar* deep_query_check(const DeepQuery* q);

/* The query compiled for testing entries; NULL when deep_query_check()
 * fails. Read-only once made: one per search thread is enough. */
typedef struct DeepFilter DeepFilter;
DeepFilter* deep_filter_new(const DeepQuery* q);
void deep_filter_free(DeepFilter* f);

/* rel (from the folder searched, "a/b.c") is left out with what is below */
gboolean deep_filter_excluded(const DeepFilter* f, const gchar* rel, gboolean is_dir);

/* An entry passes the criteria but its content: name (no folder), kind,
 * size and time of last change (Unix seconds) */
gboolean deep_filter_match_entry(const DeepFilter* f, const gchar* name, gboolean is_dir, guint64 size, gint64 mtime);

/* The query looks into the files (a content was given) */
gboolean deep_filter_has_content(const DeepFilter* f);

#endif /* DEEP_QUERY_H */
