#ifndef GIT_CHANGES_H
#define GIT_CHANGES_H

#include "core/app_types.h"

/* The Changes view of the Git tab: the uncommitted changes of the work tree
 * (staged, not staged, untracked; git_worktree) and the patch of the file
 * selected below them. Read on a worker thread only while the view is on
 * screen; each time it comes on screen it reads them again. */
typedef struct GitChanges GitChanges;

/* After each read: how many files are listed (n), for the view's button */
typedef void (*GitChangesCountFunc)(guint n, gpointer data);

/* A new view; freed with its widget */
GitChanges* git_changes_new(AppData* app, GitChangesCountFunc counted, gpointer data);

/* The widget of the view */
GtkWidget* git_changes_widget(GitChanges* c);

/* Show the changes of the work tree at root (NULL: none), read again now if
 * the view is on screen, else when it is shown */
void git_changes_set_root(GitChanges* c, const gchar* root);

/* Cancel the reads running (at exit) */
void git_changes_cancel(GitChanges* c);

#endif /* GIT_CHANGES_H */
