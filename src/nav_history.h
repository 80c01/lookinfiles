#ifndef NAV_HISTORY_H
#define NAV_HISTORY_H

/* Where the user has been: the folders each workspace's explorer showed
 * (back / forward: Shift+H / Shift+L, the mouse's back / forward buttons),
 * and the recent folders and files of every session (recent.ini, Alt+R,
 * quick open). The explorer reports its folder changes; going back or
 * forward returns the folder to show, the explorer shows it. */
#include "app_types.h"
#include "session_store.h"

/* The explorer's folder changed (file_list.c): history and recent folders */
void nav_history_folder_changed(AppData* app, const gchar* old_dir, const gchar* new_dir);
/* A file was opened in an editor tab or chosen in quick open */
void nav_history_file_opened(AppData* app, const gchar* path);

/* Folder before / after the current one in this workspace's history
 * (missing folders skipped), or NULL; the next folder change is that walk
 * itself. Caller frees. */
gchar* nav_history_back(AppData* app);
gchar* nav_history_forward(AppData* app);

/* Recent folders and files, most recent first (loaded on first use) */
const RecentList* nav_history_recent(AppData* app);

/* Free the history of a workspace being removed */
void nav_history_workspace_removed(VirtualLayout* ws);

#endif /* NAV_HISTORY_H */
