#ifndef SEARCH_H
#define SEARCH_H

#include "app_types.h"

void show_search(AppData* app, gboolean from_viewer);
void hide_search(AppData* app);
void do_search(AppData* app, gint direction);
gint utf8_ci_find_from(const gchar* hay, const gchar* needle, gint start);
void do_viewer_search(AppData* app, gint direction);

void on_search_changed(GtkSearchEntry* entry, gpointer data);
void on_search_activate(GtkWidget* entry, gpointer data);
gboolean on_search_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data);

#endif /* SEARCH_H */
