#ifndef GIT_LOG_H
#define GIT_LOG_H

/* The history of a repository through libgit2 (no git program), filtered by
 * words and by a path. No AppData and no widgets; worker threads only. */
#include "git/git_utils.h"

/* The words of a history filter as typed (separated by spaces), casefolded;
 * NULL when text holds none (every commit). g_strfreev() */
gchar** git_log_words(const gchar* text);

/* max commits reachable from HEAD of the repository at root, newest first
 * with every commit before its parents (like `git log --topo-order`),
 * after the skip newest ones (to read the history page by page), keeping
 * only those whose message, author (name or email) or full hash
 * hold every word of words (git_log_words(); NULL keeps all) and, when path
 * is not NULL, that changed path (relative to root, '/' separators, a file
 * or a folder; like `git log -- path`, renames not followed), however far
 * back in the history (skip counts kept commits only): array of
 * GitCommitInfo*, empty without commits, NULL when root is not a repository
 * or cancellable was cancelled. *more (may be NULL) is set when more commits
 * would have been kept. */
GPtrArray* git_read_log(const gchar* root, guint skip, guint max, gchar** words, const gchar* path,
                        GCancellable* cancellable, gboolean* more);

#endif /* GIT_LOG_H */
