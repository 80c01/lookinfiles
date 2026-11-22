#ifndef GIT_UTILS_H
#define GIT_UTILS_H

#include <gio/gio.h>

/* Repository root containing start_dir (or a parent), NULL if not in a repository
 * or if start_dir is inside a .git directory. Caller frees. */
gchar* find_git_root(const gchar* start_dir);

/* path relative to the root of its repository ("src/main.c", "." for the
 * root itself), NULL if path is not in a repository. Caller frees. */
gchar* git_relative_path(const gchar* path);

/* path (a folder at or below top, both canonical) as a prefix of the
 * paths git lists: '/'-separated with a final '/', "" when it is top.
 * Caller frees. */
gchar* git_path_prefix(const gchar* top, const gchar* path);

/* Git directory of a repository root ("<root>/.git" or the "gitdir:" target
 * for worktrees and submodules), NULL if none. Caller frees. */
gchar* git_resolve_dir(const gchar* git_root);

/* Current branch name (short SHA when detached), NULL outside a repository. Caller frees. */
gchar* git_read_branch(const gchar* dir);

/* One branch of a repository (git_list_branches()) */
typedef struct {
    gchar* name;        /* branch name, e.g. "main" or "origin/main" */
    gboolean is_head;   /* TRUE if this branch is currently checked out */
    gboolean is_remote; /* TRUE if remote branch */
} GitBranchInfo;

void git_branch_info_free(gpointer info);

/* List of GitBranchInfo* for the repository of dir (local branches first,
 * then remote branches, sorted by name, skipping origin/HEAD).
 * Returns NULL outside a repository. Caller frees with g_ptr_array_unref(). */
GPtrArray* git_list_branches(const gchar* dir);

/* Switch (checkout) the repository of dir to branch name (local or remote tracking).
 * Safe: fails without changing working tree if there are uncommitted conflicts.
 * Sets *error_msg on failure (caller frees). Returns TRUE on success. */
gboolean git_checkout_branch(const gchar* dir, const gchar* branch_name, gchar** error_msg);

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

/* The same from the output of `git status --porcelain=v1 -z --branch
 * --untracked-files=all` run elsewhere (a remote machine): the badges of
 * the paths below git_root (NULL when nothing changed) and, from its
 * "## " header, the branch (caller frees; NULL without a header) and the
 * counts ahead of and behind its upstream (FALSE: no upstream) */
GHashTable* git_parse_porcelain(const gchar* git_root, const gchar* data, gsize len, gchar** branch,
                                gboolean* has_upstream, guint* ahead, guint* behind);

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
    gchar* message;    /* whole message, without its trailing line breaks */
    gchar* author;
    gchar* date; /* committer date relative to now, like git's %cr */
    gint64 time; /* committer date, seconds since the epoch */
    gchar** parents; /* full SHAs of the parents, first parent first (NULL-terminated, empty for a root) */
} GitCommitInfo;

void git_commit_info_free(gpointer info);

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

/* Where a file of git_list_files_by_kind() comes from */
typedef enum {
    GIT_FILE_TRACKED = 0, /* in the index */
    GIT_FILE_UNTRACKED,   /* in the work tree only, not ignored */
    GIT_FILE_IGNORED      /* in the work tree only, ignored (with_ignored) */
} GitFileKind;

typedef gboolean (*GitKindFunc)(const gchar* rel, GitFileKind kind, gpointer data);

/* git_list_files(), with where each file comes from. An untracked folder
 * that is a repository of its own is listed once, as "name/". */
gboolean git_list_files_by_kind(const gchar* root, gboolean with_ignored, GitKindFunc func, gpointer data);

/* Age of a commit made seconds ago, like git's relative dates: "5 minutes
 * ago", "3 weeks ago", "1 year, 2 months ago", "in the future". Caller frees. */
gchar* git_relative_date(gint64 seconds);

#endif /* GIT_UTILS_H */
