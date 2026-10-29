#ifndef FILE_LIST_H
#define FILE_LIST_H

#include "app_types.h"

void refresh_git_status_in_place(AppData* app);

/* Fill the tree with a directory listing, synchronously (start-up) */
void populate_tree(AppData* app, const gchar* dirname, gboolean show_hidden, gint sort_mode);
void reexpand_paths_recursive(AppData* app, ExplorerRow* parent);

/* explorer_tree "row-toggled": lazy loading of directories, folder icons */
void on_tree_row_toggled(ExplorerTree* tree, ExplorerRow* row, gboolean expanded, gpointer user_data);
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
/* explorer_tree "row-activated" (double click) */
void on_row_activated(ExplorerTree* tree, ExplorerRow* row, gpointer data);
gboolean on_tree_key_press(GtkEventControllerKey* controller, guint keyval, guint keycode, GdkModifierType state, gpointer data);

#endif /* FILE_LIST_H */
