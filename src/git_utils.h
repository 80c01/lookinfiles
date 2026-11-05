#ifndef GIT_UTILS_H
#define GIT_UTILS_H

#include <glib.h>

/* Repository root containing start_dir (or a parent), NULL if not in a repository
 * or if start_dir is inside a .git directory. Caller frees. */
gchar* find_git_root(const gchar* start_dir);

/* Git directory of a repository root ("<root>/.git" or the "gitdir:" target
 * for worktrees and submodules), NULL if none. Caller frees. */
gchar* git_resolve_dir(const gchar* git_root);

/* Current branch name (short SHA when detached), NULL outside a repository. Caller frees. */
gchar* git_read_branch(const gchar* dir);

/* Number of commits HEAD is ahead of and behind its upstream tracking branch.
 * Returns TRUE when dir is in a repository and HEAD is a branch with an upstream
 * configured; *ahead and *behind receive the counts. Returns FALSE otherwise (and
 * sets *ahead and *behind to 0). Worker thread. */
gboolean git_read_ahead_behind(const gchar* dir, guint* ahead, guint* behind);

/* Status of the repository of dir (and of the repositories just below dir when
 * dir is in none) as full_path -> Pango badge markup, NULL when nothing changed
 * or outside a repository. Uses libgit2 and walks the work tree: call from a
 * worker thread. Caller destroys the table. */
GHashTable* scan_git_statuses(const gchar* dir);

/* Content of file path in the last commit (HEAD) of its repository, *len its
 * size (cut at the first NUL: binary files are not shown as text). NULL
 * outside a repository or when HEAD has no such file (untracked, new). Worker
 * thread. Caller frees. */
gchar* git_read_head_blob(const gchar* path, gsize* len);

/* One commit of the history (git_read_log()) */
typedef struct {
    gchar* hash;       /* full SHA */
    gchar* short_hash; /* shortest unique abbreviation, like git's %h */
    gchar* subject;    /* first paragraph of the message on one line */
    gchar* author;
    gchar* date; /* committer date relative to now, like git's %cr */
} GitCommitInfo;

void git_commit_info_free(gpointer info);

/* The last max commits reachable from HEAD of the repository at root, newest
 * first: array of GitCommitInfo*, empty without commits, NULL when root is not
 * a repository. Worker thread. */
GPtrArray* git_read_log(const gchar* root, guint max);

/* Header, message, diffstat and patch of commit hash (against its first
 * parent), laid out like `git show --stat --patch`. NULL when the commit is
 * not found. Valid UTF-8. Worker thread. Caller frees. */
gchar* git_show_commit(const gchar* root, const gchar* hash);

/* Callback of git_list_files(): rel is relative to the root, with '/'
 * separators. Returns FALSE to stop. */
typedef gboolean (*GitFileFunc)(const gchar* rel, gpointer data);

/* Each file of the index then each untracked file of the work tree at root,
 * once, like `git ls-files --cached --others --exclude-standard` (with_ignored:
 * also the ignored files, without --exclude-standard). FALSE when root is not
 * the top of a work tree. Worker thread. */
gboolean git_list_files(const gchar* root, gboolean with_ignored, GitFileFunc func, gpointer data);

/* Age of a commit made seconds ago, like git's relative dates: "5 minutes
 * ago", "3 weeks ago", "1 year, 2 months ago", "in the future". Caller frees. */
gchar* git_relative_date(gint64 seconds);

#endif /* GIT_UTILS_H */
