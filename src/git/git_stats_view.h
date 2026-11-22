#ifndef GIT_STATS_VIEW_H
#define GIT_STATS_VIEW_H

#include "core/app_types.h"

/* The Statistics view of the Git tab: how many commits, by whom, over the
 * last year (a square per day), by day of the week and hour, by month
 * (git_stats, git_stats_charts), then the files of HEAD by language
 * (git_code_view). The whole history is read on a worker thread only while
 * the view is on screen. */
typedef struct GitStatsView GitStatsView;

/* A new view; freed with its widget */
GitStatsView* git_stats_view_new(AppData* app);

GtkWidget* git_stats_view_widget(GitStatsView* v);

/* Count the history and the files of the repository at root (NULL: none);
 * reload: again even when root is the one shown (commits were made) */
void git_stats_view_set_root(GitStatsView* v, const gchar* root, gboolean reload);

/* Cancel the read running (at exit) */
void git_stats_view_cancel(GitStatsView* v);

#endif /* GIT_STATS_VIEW_H */
