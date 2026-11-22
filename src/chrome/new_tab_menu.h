#ifndef NEW_TAB_MENU_H
#define NEW_TAB_MENU_H

#include "core/app_types.h"

/* The + button at the right end of the tab bar of the preview notebook and
 * of every editor notebook: its menu opens a new tab in that notebook, the
 * Applications tab (launcher_tab.h: web browser, Jupyter notebook, Canvas,
 * Org-mode, Vim, Emacs, Notepad...), an empty editor tab in the mode of the
 * Preferences, a terminal, or the Diagnostics tab (diagnostics_tab.h). */

/* Put the + button on nb's tab bar */
void new_tab_menu_attach(AppData* app, GtkNotebook* nb);

/* Also on each editor notebook made from now on (editor_set_notebook_hook()) */
void new_tab_menu_init(void);

#endif /* NEW_TAB_MENU_H */
