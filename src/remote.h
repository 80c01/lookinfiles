#ifndef REMOTE_H
#define REMOTE_H

#include "app_types.h"

/* Remote SSH Machine definition */
typedef struct {
    gchar* id;             /* unique machine id, e.g. "rem_167890..." */
    gchar* name;           /* friendly display name */
    gchar* host;           /* hostname or IP */
    gchar* user;           /* username (optional) */
    gint port;             /* SSH port (default 22) */
    gchar* remote_path;    /* default directory on remote (default: "~" or "/") */
    gchar* identity_file;  /* optional private key path */
    gchar* mount_point;    /* local path where sshfs mounted it */
    gboolean is_connected; /* whether currently mounted */
    gboolean from_ssh_config; /* whether parsed from ~/.ssh/config */
} RemoteMachine;

void remote_init(AppData* app);
void remote_free(AppData* app);
void remote_save(AppData* app);

/* Explorer panel integration */
void remote_append_to_store(GtkTreeStore* store, AppData* app);
void remote_refresh(AppData* app);
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
RemoteMachine* remote_find_by_id(AppData* app, const gchar* id);
RemoteMachine* remote_get_active_machine(AppData* app);
RemoteMachine* remote_get_selected(AppData* app);
gboolean remote_connect_machine(AppData* app, RemoteMachine* rm);
gboolean remote_disconnect_machine(AppData* app, RemoteMachine* rm);
void remote_open_terminal(AppData* app, RemoteMachine* rm);

#endif /* REMOTE_H */
