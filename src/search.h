#ifndef SEARCH_H
#define SEARCH_H

#include "app_types.h"

/* Search state: created before the bars, freed at exit */
void search_init(AppData* app);
void search_free(AppData* app);
/* Build the search bar of the explorer or of the preview; returns its widget,
 * to put at the bottom of the panel. Its look follows the global mode. */
GtkWidget* search_bar_new(AppData* app, gboolean in_viewer);
/* The preview shows something else: the next search starts from the top */
void search_forget_viewer_match(AppData* app);
/* Open the search bar of the explorer or of the viewer; backward (Emacs C-r)
 * searches upward as you type */
void show_search(AppData* app, gboolean from_viewer, gboolean backward);
void hide_search(AppData* app);
/* The global mode changed: close an open bar (its keys and look are those of
 * the mode it was opened in) */
void search_mode_changed(AppData* app);
/* Forget the explorer search (the listing changed) */
void search_clear_explorer(AppData* app);
/* Next (+1) / previous (-1) match of the open or last search */
void search_again(AppData* app, gint direction);
void do_search(AppData* app, gint direction);
gint utf8_ci_find_from(const gchar* hay, const gchar* needle, gint start);
void do_viewer_search(AppData* app, gint direction);

#endif /* SEARCH_H */
