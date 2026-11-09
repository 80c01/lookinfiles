#ifndef OUTLINE_PANE_H
#define OUTLINE_PANE_H

/* A tagbar beside some content (docs/code-nav.md): a resizable pane listing
 * an outline (code_outline_tree.h), symbols grouped by kind or in file
 * order, or headings nested by level. The owner gives the entries and the
 * line of its cursor; the pane builds the tree, keeps the rows the user
 * collapsed, follows the cursor and calls back to go to an entry. The list
 * takes the navigation keys of the owner's mode: moving (j / k, gg / G,
 * h / l close / open a row, C-n / C-p...) and searching the names (/ n N,
 * C-s / C-r, Ctrl+F / F3) in a bar at its bottom, collapsed rows included.
 * Used by the editor tabs (editor_tagbar.h) and the preview
 * (preview_tagbar.h). */
#include <gtk/gtk.h>

#include "code/code_outline_tree.h"
#include "explorer/nav_keys.h"

typedef struct OutlinePane OutlinePane;

typedef struct {
    /* Enter or a click on an entry (never a group) */
    void (*go_to)(gpointer data, const CodeOutlineEntry* entry);
    /* The line the owner's cursor is in (0-based), or -1 */
    gint (*cursor_line)(gpointer data);
    /* Escape in the list: focus back in the content */
    void (*escape)(gpointer data);
    /* q in the list, or the close button: hide the tagbar */
    void (*close)(gpointer data);
    /* The navigation keys of the list (nav_keys.h): moving, searching */
    NavStyle (*nav_style)(gpointer data);
    /* A panel or tab key of that mode in the list (NAV_PANEL_NEXT / _PREV,
     * NAV_TAB_NEXT / _PREV, NAV_TAB_CLOSE: focus_route_panel_action()) */
    void (*panel_action)(gpointer data, NavAction action);
} OutlinePaneCallbacks;

/* Put content and a hidden tagbar side by side. width holds the tagbar
 * width (shared by every pane using it; kept when the window is resized,
 * updated when the user drags the handle); loading is the text shown
 * before the first outline. The pane lives as long as its widget. */
OutlinePane* outline_pane_new(GtkWidget* content, gint* width, const gchar* loading,
                              const OutlinePaneCallbacks* callbacks, gpointer data);
/* The GtkPaned to add to the owner's container */
GtkWidget* outline_pane_widget(OutlinePane* pane);
/* Stop calling back (the owner is gone; the widget may live a little longer) */
void outline_pane_detach(OutlinePane* pane);

/* Show the outline: entries (CodeOutlineEntry* sorted by line, referenced)
 * are headings (title "Headings", no sort button) or symbols read by
 * source (the tooltip of the title) */
void outline_pane_set_entries(OutlinePane* pane, GPtrArray* entries, gboolean headings, const gchar* source);
/* TRUE once entries were given */
gboolean outline_pane_has_entries(OutlinePane* pane);
/* Select the row of the entry the owner's cursor is in */
void outline_pane_follow_cursor(OutlinePane* pane);

void outline_pane_set_shown(OutlinePane* pane, gboolean shown);
/* Move the handle to the shared width (after it changed elsewhere) */
void outline_pane_place(OutlinePane* pane);
gboolean outline_pane_shown(OutlinePane* pane);
/* Focus the list; whether the list has the focus */
void outline_pane_focus(OutlinePane* pane);
gboolean outline_pane_has_focus(OutlinePane* pane);
/* The list (the widget focused by outline_pane_focus()); mapped while the
 * tagbar is on screen with its entries */
GtkWidget* outline_pane_list(OutlinePane* pane);

#endif /* OUTLINE_PANE_H */
