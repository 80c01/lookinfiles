#ifndef BOOKMARKS_H
#define BOOKMARKS_H

#include "app_types.h"

/* Bookmark manager: named links to directories, files or URLs, organized
 * in user-defined virtual folders. Persisted as XML in
 * ~/.config/lookin/bookmarks.xml (the legacy flat
 * "bookmarks" file is migrated automatically on first start). */

void bookmarks_init(AppData* app);
void bookmarks_free(AppData* app);
void bookmarks_save(AppData* app);

/* Explorer panel integration */
void bookmarks_append_rows(ExplorerRow* root, AppData* app);
void bookmarks_reexpand(AppData* app);
void bookmarks_refresh(AppData* app);
gboolean bookmarks_handle_row_toggled(AppData* app, ExplorerRow* row, gboolean expanded);
gboolean bookmarks_handle_key(AppData* app, guint keyval, GdkModifierType state);
void bookmarks_attach_context_menu(AppData* app);

/* Actions */
void bookmarks_add_from_context(AppData* app);  /* Ctrl+D from any view */
void bookmarks_add(AppData* app);               /* add bookmark in selected folder */
void bookmarks_new_folder(AppData* app);
void bookmarks_edit_selected(AppData* app);
void bookmarks_delete_selected(AppData* app);
void bookmarks_move_selected(AppData* app, gint delta);
void bookmarks_activate_selected(AppData* app);
void bookmarks_toggle_sort(AppData* app);
void bookmarks_toggle_collapse_expand(AppData* app);

#endif /* BOOKMARKS_H */
