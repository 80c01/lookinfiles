#ifndef REMOTE_STORE_H
#define REMOTE_STORE_H

/* Remote machine data model (no widgets): list in app->remotes,
 * persistence in <config>/remotes.xml, discovery from ~/.ssh/config. */
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

/* Load remotes.xml then merge ~/.ssh/config hosts into app->remotes */
void remote_init(AppData* app);
void remote_free(AppData* app);
/* Write manually managed machines to remotes.xml */
void remote_save(AppData* app);
void remote_machine_free(RemoteMachine* rm);

RemoteMachine* remote_find_by_id(AppData* app, const gchar* id);
/* Machine of the current workspace connection (app->current_remote_id), NULL if local */
RemoteMachine* remote_get_active_machine(AppData* app);

/* Local cache directory used as sshfs mount point for a machine. Caller frees. */
gchar* remote_get_cache_dir(const gchar* machine_id);

#endif /* REMOTE_STORE_H */
