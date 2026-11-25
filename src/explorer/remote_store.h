#ifndef REMOTE_STORE_H
#define REMOTE_STORE_H

/* Remote machine data model (no widgets): machines and their folders (private
 * to remote_store.c, read with remote_machine_list() / remote_folder_list()), persistence in <config>/remotes.xml, discovery from
 * ~/.ssh/config. */
#include "core/app_types.h"
#include "core/ssh_session.h"

/* Remote SSH Machine definition */
typedef struct {
    gchar* id;             /* unique machine id, e.g. "rem_167890..." */
    gchar* name;           /* friendly display name */
    gchar* host;           /* hostname or IP */
    gchar* user;           /* username (optional) */
    gint port;             /* SSH port (default 22) */
    gchar* remote_path;    /* default directory on remote (default: "~" or "/") */
    gchar* identity_file;  /* optional private key path */
    gchar* mount_point;    /* local folder its files are seen at (remote_mounts_dir(), empty) */
    gboolean is_connected; /* whether currently mounted */
    gboolean from_ssh_config; /* whether parsed from ~/.ssh/config */
    gchar* folder_id;      /* RemoteFolder id, NULL at the top level */
    GPtrArray* disk_mounts; /* RemoteDiskMount*: folders of the machine outside remote_path
                             * (its disks) seen at other local folders, NULL for none */
} RemoteMachine;

/* A disk of a connected machine opened outside its remote_path: the
 * remote folder root is seen at the local folder local (vfs_mount()) */
typedef struct {
    gchar* local;
    gchar* root;
} RemoteDiskMount;

/* Folder grouping machines in the remote view (one level, no nesting) */
typedef struct {
    gchar* id;             /* unique folder id, e.g. "rfold_..." */
    gchar* name;
    const gchar* icon;     /* one of NOTEBOOK_ICONS (static) */
    gboolean expanded;     /* expanded in the explorer panel */
} RemoteFolder;

/* Load remotes.xml then merge ~/.ssh/config hosts into the machine list */
void remote_init(AppData* app);
void remote_free(AppData* app);
/* Write manually managed machines to remotes.xml */
void remote_save(AppData* app);
void remote_machine_free(RemoteMachine* rm);

/* Re-read ~/.ssh/config: new hosts added, removed ones dropped (connected ones kept) */
void remote_reload_ssh_config(AppData* app);

RemoteMachine* remote_find_by_id(AppData* app, const gchar* id);

/* Machines (RemoteMachine*) and folders (RemoteFolder*) in display order,
 * owned by the store; NULL when empty */
GList* remote_machine_list(AppData* app);
GList* remote_folder_list(AppData* app);
/* Append a new machine (the store takes it) */
void remote_machine_add(AppData* app, RemoteMachine* rm);
/* Remove a machine from the list and free it */
void remote_machine_delete(AppData* app, RemoteMachine* rm);

RemoteFolder* remote_folder_find(AppData* app, const gchar* id);
/* Folder of a machine, NULL at the top level (or when its folder is gone) */
RemoteFolder* remote_machine_folder(AppData* app, const RemoteMachine* rm);
/* New folder appended to the folder list (icon NULL: the default) */
RemoteFolder* remote_folder_add(AppData* app, const gchar* name, const gchar* icon);
/* Remove and free a folder; its machines move to the top level */
void remote_folder_remove(AppData* app, RemoteFolder* folder);
/* Number of machines in a folder */
guint remote_folder_count(AppData* app, const RemoteFolder* folder);
/* Put a machine in a folder (NULL: top level) */
void remote_machine_set_folder(RemoteMachine* rm, const RemoteFolder* folder);
/* Where ssh connects for rm (strings owned by rm) */
SshTarget remote_machine_target(const RemoteMachine* rm);
/* The local folder where the remote folder root of rm is seen: its own
 * mount for a disk (made and remembered here, mounted by
 * remote_session_files_open()), or below rm->mount_point when root is
 * inside remote_path. NULL when rm has no mount point. Caller frees. */
gchar* remote_machine_disk_local(RemoteMachine* rm, const gchar* root);
/* Forget the disk mounts of rm (at disconnect; their folders removed) */
void remote_machine_clear_disk_mounts(RemoteMachine* rm);
/* Mount i of rm: 0 its mount_point and remote_path, then its disk mounts.
 * FALSE past the last one (or for a machine without a mount point). */
gboolean remote_machine_mount_nth(const RemoteMachine* rm, guint i, const gchar** local, const gchar** root);

/* Machine of the current workspace connection (app->current_remote_id), NULL if local */
RemoteMachine* remote_get_active_machine(AppData* app);

#endif /* REMOTE_STORE_H */
