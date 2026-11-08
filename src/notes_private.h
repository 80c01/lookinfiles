#ifndef NOTES_PRIVATE_H
#define NOTES_PRIVATE_H

/* Internal header of the Notes view: notes.c (rows, selection, keys, menu),
 * notes_dialogs.c and notes_actions.c (the public API is notes.h) */
#include "notes.h"
#include "notes_store.h"

/* What the selected row of the Notes view is */
typedef enum {
    SEL_NONE = 0,
    SEL_NOTEBOOK,
    SEL_NOTE,
    SEL_TRASH,
    SEL_TRASH_NOTE
} NotesSelectionType;

/* notes.c */
/* app->notes_mgr, or NULL */
NotesManager* notes_mgr_of(AppData* app);
/* The selected row of the Notes view and its items (each out may be NULL) */
NotesSelectionType notes_selected_info(AppData* app, NotebookItem** out_nb, NoteItem** out_note,
                                       TrashNoteItem** out_trash);
/* Editor tabs of every workspace on old_path follow the note to new_path */
void notes_update_open_editors(AppData* app, const gchar* old_path, const gchar* new_path,
                               const gchar* new_name);
/* The trash directory of the notes, created if missing. Caller frees. */
gchar* notes_get_trash_dir(void);
/* Select the row of a note or notebook by path, showing it if needed */
void notes_select_path(AppData* app, const gchar* path);

/* notes_dialogs.c */
/* The note dialog (title, type); TRUE on OK with *out_title (caller frees)
 * and *out_type. notebooks: when non-NULL, a "Notebook" drop-down lists them, starts on
 * *io_nb and returns the choice there (moving the note); focus_notebook
 * gives it the focus instead of the title */
gboolean notes_prompt_note(GtkWindow* parent_win, const gchar* title, const gchar* ok_btn_text,
                           const gchar* init_title, NoteType init_type, gboolean existing,
                           GList* notebooks, NotebookItem** io_nb, gboolean focus_notebook,
                           gchar** out_title, NoteType* out_type);
/* The notebook dialog (name, icon); TRUE on OK with *out_name and *out_icon (caller frees) */
gboolean notes_prompt_notebook(GtkWindow* parent_win, const gchar* title, const gchar* ok_btn_text,
                               const gchar* init_name, const gchar* init_icon,
                               gchar** out_name, gchar** out_icon);
/* Ask before an action; TRUE on OK (destructive: a red OK button) */
gboolean notes_confirm(AppData* app, const gchar* title, const gchar* message,
                       const gchar* ok_label, gboolean destructive);
/* Information message with a single Close button */
void notes_notice(AppData* app, const gchar* title, const gchar* message);

#endif /* NOTES_PRIVATE_H */
