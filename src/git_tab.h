#ifndef GIT_TAB_H
#define GIT_TAB_H

#include "core/app_types.h"

/* Initialize the Git commits tab in the preview panel notebook */
void git_tab_init(AppData* app);

/* Update the Git tab availability and commit listing for the given directory */
/* Show the Git tab for dir's repository; reloads the history only when the
 * repository root changes */
void git_tab_update(AppData* app, const gchar* dir);
/* Reload the history of the current repository (after .git changed) */
void git_tab_reload(AppData* app);

/* Notebook page of the Git tab, or NULL before git_tab_init() */
GtkWidget* git_tab_page(AppData* app);

/* Cancel the pending tasks and free the Git tab state (at exit) */
void git_tab_cleanup(AppData* app);

#endif /* GIT_TAB_H */
