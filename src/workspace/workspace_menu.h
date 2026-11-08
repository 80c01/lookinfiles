#ifndef WORKSPACE_MENU_H
#define WORKSPACE_MENU_H

/* The menu of a box of the top-bar workspace map (right click): Save as
 * Bookmark… (session_bookmark.h) and Close Workspace. */
#include "core/app_types.h"

typedef void (*WorkspaceCloseFn)(AppData* app, gint idx);

/* Pop the menu of workspace idx up at (x, y) of map; close is called for
 * Close Workspace (workspace_remove()) */
void workspace_menu_popup(AppData* app, GtkWidget* map, gdouble x, gdouble y, gint idx,
                          WorkspaceCloseFn close);

#endif /* WORKSPACE_MENU_H */
