#ifndef GIT_TAB_H
#define GIT_TAB_H

#include "core/app_types.h"

/* Initialize the Git tab in the preview panel notebook */
void git_tab_init(AppData* app);

/* Show the Git tab for dir's repository; reloads the history only when the
 * repository root changes */
void git_tab_update(AppData* app, const gchar* dir);
/* Reload the history of the current repository (after .git changed) */
void git_tab_reload(AppData* app);

/* The History view of the Git tab in front, with only the commits that
 * changed path (a file or folder of the current folder's repository; its
 * root: every commit); a notice when path is in another repository */
void git_tab_show_history(AppData* app, const gchar* path);

/* Notebook page of the Git tab, or NULL before git_tab_init() */
GtkWidget* git_tab_page(AppData* app);

/* Cancel the pending tasks and free the Git tab state (at exit) */
void git_tab_cleanup(AppData* app);

#endif /* GIT_TAB_H */
