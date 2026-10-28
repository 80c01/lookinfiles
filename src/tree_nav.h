#ifndef TREE_NAV_H
#define TREE_NAV_H

/* Explorer tree navigation and selection helpers */
#include "app_types.h"

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
void center_selected_row(AppData* app);
void top_selected_row(AppData* app);
void bottom_selected_row(AppData* app);
gboolean select_by_name(AppData* app, const gchar* name);
gboolean select_by_path(AppData* app, const gchar* full_path);

/* Suspend / resume the preview handler on selection "changed" */
void tree_nav_block_selection_handler(AppData* app);
void tree_nav_unblock_selection_handler(AppData* app);

#endif /* TREE_NAV_H */
