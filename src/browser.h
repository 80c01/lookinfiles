#ifndef BROWSER_H
#define BROWSER_H

/* Browser tabs: web pages in pages of the preview notebook, shown by
 * WebKitGTK 6.0 (optional: HAVE_WEBKIT). Each tab has back / forward /
 * reload buttons, an address bar (web_address.h: an address, a path or words
 * to search) and the page; a link or popup opened in a new window becomes a
 * new tab, links WebKit cannot show go to the default application. The
 * page's own requests stay inside the window: no print dialog, no file
 * chooser, no fullscreen, permissions (camera, location...) denied.
 * Ctrl+Shift+B opens an empty tab; in a tab Ctrl+L goes to the address bar,
 * F5 reloads, Escape stops loading; its close button, a middle click on the
 * tab, Ctrl+W close it. Each workspace has its own browser tabs. */
#include "app_types.h"

/* TRUE when built with WebKitGTK (browser tabs available) */
gboolean browser_available(void);

/* Open uri in a new browser tab, shown with the focus in the page (NULL: an
 * empty tab, the focus in the address bar). Without WebKit: uri goes to the
 * default browser, and NULL shows a notice. */
void browser_open(AppData* app, const gchar* uri);

/* Close the browser tab if it is the current page of the preview notebook
 * (Ctrl+W). Returns TRUE if it was closed. */
gboolean browser_close_shown(AppData* app);

/* Workspace switch (workspace.c): the browser tabs of the workspace left are
 * put aside, those of the workspace shown come back */
void browser_workspace_hidden(AppData* app, VirtualLayout* ws);
void browser_workspace_shown(AppData* app, VirtualLayout* ws);
/* A workspace is removed: its browser tabs go */
void browser_workspace_removed(AppData* app, VirtualLayout* ws);

#endif /* BROWSER_H */
