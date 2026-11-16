#ifndef BROWSER_H
#define BROWSER_H

/* Browser tabs: web pages in pages of the preview notebook (moved like an
 * editor tab: dragged to another pane or a split, Ctrl+Alt+Arrow, the tab
 * menu), shown by
 * WebKitGTK 6.0 (optional: HAVE_WEBKIT). Each tab has back / forward /
 * reload buttons, an address bar (web_address.h: an address, a path or words
 * to search) and the page; a link or popup opened in a new window becomes a
 * new tab, links WebKit cannot show go to the default application. The
 * page's own requests stay inside the window: no print dialog, no file
 * chooser, no fullscreen, permissions (camera, location...) denied.
 * Ctrl+Shift+B opens an empty tab; in a tab Ctrl+L goes to the address bar,
 * F5 reloads, Escape stops loading; its close button, a middle click on the
 * tab, Ctrl+W close it. Each workspace has its own browser tabs, saved
 * with the session (editor_session.h: their address and place). */
#include "core/app_types.h"

/* TRUE when built with WebKitGTK (browser tabs available) */
gboolean browser_available(void);

/* Open uri in a new browser tab, shown with the focus in the page (NULL: an
 * empty tab, the focus in the address bar). Without WebKit: uri goes to the
 * default browser, and NULL shows a notice. */
void browser_open(AppData* app, const gchar* uri);
/* An empty browser tab, last page of nb (NULL: the preview notebook), shown
 * with the focus in the address bar (the + menu of a tab bar). Without
 * WebKit: a notice. */
void browser_new_tab_in(AppData* app, GtkNotebook* nb);

/* The address of the browser tab page (a notebook page), NULL when page is
 * not a browser tab or shows no page yet */
const gchar* browser_page_uri(GtkWidget* page);
/* A new browser tab loading uri, last page of nb, not shown (the session
 * reopening it); NULL without WebKit */
GtkWidget* browser_open_in(AppData* app, const gchar* uri, GtkNotebook* nb);

/* Close the browser tab with the focus, else the one shown as the current
 * page of the preview notebook (Ctrl+W). Returns TRUE if it was closed. */
gboolean browser_close_shown(AppData* app);

/* Workspace switch (workspace.c): the browser tabs of the workspace left are
 * put aside, those of the workspace shown come back */
void browser_workspace_hidden(AppData* app, VirtualLayout* ws);
void browser_workspace_shown(AppData* app, VirtualLayout* ws);
/* A workspace is removed: its browser tabs go */
void browser_workspace_removed(AppData* app, VirtualLayout* ws);

#endif /* BROWSER_H */
