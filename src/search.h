#ifndef SEARCH_H
#define SEARCH_H

#include "app_types.h"

/* Build a search bar (explorer or preview) into sb; returns its widget, to
 * put at the bottom of the panel. Its look follows the global mode. */
GtkWidget* search_bar_new(AppData* app, AppSearchBar* sb);
/* Open the search bar of the explorer or of the viewer; backward (Emacs C-r)
 * searches upward as you type */
void show_search(AppData* app, gboolean from_viewer, gboolean backward);
void hide_search(AppData* app);
/* Forget the explorer search (the listing changed) */
void search_clear_explorer(AppData* app);
/* Next (+1) / previous (-1) match of the open or last search */
void search_again(AppData* app, gint direction);
void do_search(AppData* app, gint direction);
gint utf8_ci_find_from(const gchar* hay, const gchar* needle, gint start);
void do_viewer_search(AppData* app, gint direction);

#endif /* SEARCH_H */
