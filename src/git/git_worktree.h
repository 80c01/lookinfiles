#ifndef GIT_WORKTREE_H
#define GIT_WORKTREE_H

/* The uncommitted changes of a work tree, read through libgit2 (no git
 * program): what is staged (index against HEAD), what is not (work tree
 * against the index) and the untracked files, with their line counts, and
 * the patch of one of them. No AppData and no widgets; worker threads only
 * (each call opens its own repository). */
#include <gio/gio.h>

typedef enum {
    GIT_CHANGE_STAGED,    /* in the index, not committed */
    GIT_CHANGE_UNSTAGED,  /* in the work tree, not in the index */
    GIT_CHANGE_UNTRACKED, /* a new file git does not follow */
    GIT_CHANGE_GROUPS
} GitChangeGroup;

typedef struct {
    gchar* path;          /* from the work tree root, '/' separators */
    gchar* old_path;      /* renamed from, or NULL */
    GitChangeGroup group;
    gchar status;         /* 'M' modified, 'A' added, 'D' deleted, 'R' renamed,
                           * 'T' type changed, 'U' untracked, '!' conflict */
    gint added;           /* lines added, -1 for a binary file */
    gint removed;         /* lines removed, -1 for a binary file */
} GitChange;

void git_change_free(gpointer change);

/* Files over this size count as binary (no lines, no patch) */
#define GIT_CHANGE_MAX_SIZE (4 * 1024 * 1024)

/* The changes of the work tree at root: the staged ones, then the unstaged,
 * then the untracked, each group by path; a file staged and changed again
 * is in both of the first two. At most max of them (0: no limit), *more
 * set when some are left out (more may be NULL). NULL when root is not a
 * work tree or cancellable was cancelled. */
GPtrArray* git_read_changes(const gchar* root, guint max, GCancellable* cancellable, gboolean* more);

/* The patch of path in group (`git diff --cached`, `git diff`, or the whole
 * file for an untracked one), NULL if it cannot be read; "" when path has
 * no change there */
gchar* git_change_patch(const gchar* root, const gchar* path, GitChangeGroup group);

/* "Staged", "Changes", "Untracked" (translated) */
const gchar* git_change_group_title(GitChangeGroup group);

#endif /* GIT_WORKTREE_H */
