#ifndef NOTES_H
#define NOTES_H

#include "app_types.h"

/* Notes & Notebooks Manager
 * Manages hierarchical notebooks (carnets de notes), notes, and a trash bin (corbeille).
 * Supports reordering (manual order), sorting, moving notes between notebooks,
 * renaming, trashing, and restoring. Persists metadata to
 * ~/.config/lookin/notes/notes.xml and note files in
 * ~/.config/lookin/notes/<notebook_name>/.
 */

void notes_init(AppData* app);
void notes_free(AppData* app);
void notes_save(AppData* app);

/* Explorer panel integration */
void notes_append_rows(ExplorerRow* root, AppData* app);
void notes_reexpand(AppData* app);
void notes_refresh(AppData* app);
gboolean notes_handle_row_toggled(AppData* app, ExplorerRow* row, gboolean expanded);
gboolean notes_handle_key(AppData* app, guint keyval, GdkModifierType state);
void notes_attach_context_menu(AppData* app);

/* Actions */
void notes_new_note(AppData* app);
void notes_new_notebook(AppData* app);
void notes_edit_selected(AppData* app);
void notes_convert_selected_type(AppData* app, NoteType target_type);
void notes_change_notebook_icon(AppData* app);
void notes_move_selected(AppData* app);
void notes_delete_selected(AppData* app);
void notes_restore_selected(AppData* app);
void notes_empty_trash(AppData* app);
void notes_move_reorder(AppData* app, gint delta);
void notes_toggle_sort(AppData* app);
void notes_toggle_collapse_expand(AppData* app);
void notes_activate_selected(AppData* app);

/* Note named on the command line: "Title" (any notebook) or "Notebook/Title";
 * a .org / .md / .txt / .canvas extension matches the file name and sets the
 * type of a new note. Created (with its notebook) when missing. Returns its path (free
 * it), or NULL if the name is invalid. */
gchar* notes_find_or_create(AppData* app, const gchar* spec);

/* Helper to get root notes directory */
gchar* notes_get_root_dir(void);

#endif /* NOTES_H */
