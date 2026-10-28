#ifndef NOTES_H
#define NOTES_H

#include "app_types.h"

/* Notes & Notebooks Manager
 * Manages hierarchical notebooks (carnets de notes), notes, and a trash bin (corbeille).
 * Supports reordering (manual order), sorting, moving notes between notebooks,
 * renaming, trashing, and restoring. Persists metadata to
 * ~/.config/explorateur_fichiers/notes/notes.xml and note files in
 * ~/.config/explorateur_fichiers/notes/<notebook_name>/.
 */

void notes_init(AppData* app);
void notes_free(AppData* app);
void notes_save(AppData* app);

/* Explorer panel integration */
void notes_append_to_store(GtkTreeStore* store, AppData* app);
void notes_reexpand(AppData* app);
void notes_refresh(AppData* app);
gboolean notes_handle_row_toggled(AppData* app, GtkTreeIter* iter, gboolean expanded);
gboolean notes_handle_key(AppData* app, guint keyval, GdkModifierType state);
void notes_attach_context_menu(AppData* app);

/* Actions */
void notes_new_note(AppData* app);
void notes_new_notebook(AppData* app);
void notes_edit_selected(AppData* app);
void notes_move_selected(AppData* app);
void notes_delete_selected(AppData* app);
void notes_restore_selected(AppData* app);
void notes_empty_trash(AppData* app);
void notes_move_reorder(AppData* app, gint delta);
void notes_toggle_sort(AppData* app);
void notes_toggle_collapse_expand(AppData* app);
void notes_activate_selected(AppData* app);

/* Helper to get root notes directory */
gchar* notes_get_root_dir(void);

#endif /* NOTES_H */
