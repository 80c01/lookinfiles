#ifndef SESSION_H
#define SESSION_H

/* The session between launches (session_store.h): at quit, each workspace's
 * folder, expanded folders, selected entry, hidden files, sort, open files
 * and where they are (notebooks, splits, current pages: editor_session.h),
 * panels, split sizes and terminal panes are saved; a launch without a file
 * or folder argument restores them (terminals with new shells). Missing
 * folders and files are skipped. */
#include "core/app_types.h"

/* Save the workspaces (Ctrl+Q, window closed) */
void session_save_now(AppData* app);
/* Restore the saved workspaces; FALSE when there is nothing to restore */
gboolean session_restore(AppData* app);

/* One workspace on its own, in a file of the session format (a workspace
 * bookmark, see session_bookmark.h). Save workspace idx to path. */
gboolean session_save_workspace(AppData* app, gint idx, const gchar* path, GError** error);
/* Open the workspace saved in path in a new workspace, which is shown. FALSE
 * (error set, nothing opened) when the file is unreadable, its folder is
 * gone or all workspaces are in use; missing tabs are skipped. */
gboolean session_open_workspace(AppData* app, const gchar* path, GError** error);

#endif /* SESSION_H */
