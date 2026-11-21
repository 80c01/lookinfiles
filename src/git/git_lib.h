#ifndef GIT_LIB_H
#define GIT_LIB_H

/* What the libgit2 readers share (git_utils, git_worktree): libgit2 set up
 * once, a work tree opened, a patch printed as git prints it. No AppData and
 * no widgets; safe on worker threads. */
#include <glib.h>
#include <git2.h>

/* libgit2 is initialised (once, until the process exits) */
void git_lib_ready(void);

/* The repository whose work tree starts at root, NULL if none (or bare);
 * free with git_repository_free() */
git_repository* git_lib_open(const gchar* root);

/* git_diff_print() callback (data: a GString): the patch text as git prints
 * it (hunk headers without trailing blanks, no "---" / "+++" for files
 * empty on both sides) */
int git_lib_patch_line(const git_diff_delta* delta, const git_diff_hunk* hunk, const git_diff_line* line,
                       void* data);

/* Shortest unique abbreviation of an object, like git's %h. g_free() */
gchar* git_lib_short_id(const git_object* obj);

/* git's default date format, in the signature's time zone and in English
 * whatever the locale: "Thu Nov 5 14:03:12 2026 +0100". g_free() */
gchar* git_lib_signature_date(const git_time* when);

#endif /* GIT_LIB_H */
