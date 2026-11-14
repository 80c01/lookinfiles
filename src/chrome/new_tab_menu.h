#ifndef NEW_TAB_MENU_H
#define NEW_TAB_MENU_H

#include "core/app_types.h"

/* The + button at the right end of the tab bar of the preview notebook and
 * of every editor notebook: its menu opens a new tab in that notebook, a
 * web page (always in the preview notebook: browser tabs live there), a
 * Canvas note, a Jupyter notebook in memory, an empty Vim, Emacs or Notepad
 * editor tab, or a terminal. */

/* Put the + button on nb's tab bar */
void new_tab_menu_attach(AppData* app, GtkNotebook* nb);

/* Also on each editor notebook made from now on (editor_set_notebook_hook()) */
void new_tab_menu_init(void);

#endif /* NEW_TAB_MENU_H */
