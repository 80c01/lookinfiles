#ifndef VIEWER_H
#define VIEWER_H

#include "app_types.h"

void set_view_monospace(GtkWidget* view, gboolean monospace);
gboolean is_code_file(const gchar* name);
void apply_viewer_language(AppData* app, const gchar* name);
void apply_viewer_background(GtkWidget* view);
#ifdef HAVE_GSV
void apply_vscode_scheme(GtkTextBuffer* buffer, GtkWidget* view);
#endif

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
