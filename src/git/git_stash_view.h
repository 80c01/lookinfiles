#ifndef GIT_STASH_VIEW_H
#define GIT_STASH_VIEW_H

#include "core/app_types.h"

/* The Stashes & Tags view of the Git tab: the stashes, newest first, and
 * the tags, newest first, in two groups that fold (git_stash_tags); the
 * entry selected shows below what it holds (a stash its patch, a tag its
 * message and its commit). Read only. Read on a worker thread each time the
 * view comes on screen, and on each update while shown. */
typedef struct GitStashView GitStashView;

/* A new view; freed with its widget */
GitStashView* git_stash_view_new(AppData* app);

GtkWidget* git_stash_view_widget(GitStashView* v);

/* Show the repository at root (NULL: none), read again now if the view is
 * on screen, else when it is shown */
void git_stash_view_set_root(GitStashView* v, const gchar* root);

/* Cancel the reads running (at exit) */
void git_stash_view_cancel(GitStashView* v);

#endif /* GIT_STASH_VIEW_H */
