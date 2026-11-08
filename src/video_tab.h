#ifndef VIDEO_TAB_H
#define VIDEO_TAB_H

#include "core/app_types.h"

/* Video tabs: a video file played in a tab of the editor panel (Enter on a
 * video in the explorer), one tab per file. The tab moves like an editor tab
 * (dragged to another pane, a split, the preview notebook; Ctrl+Alt+Arrow)
 * and belongs to the workspace it was opened in. The preview's own player
 * (Space in the explorer) is separate and stays as it is.
 *
 * In the tab: Space / k play or pause, Left / Right seek 5 s back / forward,
 * Home goes back to the start; a middle click on the tab, its menu (right
 * click) or Ctrl+W close it. A tab out of sight (another tab shown,
 * another workspace) pauses. */

/* Show the tab of the video at path (name: its file name), opened in the
 * editor panel and played when it is not open; a tab of another workspace
 * moves to this one */
void video_tab_open(AppData* app, const gchar* name, const gchar* path);

/* Close the Video tab with the focus, else the one shown as the current
 * page of the preview notebook (Ctrl+W). Returns TRUE if one was closed. */
gboolean video_tab_close_shown(AppData* app);

/* Video tabs of the preview notebook leave it with their workspace
 * (hidden), come back with it (shown), or close with it (removed). Those of
 * the editor panel go with the panel (editor_detach_tabs()). */
void video_tab_workspace_hidden(AppData* app, VirtualLayout* ws);
void video_tab_workspace_shown(AppData* app, VirtualLayout* ws);
void video_tab_workspace_removed(AppData* app, VirtualLayout* ws);

#endif /* VIDEO_TAB_H */
