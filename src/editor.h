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
    TAB_ACTION_DEDICATED,
    TAB_ACTION_MOVE_RIGHT,
    TAB_ACTION_MOVE_LEFT,
    TAB_ACTION_MOVE_UP,
    TAB_ACTION_MOVE_DOWN,
    TAB_ACTION_CLOSE,
    TAB_ACTION_MODE_NOTEPAD,
    TAB_ACTION_MODE_EMACS,
    TAB_ACTION_MODE_VIM
};

EditorTab* editor_current_tab(AppData* app);
GPtrArray* editor_get_all_notebooks(AppData* app);
void editor_tab_label_update(EditorTab* tab);
void editor_set_tab_mode(AppData* app, EditorTab* tab, EditorMode mode);
void on_editor_modified_changed(GtkTextBuffer* buffer, gpointer data);
void editor_apply_language(GtkTextBuffer* buffer, const gchar* name);
void editor_open_file(AppData* app, const gchar* name, const gchar* full);
void on_editor_switch_page_changed(GtkNotebook* notebook, GtkWidget* page, guint page_num, gpointer data);
gboolean editor_confirm_discard(AppData* app, EditorTab* tab);
void editor_close_tab(AppData* app, EditorTab* tab);
void editor_close_page(AppData* app, gint page);
void editor_close_current(AppData* app);
void editor_save_all_buffers(AppData* app);
gboolean editor_save_current(AppData* app);
void editor_detach_tabs(AppData* app);
void editor_attach_tabs(AppData* app, GList* editors);
void editor_free_tabs(AppData* app, GList* editors, gboolean detach_first);
gboolean editor_confirm_discard_workspace(AppData* app, GList* editors);
void editor_toggle_dedicated(AppData* app);
void editor_set_dedicated(AppData* app, gboolean dedicated);
void editor_move_tab_dedicated(AppData* app, EditorTab* tab);
void editor_move_tab_direction(AppData* app, EditorTab* tab, EditorMoveDirection dir);
void editor_tab_show_context_menu(AppData* app, EditorTab* tab, GtkWidget* target_widget);
void on_notebook_page_reordered(GtkNotebook* notebook, GtkWidget* child,
                                guint page_num, gpointer data);

#endif /* EDITOR_H */
