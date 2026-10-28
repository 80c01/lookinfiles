#ifndef VIEWER_H
#define VIEWER_H

#include "app_types.h"

void viewer_scroll_lines(AppData* app, gint lines);
void viewer_scroll_page(AppData* app, gint direction);
void viewer_goto(AppData* app, gboolean top);
gboolean on_viewer_key_press(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer data);

void load_file_content(AppData* app, const gchar* name, const gchar* full);
void render_image_preview(AppData* app, const gchar* name, const gchar* full);
void on_selection_changed(GtkTreeSelection* selection, gpointer user_data);

void monitor_directory(AppData* app);
void sync_directory_monitors(AppData* app);
void stop_directory_monitors(AppData* app);
void monitor_file(AppData* app, const char* path);
void schedule_refresh(AppData* app);

#endif /* VIEWER_H */
