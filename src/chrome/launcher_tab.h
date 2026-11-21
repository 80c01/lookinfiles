#ifndef LAUNCHER_TAB_H
#define LAUNCHER_TAB_H

/* The Applications tab (Application entry of the + menu, new_tab_menu.h):
 * a list of the applications a tab can hold (web browser, Jupyter notebook,
 * Canvas, Org-mode, Vim, Emacs, Notepad, System Monitor), filtered by the
 * search field that has the focus. Up / Down / Page Up / Page Down move in
 * the list from the field, Enter or a click opens the application, which
 * takes the place of the tab in its notebook. Closed with Ctrl+W (Vim
 * Ctrl+W c) or a middle click on the tab. It is not saved with the session,
 * and closes when its workspace is hidden. */
#include "core/app_types.h"

/* Open the tab as the last page of nb (the preview notebook or an editor
 * notebook) and put the focus in its search field */
void launcher_tab_open(AppData* app, GtkNotebook* nb);

/* Close the tab that has the focus, else the current page of the preview
 * notebook when it is one (Ctrl+W). Returns TRUE if one was closed. */
gboolean launcher_tab_close_shown(AppData* app);

/* Workspace switch (workspace.c): the tabs of the preview notebook close */
void launcher_tab_workspace_hidden(AppData* app);

#endif /* LAUNCHER_TAB_H */
