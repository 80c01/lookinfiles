#ifndef VFS_H
#define VFS_H

/* Files of remote machines without a mount: the folder of a connected
 * machine is seen at a local path (its "mount" path, remote_mounts_dir(),
 * shown as machine:path by remote_path.h), and what is below that path is
 * read over SFTP (sftp_client.h), one connection per machine through its
 * master connection. The SFTP clients live on a thread of their own, so
 * the calls here are synchronous (at most a minute), for worker threads
 * (dir_scan): on the main thread they would freeze the interface.
 * GIO only, no widgets; unit-tested with the test's SFTP server as ssh. */
#include "core/ssh_session.h"

/* Files below local (a path, no trailing '/') are those of remote_root on
 * target ("~" or a relative path: from the home folder). target is copied.
 * Replaces an earlier mount of local (kept, connection and all, when it is
 * the same). */
void vfs_mount(const gchar* local, const SshTarget* target, const gchar* remote_root);
/* local is no longer remote (its connection closes) */
void vfs_unmount(const gchar* local);
/* path is a mount of vfs_mount() or below one */
gboolean vfs_is_remote(const gchar* path);
/* Close every connection and stop the thread (at exit) */
void vfs_shutdown(void);

/* The SFTP path of rel (a path relative to the mount, "" for the mount
 * itself) in remote_root: "~" + "a/b" -> "a/b", "/srv" + "a" -> "/srv/a",
 * "~" + "" -> ".". Caller frees. */
gchar* vfs_remote_path(const gchar* remote_root, const gchar* rel);

/* An entry of a folder */
typedef struct {
    gchar* name;
    gboolean is_dir;  /* a folder, or a link to one */
    gboolean is_link; /* a symbolic link */
    gint64 mtime;     /* seconds since 1970 */
    gint64 size;      /* bytes (of the target for a link) */
} VfsEntry;

void vfs_entry_free(gpointer entry);

/* The entries of the remote folder at path (vfs_is_remote()), without "."
 * nor "..": VfsEntry, vfs_entry_free. NULL with error: G_IO_ERROR_NOT_FOUND,
 * PERMISSION_DENIED, as the server said; NOT_CONNECTED without a master
 * connection; TIMED_OUT, CANCELLED; CONNECTION_CLOSED. */
GPtrArray* vfs_list(const gchar* path, GCancellable* cancel, GError** error);

#endif /* VFS_H */
