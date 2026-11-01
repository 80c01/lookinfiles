#ifndef HELP_GUIDE_H
#define HELP_GUIDE_H

/* The Help tab: the user guide (user_guide.h) rendered as Markdown in a page
 * of the preview notebook, after the Git, result and Canvas pages. F1 and a
 * left click on the top bar's Help button open it (a click on the button
 * while the tab is shown closes it); the keyboard cheat sheet
 * (help.h) stays on ? and the button's right click. The navigation keys of
 * the global mode scroll it; its close button, a middle click on the tab,
 * Ctrl+W (Vim Ctrl+W c) close it. */
#include "app_types.h"

/* Show the guide at page (a user_guide page name, NULL: the table of
 * contents), opening the tab when needed; the focus goes into it */
void help_guide_show(AppData* app, const gchar* page);

/* TRUE when the Help tab is open */
gboolean help_guide_is_open(AppData* app);

/* Close the Help tab if it is the current page of the preview notebook
 * (Ctrl+W, the Help button). Returns TRUE if it was closed. */
gboolean help_guide_close_shown(AppData* app);

#endif /* HELP_GUIDE_H */
