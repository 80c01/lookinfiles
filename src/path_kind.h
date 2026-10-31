#ifndef PATH_KIND_H
#define PATH_KIND_H

/* Whether a path is a folder, a file or missing, read in a worker thread so a
 * slow or hung file system (NFS, SSHFS, sleeping disk) never blocks the UI.
 * The last answer for each path is kept: callers show it at once and start a
 * check to refresh it. Main thread only. */
#include <glib.h>

typedef enum {
    PATH_KIND_UNKNOWN = 0, /* never checked yet */
    PATH_KIND_DIR,
    PATH_KIND_FILE,
    PATH_KIND_MISSING,     /* does not exist or cannot be read */
} PathKind;

/* Called on the main thread when a check ends */
typedef void (*PathKindFunc)(const gchar* path, PathKind old_kind, PathKind new_kind,
                             gpointer data);

/* Last kind read for path, PATH_KIND_UNKNOWN if never checked */
PathKind path_kind_lookup(const gchar* path);

/* Read path's kind in a worker thread, then call done (may be NULL). FALSE
 * (and nothing called) if a check of path is still running: a hung mount ties
 * up one thread at most. */
gboolean path_kind_check(const gchar* path, PathKindFunc done, gpointer data);

/* Forget every kind read (tests) */
void path_kind_clear(void);

#endif /* PATH_KIND_H */
