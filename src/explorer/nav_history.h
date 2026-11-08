#ifndef NAV_HISTORY_H
#define NAV_HISTORY_H

/* Where the user has been: the folders each workspace's explorer showed
 * (back / forward: Shift+H / Shift+L, the mouse's back / forward buttons),
 * and the recent folders and files of every session (recent.ini, Alt+R,
 * quick open). The explorer reports its folder changes; going back or
 * forward returns the folder to show, the explorer shows it. */
#include "core/app_types.h"
#include "workspace/session_store.h"

/* The explorer's folder changed (file_list.c): history and recent folders.
 * selected: the row selected in old_dir (or NULL), shown again when the
 * history comes back to old_dir. */
void nav_history_folder_changed(AppData* app, const gchar* old_dir, const gchar* new_dir,
                                const gchar* selected);
/* A file was opened in an editor tab or chosen in quick open */
void nav_history_file_opened(AppData* app, const gchar* path);

/* Folder before / after the current one in this workspace's history
 * (missing folders skipped), or NULL; the next folder change is that walk
 * itself. Caller frees. */
gchar* nav_history_back(AppData* app);
gchar* nav_history_forward(AppData* app);
/* Several folders back / forward at once (steps >= 1, the history menu of
 * the top bar buttons): the folder to show, or NULL if it is gone. Caller
 * frees. */
gchar* nav_history_go(AppData* app, gboolean back, guint steps);
/* Whether there is a folder before / after the current one */
gboolean nav_history_can_go(AppData* app, gboolean back);
/* The folders before / after the current one, nearest first (gchar*,
 * deleted ones included): entry i is nav_history_go(app, back, i + 1).
 * Free with g_ptr_array_unref(). */
GPtrArray* nav_history_folders(AppData* app, gboolean back);
/* Name of the entry of dir selected when the explorer left it, or NULL */
const gchar* nav_history_selection(AppData* app, const gchar* dir);

/* Recent folders and files, most recent first (loaded on first use) */
const RecentList* nav_history_recent(AppData* app);

/* Free the history of a workspace being removed */
void nav_history_workspace_removed(VirtualLayout* ws);

#endif /* NAV_HISTORY_H */
