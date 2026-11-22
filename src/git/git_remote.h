#ifndef GIT_REMOTE_H
#define GIT_REMOTE_H

/* Git on a remote machine (vfs.h), whose repository cannot be opened here:
 * `git status` run there (vfs_exec()) gives the explorer's badges, the
 * branch and the upstream counts. Blocking: worker threads only. The
 * reading of the output is unit-tested. */
#include "git/git_state.h"

#include <gio/gio.h>

/* The shell command printing what git_remote_parse() reads */
#define GIT_REMOTE_STATUS_COMMAND                                                                                  \
    "git rev-parse --show-prefix 2>/dev/null || exit 3; printf '\\0'; "                                         \
    "git status --porcelain=v1 -z --branch --untracked-files=all 2>/dev/null; exit 0"

/* The state of dir (a local path: the folder's path as seen here) from the
 * output of GIT_REMOTE_STATUS_COMMAND run in it: the repository's root is
 * dir less the prefix git printed. NULL when the output is not one. */
GitState* git_remote_parse(const gchar* dir, const gchar* data, gsize len);

/* The state of dir, a remote machine's folder: NULL outside a repository
 * or when git could not run there */
GitState* git_remote_scan(const gchar* dir, GCancellable* cancel);

#endif /* GIT_REMOTE_H */
