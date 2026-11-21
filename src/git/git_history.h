#ifndef GIT_HISTORY_H
#define GIT_HISTORY_H

#include "core/app_types.h"

/* The History view of the Git tab: the commits of a repository, newest
 * first, read a page at a time, with a filter, and the diff of the selected
 * commit below. The history is read only while the view is on screen. */
typedef struct GitHistory GitHistory;

/* A new view; freed with its widget */
GitHistory* git_history_new(AppData* app);

/* The widget of the view */
GtkWidget* git_history_widget(GitHistory* h);

/* Show the history of the repository at root (a work tree root, NULL: none);
 * reload: read it again even when root is the one shown, with as many
 * commits as are listed */
void git_history_set_root(GitHistory* h, const gchar* root, gboolean reload);

/* Only the commits that changed path (relative to the root, '/'
 * separators; a file or a folder), shown beside the filter with a cross
 * that clears it; NULL: every commit. Another root clears it. */
void git_history_set_path(GitHistory* h, const gchar* path);

/* Cancel the reads running (at exit) */
void git_history_cancel(GitHistory* h);

#endif /* GIT_HISTORY_H */
