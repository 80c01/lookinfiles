#ifndef FILE_LIST_H
#define FILE_LIST_H

#include "app_types.h"

void refresh_git_status_in_place(AppData* app);

void populate_store(GtkTreeStore* store, AppData* app, const gchar* dirname,
                    gboolean show_hidden, gint sort_mode);
void reexpand_paths_recursive(AppData* app, GtkTreeIter* parent);

gboolean on_tree_test_expand_row(GtkTreeView* tree_view, GtkTreeIter* iter,
                                 GtkTreePath* path, gpointer user_data);
void on_tree_row_expanded(GtkTreeView* tree_view, GtkTreeIter* iter,
                          GtkTreePath* path, gpointer user_data);
void on_tree_row_collapsed(GtkTreeView* tree_view, GtkTreeIter* iter,
                           GtkTreePath* path, gpointer user_data);
void reload_directory(AppData* app, const gchar* new_dir);
void navigate_to_path(AppData* app, const gchar* path);
void refresh_listing(AppData* app);
void refresh_listing_with_path(AppData* app, const gchar* select_path);
void toggle_hidden_files(AppData* app);
void toggle_bookmarks_view(AppData* app);
void toggle_notes_view(AppData* app);
void toggle_remote_view(AppData* app);
void cycle_sort_mode(AppData* app);
void toggle_collapse_expand_files(AppData* app);

/* Files view actions (keys a / Shift+A, F2 / r / e, d / Delete and the toolbar) */
void create_new_file_in_tree(AppData* app);
void create_new_folder_in_tree(AppData* app);
void rename_selected_in_tree(AppData* app);
void trash_selected_in_tree(AppData* app);

void activate_selected_entry(AppData* app);
void on_row_activated(GtkTreeView* view, GtkTreePath* path, GtkTreeViewColumn* column, gpointer data);
gboolean on_tree_key_press(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer data);

#endif /* FILE_LIST_H */
