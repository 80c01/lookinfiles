#ifndef EDITOR_MODES_H
#define EDITOR_MODES_H

#include "app_types.h"

/* Case conversion modes */
enum {
    CASE_LOWER = 0,
    CASE_UPPER = 1,
    CASE_TOGGLE = 2,
    CASE_CAPITALIZE = 3
};

/* Public API for editor modes */
void editor_modes_init_tab(AppData* app, EditorTab* tab);
void editor_modes_free_tab(EditorTab* tab);
void editor_modes_apply_mode(AppData* app, EditorTab* tab, EditorMode mode);
gboolean editor_modes_handle_key(AppData* app, EditorTab* tab, guint keyval,
                                 guint keycode, GdkModifierType state);
void editor_modes_update_status(EditorTab* tab);
/* TRUE when the focused editor tab needs keyval although the window binds it
 * (Org-mode prefix keys, Alt+Left/Right on Org structure) */
gboolean editor_modes_claims_key(AppData* app, guint keyval, GdkModifierType state);
gboolean editor_save_tab_as(AppData* app, EditorTab* tab, const gchar* new_path);

/* Refresh the tab title: file icon, name, and a bullet when unsaved */
void editor_tab_label_update(EditorTab* tab);

/* Common editing, formatting, and search operations */
void editor_mode_align_text(EditorTab* tab, GtkJustification just, gint target_width);
void editor_mode_format_paragraph(EditorTab* tab, gint target_width);
void editor_mode_change_case(EditorTab* tab, gint mode);
void editor_mode_indent_selection(EditorTab* tab, gboolean unindent);
gboolean editor_mode_find_next(EditorTab* tab, const gchar* query,
                               gboolean forward, gboolean wrap);
gint editor_mode_replace(EditorTab* tab, const gchar* query,
                         const gchar* replacement, gboolean replace_all);
void editor_mode_goto_line(EditorTab* tab, gint line);

/* Notepad specific triggers */
void notepad_show_find(EditorTab* tab, gboolean is_replace);
void notepad_hide_find(EditorTab* tab);
void notepad_show_save_as(EditorTab* tab);
void notepad_hide_save_as(EditorTab* tab);

/* Emacs specific triggers */
void emacs_echo(EditorTab* tab, const gchar* msg);

/* Vim specific triggers */
void vim_echo(EditorTab* tab, const gchar* msg);

#endif /* EDITOR_MODES_H */
