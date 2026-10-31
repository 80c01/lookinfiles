#ifndef FOCUS_ROUTE_H
#define FOCUS_ROUTE_H

/* Where the keyboard focus is (explorer, preview, another notebook page,
 * editor tab), so that every key handler routes a key the same way; tab
 * switching that always lands the focus in the new page; the navigation keys
 * of the global mode in list pages (Git Commits, References). */
#include "app_types.h"
#include "nav_keys.h"

typedef enum {
    FOCUS_AREA_NONE = 0, /* no focus, or another panel (terminal, help, bars) */
    FOCUS_AREA_EXPLORER, /* the explorer list */
    FOCUS_AREA_PREVIEW,  /* the Preview page */
    FOCUS_AREA_PAGE,     /* another page of the preview notebook (Git Commits, References) */
    FOCUS_AREA_EDITOR    /* an editor tab (its view, command line, minibuffer...) */
} FocusArea;

typedef struct {
    FocusArea area;
    GtkWidget* focus;      /* the focused widget, or NULL */
    GtkNotebook* notebook; /* PREVIEW, PAGE, EDITOR: the notebook of the page */
    GtkWidget* page;       /* its page */
    EditorTab* tab;        /* EDITOR: the tab */
    gboolean nav_keys;     /* the focused widget takes the navigation keys of the
                            * global mode (explorer list, preview text, list pages) */
} FocusInfo;

/* A focused notebook (its tab bar) counts as its current page */
void focus_route_get(AppData* app, FocusInfo* info);

/* The editor tab shown in page, or NULL */
EditorTab* focus_route_page_tab(AppData* app, GtkWidget* page);

/* Focus the main widget of a notebook page: the editor view, the preview
 * text, the list of a list page */
void focus_route_focus_page(AppData* app, GtkWidget* page);

/* Next (step 1) / previous (-1) visible page of nb, wrapping around; with
 * focus, the focus goes into the new page. FALSE when there is no other page. */
gboolean focus_route_step_tab(AppData* app, GtkNotebook* nb, gint step, gboolean focus);

/* The widget takes the navigation keys of the global mode (FocusInfo.nav_keys) */
void focus_route_mark_nav(GtkWidget* widget);

/* The focus moved (notify::focus-widget): drop the explorer / viewer prefix
 * key, and move the focus from a notebook's tab bar into its current page */
void focus_route_focus_changed(AppData* app);

/* Navigation keys of the global mode in a list page: next / previous row,
 * first / last, page down / up, panel and tab keys */
void focus_route_attach_list(AppData* app, GtkWidget* list, GtkSingleSelection* selection);

#endif /* FOCUS_ROUTE_H */
