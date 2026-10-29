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

/* Run 'git status' on the repository of dir (and nested repositories) and return
 * full_path -> Pango badge markup, NULL outside a repository. Spawns git: call
 * from a worker thread. Caller destroys the table. */
GHashTable* scan_git_statuses(const gchar* dir);

/* Content of file path in the last commit (HEAD) of its repository, *len its
 * size. NULL outside a repository or when HEAD has no such file (untracked,
 * new). Spawns git: call from a worker thread. Caller frees. */
gchar* git_read_head_blob(const gchar* path, gsize* len);

#endif /* GIT_UTILS_H */
