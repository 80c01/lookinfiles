#ifndef EDITOR_H
#define EDITOR_H

#include "app_types.h"

#define EDIT_MAX_BYTES (2 * 1024 * 1024) /* editing size cap */

EditorTab* editor_current_tab(AppData* app);
void editor_tab_label_update(EditorTab* tab);
void on_editor_modified_changed(GtkTextBuffer* buffer, gpointer data);
void editor_apply_language(GtkTextBuffer* buffer, const gchar* name);
void editor_open_file(AppData* app, const gchar* name, const gchar* full);
void on_editor_switch_page_changed(GtkNotebook* notebook, GtkWidget* page, guint page_num, gpointer data);
gboolean editor_confirm_discard(AppData* app, EditorTab* tab);
void editor_close_page(AppData* app, gint page);
void editor_close_current(AppData* app);
gboolean editor_save_current(AppData* app);
void editor_detach_tabs(AppData* app);
void editor_attach_tabs(AppData* app, GList* editors);
void editor_free_tabs(AppData* app, GList* editors, gboolean detach_first);
gboolean editor_confirm_discard_workspace(AppData* app, GList* editors);
void editor_toggle_dedicated(AppData* app);
void editor_set_dedicated(AppData* app, gboolean dedicated);

#endif /* EDITOR_H */
