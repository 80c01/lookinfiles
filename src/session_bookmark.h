#ifndef SESSION_BOOKMARK_H
#define SESSION_BOOKMARK_H

/* Workspace bookmarks: the current workspace (folder, expanded folders,
 * editor tabs, panels, terminal panes: session.h) saved under a name in the
 * bookmarks, opened again in a new workspace with a click. The workspace is
 * kept in sessions/<id>.ini (session_store.h); the bookmark points to it
 * with a "lookinfiles-session://<id>" target, and the file goes with the
 * bookmark when it is deleted. The bookmarks view asks through
 * APP_EVENT_SAVE_WORKSPACE_BOOKMARK / APP_EVENT_OPEN_WORKSPACE_BOOKMARK. */
#include "app_types.h"

/* Ask for a name, save the current workspace and add the bookmark */
void session_bookmark_save(AppData* app);
/* Open the workspace of app->session_open_target in a new workspace (the
 * target is taken); a message says why when it cannot be opened */
void session_bookmark_open(AppData* app);

#endif /* SESSION_BOOKMARK_H */
