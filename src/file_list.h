#ifndef FILE_LIST_H
#define FILE_LIST_H

#include "app_types.h"

const gchar* file_extension(const gchar* name);
gboolean is_markdown_file(const gchar* name);
gboolean is_html_file(const gchar* name);
gboolean is_renderable_file(const gchar* name);
const gchar* file_icon(const gchar* name, gboolean is_dir);

gint list_count(AppData* app);
gint count_visible_rows(AppData* app);

GtkTreePath* get_first_visible_path(GtkTreeView* view);
GtkTreePath* get_last_visible_path(GtkTreeView* view);
GtkTreePath* get_next_visible_path(GtkTreeView* view, GtkTreePath* path);
GtkTreePath* get_prev_visible_path(GtkTreeView* view, GtkTreePath* path);

void select_path(AppData* app, GtkTreePath* path);
void select_first_row(AppData* app);
void select_last_row(AppData* app);
void select_next_row(AppData* app);
void select_prev_row(AppData* app);
void page_down_rows(AppData* app);
void page_up_rows(AppData* app);

gboolean select_by_name(AppData* app, const gchar* name);
gboolean select_by_path(AppData* app, const gchar* full_path);

void populate_store(GtkTreeStore* store, AppData* app, const gchar* dirname,
                    gboolean show_hidden, gint sort_mode);
void reexpand_paths_recursive(AppData* app, GtkTreeIter* parent);

gboolean on_tree_test_expand_row(GtkTreeView* tree_view, GtkTreeIter* iter,
                                 GtkTreePath* path, gpointer user_data);
void on_tree_row_expanded(GtkTreeView* tree_view, GtkTreeIter* iter,
                          GtkTreePath* path, gpointer user_data);
void on_tree_row_collapsed(GtkTreeView* tree_view, GtkTreeIter* iter,
                           GtkTreePath* path, gpointer user_data);
void update_path_label(AppData* app);
void reload_directory(AppData* app, const gchar* new_dir);
void refresh_listing(AppData* app);
void toggle_hidden_files(AppData* app);
void cycle_sort_mode(AppData* app);

void activate_selected_entry(AppData* app);
void on_row_activated(GtkTreeView* view, GtkTreePath* path, GtkTreeViewColumn* column, gpointer data);
gboolean on_tree_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data);

#endif /* FILE_LIST_H */
