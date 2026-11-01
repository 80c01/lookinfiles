#ifndef EDITOR_H
#define EDITOR_H

#include "app_types.h"
#include "editor_modes.h"

#define EDIT_MAX_BYTES (2 * 1024 * 1024) /* editing size cap */

typedef enum {
    EDITOR_MOVE_RIGHT,
    EDITOR_MOVE_LEFT,
    EDITOR_MOVE_UP,
    EDITOR_MOVE_DOWN
} EditorMoveDirection;

enum {
    TAB_ACTION_MOVE_RIGHT,
    TAB_ACTION_MOVE_LEFT,
    TAB_ACTION_MOVE_UP,
    TAB_ACTION_MOVE_DOWN,
    TAB_ACTION_CLOSE,
    TAB_ACTION_MODE_NOTEPAD,
    TAB_ACTION_MODE_EMACS,
    TAB_ACTION_MODE_VIM,
    TAB_ACTION_LINE_NUMBERS,
    TAB_ACTION_WRAP,
    TAB_ACTION_SPLIT_RIGHT,
    TAB_ACTION_SPLIT_DOWN
};

EditorTab* editor_current_tab(AppData* app);
/* Show or hide the line numbers of tab (NULL: the current editor tab) */
void editor_toggle_line_numbers(AppData* app, EditorTab* tab);
GPtrArray* editor_get_all_notebooks(AppData* app);
void editor_set_tab_mode(AppData* app, EditorTab* tab, EditorMode mode);
void on_editor_modified_changed(GtkTextBuffer* buffer, gpointer data);
void editor_apply_language(GtkTextBuffer* buffer, const gchar* name);
void editor_open_file(AppData* app, const gchar* name, const gchar* full);
/* Open full in the dedicated editor panel (moving its tab there when it is
 * open in the preview notebook). keep_focus: widget given the focus back
 * (Emacs C-o), NULL to focus the editor (Vim t). NULL when not editable. */
EditorTab* editor_open_dedicated(AppData* app, const gchar* name, const gchar* full,
                                 GtkWidget* keep_focus);
/* The open tab of path (paths compared canonicalized), or NULL */
EditorTab* editor_find_tab(AppData* app, const gchar* path);
/* Open path in an editor tab (or switch to its tab), put the cursor at line
 * (0-based) and byte index (both clamped), scroll it into view when it is
 * not visible and focus the view. NULL when the file cannot be edited. */
EditorTab* editor_open_at(AppData* app, const gchar* path, gint line, gint index);
void on_editor_switch_page_changed(GtkNotebook* notebook, GtkWidget* page, guint page_num, gpointer data);
gboolean editor_confirm_discard(AppData* app, EditorTab* tab);
void editor_close_tab(AppData* app, EditorTab* tab);
void editor_close_page(AppData* app, gint page);
void editor_close_current(AppData* app);
void editor_save_all_buffers(AppData* app);
gboolean editor_save_current(AppData* app);
/* Take the workspace's tabs out of the window. Returns the content of the
 * dedicated editor panel (notebooks and splits, with their tabs; a reference
 * the caller owns, NULL if empty) to give back to editor_attach_tabs(). */
GtkWidget* editor_detach_tabs(AppData* app);
/* Show a workspace's tabs; panel (from editor_detach_tabs(), may be NULL) is consumed */
void editor_attach_tabs(AppData* app, GList* editors, GtkWidget* panel);
void editor_free_tabs(AppData* app, GList* editors, gboolean detach_first);
gboolean editor_confirm_discard_workspace(AppData* app, GList* editors);
void editor_toggle_dedicated(AppData* app);
void editor_set_dedicated(AppData* app, gboolean dedicated);
void editor_move_tab_dedicated(AppData* app, EditorTab* tab);
void editor_move_tab_direction(AppData* app, EditorTab* tab, EditorMoveDirection dir);
/* Show tab's file in a second view on the dir side of its notebook (moved to
 * the dedicated panel first when in the preview notebook). The views share
 * the buffer: text, cursor, undo. tab NULL: the current editor tab. */
void editor_split_tab(AppData* app, EditorTab* tab, EditorMoveDirection dir);
/* Close the other views of tab's file (Emacs C-x 1, Vim :only), focus tab.
 * Returns how many were closed. */
gint editor_close_other_views(AppData* app, EditorTab* tab);
/* Accept editor tabs dragged with the mouse onto nb (an editor notebook or
 * the preview notebook): on its tab bar, its page, or an edge to split it */
void editor_notebook_enable_tab_drop(AppData* app, GtkWidget* nb);
/* Take child, a page that is not an editor tab (a terminal whose shell
 * exited), out of its editor or preview notebook; an editor notebook left
 * empty goes, the editor panel hides without tabs */
void editor_remove_page(AppData* app, GtkWidget* child);
void editor_tab_show_context_menu(AppData* app, EditorTab* tab, GtkWidget* target_widget);
void on_notebook_page_reordered(GtkNotebook* notebook, GtkWidget* child,
                                guint page_num, gpointer data);

#endif /* EDITOR_H */
