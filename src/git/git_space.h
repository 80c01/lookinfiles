#ifndef GIT_SPACE_H
#define GIT_SPACE_H

/* The room the files of a git work tree take in a disk scan (the Disk
 * Usage tab): tracked, untracked, ignored, the history (.git) and the
 * repositories inside it (submodules, nested repositories), with the
 * largest ignored entries and the largest tracked files. Read from the
 * index and the status of the work tree (git_list_files_by_kind()) and
 * the tree of the scan; the ignored room is what the scan read beside the
 * rest. No widgets, no AppData. */
#include "core/disk_scan.h"

/* Entries kept in each list */
#define GIT_SPACE_TOP 10

typedef struct {
    gchar* path;    /* full path */
    guint64 size;   /* apparent bytes */
    guint64 count;  /* files (1 for a file) */
    gboolean dir;
} GitSpaceEntry;

typedef struct {
    gchar* top;          /* top of the work tree */
    gchar* top_shown;    /* top as shown (machine:path on a remote machine),
                            set by the caller; NULL: top */
    gboolean inside;     /* the folder scanned is below the top (the history
                            and what lies beside the folder are left out) */
    DiskTally tracked;   /* files of the index */
    DiskTally untracked; /* files of the work tree only, not ignored */
    DiskTally ignored;   /* the rest of the scan */
    DiskTally history;   /* the .git folder (empty when it is outside the
                            folder scanned or a "gitdir:" file) */
    DiskTally nested;    /* submodules and repositories inside the work tree */
    GArray* ignored_top; /* GitSpaceEntry: the largest folders holding only
                            ignored files and the largest ignored files of the
                            other folders (among those the scan kept), largest
                            first */
    GArray* tracked_top; /* GitSpaceEntry: the largest tracked files */
} GitSpace;

/* The room of the work tree holding the root of scan (a scan that ran to
 * its end), NULL when that root is not in a work tree or cancel is
 * triggered. Reads the index, the work tree status and the size of each
 * file listed: worker thread. */
GitSpace* git_space_compute(DiskScan* scan, GCancellable* cancel);
void git_space_free(GitSpace* space);

#endif /* GIT_SPACE_H */
