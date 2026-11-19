#ifndef DEEP_SEARCH_TAB_H
#define DEEP_SEARCH_TAB_H

/* The Deep Search tab: the entries below a folder found by name,
 * extensions or category (images, videos...), kind, size, date of last
 * change and content, with no limit (deep_search.h), in a page of the preview notebook (after the result
 * pages). Opened on a folder from the explorer (Shift+F, the context menu)
 * or the button of the folder preview (dir_preview.c). The criteria are a
 * form over the results (deep_search_list.h); Enter in a field starts the
 * search, run in a worker thread, the results listed as they are found.
 * F5 searches again, Escape stops the search (what was found stays).
 * The close button, a middle click on the tab, Ctrl+W (Vim Ctrl+W c)
 * close it and stop the search. Each workspace has its own tab; opening
 * it on another folder keeps the criteria. */
#include "core/app_types.h"

/* Show the tab with path (a folder) as the folder to search, opened when
 * needed; the focus goes to the name field. Nothing is searched yet. */
void deep_search_tab_show(AppData* app, const gchar* path);

/* Close the tab if it is the current page of the preview notebook (Ctrl+W).
 * Returns TRUE if it was closed. */
gboolean deep_search_tab_close_shown(AppData* app);

/* Workspace switch (workspace.c): the tab of the workspace left is put
 * aside, that of the workspace shown comes back */
void deep_search_tab_workspace_hidden(AppData* app, VirtualLayout* ws);
void deep_search_tab_workspace_shown(AppData* app, VirtualLayout* ws);
/* A workspace is removed: its tab goes */
void deep_search_tab_workspace_removed(AppData* app, VirtualLayout* ws);

#endif /* DEEP_SEARCH_TAB_H */
