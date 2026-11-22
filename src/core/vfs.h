#ifndef VFS_H
#define VFS_H

/* Files of remote machines without a mount: the folder of a connected
 * machine is seen at a local path (its "mount" path, remote_mounts_dir(),
 * shown as machine:path by remote_path.h), and what is below that path is
 * read over SFTP (sftp_client.h), one connection per machine through its
 * master connection. The SFTP clients live on a thread of their own, so
 * the calls here are synchronous (at most a minute): reads are for worker
 * threads (dir_scan, previews); the writes asked by the user (save, new
 * folder, rename, delete) run on the main thread, which waits for them as
 * it waited for the mount, with a limit now.
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

/* What path is (links followed; e->name NULL): FALSE with error as
 * vfs_list() */
gboolean vfs_stat(const gchar* path, VfsEntry* e, GCancellable* cancel, GError** error);

/* Run command (a line for the remote shell) on the machine of path, in
 * the remote folder path is (worker threads only), input (NULL: none) on
 * its standard input: ssh_exec_sync(), its exit status and output. FALSE
 * with error when it could not run, or when path is not remote. */
gboolean vfs_exec(const gchar* path, const gchar* command, GBytes* input, GCancellable* cancel, gint* status,
                  GBytes** out, GError** error);
/* The local folder of the machine path is on (the mount of vfs_mount()),
 * NULL when path is not remote. Caller frees. */
gchar* vfs_mount_of(const gchar* path);
/* The absolute path on the machine of the folder path (links resolved, as
 * the programs there see it), NULL with error (worker threads, or a short
 * wait). Caller frees. */
gchar* vfs_remote_absolute(const gchar* path, GCancellable* cancel, GError** error);
/* The ssh command line running command in the remote folder of path,
 * through the master (ssh_session_argv()), for a process kept running
 * here (g_ptr_array_unref; not NULL-terminated) */
GPtrArray* vfs_command_argv(const gchar* path, const gchar* command, GError** error);
/* The same started, its output read as it comes (ssh_spawn()) */
GSubprocess* vfs_spawn(const gchar* path, const gchar* command, GError** error);

/* What the last vfs_list() of its folder or vfs_stat() said of path
 * (e->name NULL), at once, without the network; FALSE when nothing did */
gboolean vfs_peek(const gchar* path, VfsEntry* e);

/* The first max bytes of the remote file at path (G_MAXUINT64: all of it);
 * *size (may be NULL) gets the file's size. NULL with error as vfs_list(). */
GBytes* vfs_read(const gchar* path, guint64 max, guint64* size, GCancellable* cancel, GError** error);

/* The whole content of path, NUL-terminated, as g_file_get_contents():
 * over SFTP for a remote machine's file (at most max bytes: larger fails),
 * else from the disk */
gboolean vfs_file_get_contents(const gchar* path, guint64 max, gchar** contents, gsize* len, GError** error);

/* Write contents to the remote file at path, safely (sftp_client_write_async():
 * a temporary file renamed over it, the old mode kept; a new file gets
 * mode); a link stays a link. FALSE with error as vfs_list(). */
gboolean vfs_write(const gchar* path, GBytes* contents, guint32 mode, GError** error);
/* Make the remote folder path */
gboolean vfs_mkdir(const gchar* path, guint32 mode, GError** error);
/* Make path and the folders above it that are missing (below the mount) */
gboolean vfs_mkdir_with_parents(const gchar* path, guint32 mode, GError** error);
/* Remove the remote file (or link) at path */
gboolean vfs_remove(const gchar* path, GError** error);
/* Remove path and, for a folder, everything in it (links are removed, not
 * followed). No trash on a remote machine: this cannot be undone. */
gboolean vfs_delete_tree(const gchar* path, GError** error);
/* Rename from to to (both on the same machine); never over an existing
 * file unless replace */
gboolean vfs_rename(const gchar* from, const gchar* to, gboolean replace, GError** error);

/* g_file_test() for G_FILE_TEST_EXISTS, IS_DIR and IS_REGULAR, over SFTP
 * for a remote machine's path (links followed; FALSE when the SFTP session
 * cannot tell); waits for the answer on the calling thread */
gboolean vfs_file_test(const gchar* path, GFileTest test);

/* The same from offset on (at most max bytes) */
GBytes* vfs_read_range(const gchar* path, guint64 offset, guint64 max, guint64* size, GCancellable* cancel,
                       GError** error);

/* Whether error says the SFTP session could not serve the call (no master
 * connection, lost, no answer) rather than the server's answer about the
 * file */
gboolean vfs_error_is_connection(const GError* error);

#endif /* VFS_H */
