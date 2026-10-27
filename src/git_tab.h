#ifndef GIT_TAB_H
#define GIT_TAB_H

#include "app_types.h"

/* Initialize the Git commits tab in the preview panel notebook */
void git_tab_init(AppData* app);

/* Update the Git tab availability and commit listing for the given directory */
void git_tab_update(AppData* app, const gchar* dir);

/* Clean up Git tab resources */
void git_tab_cleanup(AppData* app);

#endif /* GIT_TAB_H */
