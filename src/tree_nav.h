#ifndef TREE_NAV_H
#define TREE_NAV_H

/* Explorer tree navigation and selection helpers (rows: explorer_row.h) */
#include "app_types.h"

/* Selected row, NULL if none (borrowed) */
ExplorerRow* tree_nav_selected(AppData* app);

/* Select a visible row and scroll it into view; re-selecting the selected
 * row still refreshes the preview */
void select_row(AppData* app, ExplorerRow* row);

/* Expand the folders above row, then select it. Expanding may rebuild rows
 * (bookmarks, notes): FALSE when row was dropped meanwhile (nothing selected). */
gboolean reveal_row(AppData* app, ExplorerRow* row);
/* reveal_row() without running the selection handler: for the row selected
 * again after its list was rebuilt (the caller re-renders the preview in place) */
gboolean reveal_row_quietly(AppData* app, ExplorerRow* row);
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

/* Switch the explorer panel to view (SIDEBAR_VIEW_...); TRUE when it changed.
 * The only writer of app->sidebar_view besides the workspace switch: the
 * caller then lists what the view shows (reload_directory(), refresh...). */
gboolean explorer_set_view(AppData* app, gint view);

/* Suspend / resume the preview handler on "selection-changed" */
void tree_nav_block_selection_handler(AppData* app);
void tree_nav_unblock_selection_handler(AppData* app);

#endif /* TREE_NAV_H */
