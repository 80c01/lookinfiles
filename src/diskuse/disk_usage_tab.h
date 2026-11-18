#ifndef DISK_USAGE_TAB_H
#define DISK_USAGE_TAB_H

/* The Disk Usage tab: the room a folder and everything below it take, in
 * a page of the preview notebook (after the Git tab and the result pages).
 * Opened on a folder from the explorer (Shift+S, the context menu). The
 * tree is read in a worker thread (disk_scan.h) with a progress bar and the
 * folders done so far; then its breakdown (disk_usage_list.h) goes down
 * into the folders (Enter, Right) and back up (Backspace, Left), and the
 * statistics of the whole folder are shown beside it (disk_usage_stats.h).
 * F5 reads it again, Escape stops the reading (what was read stays). The
 * close button, a middle click on the tab, Ctrl+W (Vim Ctrl+W c) close it
 * and stop the reading. Each workspace has its own tab; opening it on
 * another folder replaces the folder analysed. */
#include "core/app_types.h"

/* Analyse path (a folder) in the tab, opened when needed and shown; the
 * focus goes into it */
void disk_usage_tab_show(AppData* app, const gchar* path);

/* Close the tab if it is the current page of the preview notebook (Ctrl+W).
 * Returns TRUE if it was closed. */
gboolean disk_usage_tab_close_shown(AppData* app);

/* Workspace switch (workspace.c): the tab of the workspace left is put
 * aside, that of the workspace shown comes back */
void disk_usage_tab_workspace_hidden(AppData* app, VirtualLayout* ws);
void disk_usage_tab_workspace_shown(AppData* app, VirtualLayout* ws);
/* A workspace is removed: its tab goes */
void disk_usage_tab_workspace_removed(AppData* app, VirtualLayout* ws);

#endif /* DISK_USAGE_TAB_H */
