#ifndef REMOTE_H
#define REMOTE_H

#include "app_types.h"
#include "remote_store.h"

/* Explorer panel integration */
void remote_append_rows(ExplorerRow* root, AppData* app);
void remote_refresh(AppData* app);
/* Re-read ~/.ssh/config (F5 / refresh button) and redraw */
void remote_rescan(AppData* app);
gboolean remote_handle_key(AppData* app, guint keyval, GdkModifierType state);
void remote_attach_context_menu(AppData* app);

/* Actions */
void remote_add_dialog(AppData* app);
void remote_edit_dialog(AppData* app);
void remote_delete_selected(AppData* app);
void remote_connect_selected(AppData* app);
void remote_disconnect_selected(AppData* app);
void remote_toggle_connect_selected(AppData* app);
void remote_open_terminal_selected(AppData* app);
void remote_activate_selected(AppData* app);

/* Remote connection helpers */
RemoteMachine* remote_get_selected(AppData* app);
gboolean remote_connect_machine(AppData* app, RemoteMachine* rm);
gboolean remote_disconnect_machine(AppData* app, RemoteMachine* rm);
void remote_open_terminal(AppData* app, RemoteMachine* rm);

#endif /* REMOTE_H */
