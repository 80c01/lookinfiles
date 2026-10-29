#ifndef VIEWER_H
#define VIEWER_H

#include "app_types.h"

void viewer_scroll_lines(AppData* app, gint lines);
void viewer_scroll_page(AppData* app, gint direction);
void viewer_goto(AppData* app, gboolean top);
gboolean on_viewer_key_press(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer data);

void load_file_content(AppData* app, const gchar* name, const gchar* full);
void viewer_reload_preview(AppData* app);
/* explorer_tree "selection-changed": preview the selected row */
void on_selection_changed(ExplorerTree* tree, gpointer user_data);

#endif /* VIEWER_H */
