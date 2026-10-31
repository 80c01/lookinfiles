#ifndef SEARCH_H
#define SEARCH_H

#include "app_types.h"

/* Open the search bar for the explorer or the viewer; backward (Emacs C-r)
 * searches upward as you type */
void show_search(AppData* app, gboolean from_viewer, gboolean backward);
void hide_search(AppData* app);
/* Next (+1) / previous (-1) match of the open or last search */
void search_again(AppData* app, gint direction);
void do_search(AppData* app, gint direction);
gint utf8_ci_find_from(const gchar* hay, const gchar* needle, gint start);
void do_viewer_search(AppData* app, gint direction);

void on_search_changed(GtkSearchEntry* entry, gpointer data);
void on_search_activate(GtkSearchEntry* entry, gpointer data);
gboolean on_search_key_press(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer data);

#endif /* SEARCH_H */
