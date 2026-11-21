#ifndef GIT_REPO_VIEW_H
#define GIT_REPO_VIEW_H

#include "core/app_types.h"

/* The Repository view of the Git tab: the identity commits are made with
 * (and the level it comes from), the remotes, the local branches with their
 * upstream and how far ahead or behind, and the whole configuration with a
 * filter and the level of each entry (git_repo_info; secrets masked).
 * Read only; buttons open .git/config and the global config in an editor
 * tab (APP_EVENT_EDIT_PATH). Read on a worker thread each time the view
 * comes on screen, and on a reload while shown. */
typedef struct GitRepoView GitRepoView;

/* A new view; freed with its widget */
GitRepoView* git_repo_view_new(AppData* app);

GtkWidget* git_repo_view_widget(GitRepoView* v);

/* Show the repository at root (NULL: none); reload: read again even when
 * root is the one shown (.git changed) */
void git_repo_view_set_root(GitRepoView* v, const gchar* root, gboolean reload);

/* Cancel the read running (at exit) */
void git_repo_view_cancel(GitRepoView* v);

#endif /* GIT_REPO_VIEW_H */
