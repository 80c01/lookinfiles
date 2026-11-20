#ifndef GIT_FILTER_H
#define GIT_FILTER_H

/* A filter of a disk scan by git: only the files of the repository
 * (tracked and untracked, not ignored, with its submodules and nested
 * repositories whole), or only the ignored ones (without the history and
 * the nested repositories). Built from the index and the status of the
 * work tree (git_list_files_by_kind()), then given to the scan with
 * disk_scan_set_filter(). No widgets, no AppData. */
#include "core/disk_scan.h"

typedef enum {
    GIT_FILTER_NONE = 0, /* every file */
    GIT_FILTER_REPO,     /* the files of the repository */
    GIT_FILTER_IGNORED   /* the ignored files */
} GitFilterMode;

typedef struct GitFilter GitFilter;

/* The filter of mode for a scan of root (a folder in a work tree), NULL
 * when root is not in a work tree, mode is GIT_FILTER_NONE or cancel is
 * triggered. Reads the index and the work tree status: worker thread. */
GitFilter* git_filter_new(const gchar* root, GitFilterMode mode, GCancellable* cancel);
void git_filter_free(GitFilter* filter);

/* Read only what filter keeps (filter must outlive the reading) */
void git_filter_apply(GitFilter* filter, DiskScan* scan);

/* What filter does with rel (below the root, native separators) */
DiskFilterVerdict git_filter_check(const gchar* rel, gboolean dir, gpointer filter);

#endif /* GIT_FILTER_H */
