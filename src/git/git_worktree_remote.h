#ifndef GIT_WORKTREE_REMOTE_H
#define GIT_WORKTREE_REMOTE_H

/* The uncommitted changes of a remote machine's work tree (vfs.h), as
 * git_worktree.h gives them for a local one, from git run there: one
 * command lists them (git status, git diff --numstat for the line counts),
 * another gives the patch of one file. Blocking: worker threads only. The
 * reading of the output is unit-tested. */
#include "git/git_worktree.h"

/* The shell command, run at the top of the work tree, printing what
 * git_remote_parse_changes() reads: the status (porcelain, -z), the line
 * counts of the staged and of the unstaged changes (numstat, -z), the
 * lines of the untracked files (grep -c), the four parts separated by a
 * "\001" record */
#define GIT_WORKTREE_REMOTE_COMMAND                                                                                \
    "git status --porcelain=v1 -z --untracked-files=all 2>/dev/null || exit 3; printf '\\001\\0'; "              \
    "git diff --cached --numstat -z -M 2>/dev/null; printf '\\001\\0'; "                                         \
    "git diff --numstat -z 2>/dev/null; printf '\\001\\0'; "                                                     \
    "git ls-files -z -o --exclude-standard 2>/dev/null | xargs -0 -r grep -IHcZ '' -- 2>/dev/null; exit 0"

/* The changes in that output, as git_read_changes() lists them: staged,
 * then unstaged (conflicts there, '!'), then untracked, each group by path;
 * at most max (0: no limit), *more (may be NULL) set when some are left out */
GPtrArray* git_remote_parse_changes(const gchar* data, gsize len, guint max, gboolean* more);

/* git_read_changes(), git_count_changes() and git_change_patch() for top,
 * the top of a remote machine's work tree (its path as seen here) */
GPtrArray* git_remote_read_changes(const gchar* top, guint max, GCancellable* cancel, gboolean* more);
gint git_remote_count_changes(const gchar* top, GCancellable* cancel);
gchar* git_remote_change_patch(const gchar* top, const gchar* path, GitChangeGroup group);

/* The part of a `git diff` (several files) about path ("diff --git a/... b/path"
 * up to the next file), "" when there is none. Caller frees. */
gchar* git_remote_patch_of(const gchar* diff, const gchar* path);

#endif /* GIT_WORKTREE_REMOTE_H */
