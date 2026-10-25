#ifndef FILE_LIST_H
#define FILE_LIST_H

#include "app_types.h"

const gchar* file_extension(const gchar* name);
gboolean is_markdown_file(const gchar* name);
const gchar* file_icon(const gchar* name, gboolean is_dir);

gint list_count(AppData* app);
gint current_index(AppData* app);
void select_index(AppData* app, gint idx);
gboolean select_by_name(AppData* app, const gchar* name);
gint count_visible_rows(AppData* app);

void populate_store(GtkListStore* store, const gchar* dirname, gboolean show_hidden, gint sort_mode);
void update_path_label(AppData* app);
void reload_directory(AppData* app, const gchar* new_dir);
void refresh_listing(AppData* app);
void toggle_hidden_files(AppData* app);
void cycle_sort_mode(AppData* app);

void activate_selected_entry(AppData* app);
void on_row_activated(GtkTreeView* view, GtkTreePath* path, GtkTreeViewColumn* column, gpointer data);
gboolean on_tree_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data);

#endif /* FILE_LIST_H */
