#ifndef DEEP_SEARCH_H
#define DEEP_SEARCH_H

/* A deep search: every entry below a folder tested against a DeepQuery
 * (deep_query.h), with no limit on the files listed or read (the content
 * of a file is read by blocks, text_search.h). Run in a worker thread
 * (deep_search_run(), cancellable); any thread takes the entries found so
 * far and the counters with deep_search_take(), so the results show while
 * the search goes on. Folders are read one at a time, names first (a deep
 * tree keeps no descriptor per level), in name order, each folder's
 * entries before what is below them. Symbolic links are tested as entries
 * but never followed; /proc, /sys and /dev are not entered unless the
 * search starts in them; with one_filesystem, neither are other mounted
 * filesystems. At most DEEP_SEARCH_MAX_HITS entries are kept (memory, not
 * time: the search stops there). Pure GLib, unit-tested. */
#include "search/deep_query.h"

#define DEEP_SEARCH_MAX_HITS 1000000
#define DEEP_SEARCH_MAX_COUNT 9999 /* matches counted in one file */

typedef struct {
    gchar* path;      /* absolute */
    gboolean is_dir;
    gboolean is_link;
    guint64 size;
    gint64 mtime;     /* Unix seconds */
    guint matches;    /* content: matches in the file (DEEP_SEARCH_MAX_COUNT at most) */
    gboolean more;    /* content: more matches than counted */
    gint line;        /* content: the first match, line (0-based), byte column, length */
    gint column;
    gint length;
    gchar* text;      /* content: its line (valid UTF-8), else NULL */
} DeepHit;

void deep_hit_free(gpointer hit);

typedef enum {
    DEEP_SEARCH_RUNNING,
    DEEP_SEARCH_DONE,
    DEEP_SEARCH_STOPPED,    /* cancelled */
    DEEP_SEARCH_FULL,       /* DEEP_SEARCH_MAX_HITS kept */
    DEEP_SEARCH_NO_FOLDER,  /* the folder cannot be read */
} DeepSearchState;

typedef struct {
    DeepSearchState state;
    guint64 dirs;       /* folders read */
    guint64 entries;    /* entries tested */
    guint64 searched;   /* files whose content was read */
    guint64 bytes;      /* bytes of content read */
    guint64 found;      /* entries found in all */
    guint64 unreadable; /* folders and files that could not be read */
    guint64 binary;     /* files not searched: binary */
    gchar* current;     /* folder being read, or NULL */
} DeepSearchProgress;

void deep_search_progress_clear(DeepSearchProgress* p);

typedef struct DeepSearch DeepSearch;

/* A search of root (a folder) for q (copied); NULL when the query cannot
 * run (deep_query_check()) */
DeepSearch* deep_search_new(const gchar* root, const DeepQuery* q);
void deep_search_free(DeepSearch* s);
const gchar* deep_search_root(const DeepSearch* s);

/* Run it to the end, the cancellation or DEEP_SEARCH_MAX_HITS; blocking */
void deep_search_run(DeepSearch* s, GCancellable* cancel);

/* Thread-safe: the counters into progress (clear it after), and the
 * DeepHit* found since the last call (owned by the caller), or NULL when
 * there is none */
GPtrArray* deep_search_take(DeepSearch* s, DeepSearchProgress* progress);

#endif /* DEEP_SEARCH_H */
