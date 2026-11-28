#ifndef WELCOME_TAB_H
#define WELCOME_TAB_H

/* The Welcome tab: a page of the preview notebook for new users, opened at
 * the first launch (no configuration folder yet) and at every launch when
 * Preferences → Window → Show the Welcome tab at startup is checked
 * ([Window] welcome of settings.ini). It sets the theme (applied as it is
 * chosen) and the editor keys (Notepad, Vim, Emacs: the global mode and the
 * mode of new editor tabs), lists the essential shortcuts of that mode, and
 * opens the user guide, the Diagnostics tab and the Preferences. Its own
 * check button is the startup setting. Reopened from the + menu of the tab
 * bars and the user guide's lookinfiles:welcome links. The close button, a
 * middle click on the tab, Ctrl+W (Vim Ctrl+W c) close it. Not saved with
 * the session; each workspace keeps its own. */
#include "core/app_types.h"

/* Open the tab when needed and show it; the focus goes into it */
void welcome_tab_show(AppData* app);

/* Close the tab if it is the current page of the preview notebook (Ctrl+W).
 * Returns TRUE if it was closed. */
gboolean welcome_tab_close_shown(AppData* app);

/* Workspace switch (workspace.c): the tab of the workspace left is put
 * aside, that of the workspace shown comes back */
void welcome_tab_workspace_hidden(AppData* app, VirtualLayout* ws);
void welcome_tab_workspace_shown(AppData* app, VirtualLayout* ws);
/* A workspace is removed: its tab goes */
void welcome_tab_workspace_removed(AppData* app, VirtualLayout* ws);

#endif /* WELCOME_TAB_H */
