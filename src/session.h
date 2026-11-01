#ifndef SESSION_H
#define SESSION_H

/* The session between launches (session_store.h): at quit, each workspace's
 * folder, expanded folders, selected entry, hidden files, sort, open files
 * and where they are (notebooks, splits, current pages: editor_session.h),
 * panels, split sizes and terminal panes are saved; a launch without a file
 * or folder argument restores them (terminals with new shells). Missing
 * folders and files are skipped. */
#include "app_types.h"

/* Save the workspaces (Ctrl+Q, window closed) */
void session_save_now(AppData* app);
/* Restore the saved workspaces; FALSE when there is nothing to restore */
gboolean session_restore(AppData* app);

#endif /* SESSION_H */
