#ifndef VFS_WATCH_H
#define VFS_WATCH_H

/* Changes of remote files (vfs.h), which no file monitor sees: a watch
 * reads the file's stamp (size, date) over SFTP every few seconds on a
 * worker thread and calls its function on the main thread when it changed.
 * The last stamp read of each path is kept, so the editor's checks answer
 * at once (vfs_watch_stamp()). GIO only, no widgets; unit-tested. */
#include <gio/gio.h>

#define VFS_WATCH_SECONDS 3 /* between two readings */

typedef void (*VfsWatchFunc)(gpointer data);
typedef struct VfsWatch VfsWatch;

/* Watch the remote file path (main thread); changed(data) when its stamp
 * changes (created, removed, written) */
VfsWatch* vfs_watch_new(const gchar* path, VfsWatchFunc changed, gpointer data);
/* Stop: changed is not called after this */
void vfs_watch_free(VfsWatch* watch);
/* Read now rather than at the next tick (main thread) */
void vfs_watch_poll(VfsWatch* watch);

/* The stamp of the remote file path: the last one read (by a watch, or by
 * an earlier call), else read now (waits for the network). FALSE when
 * unknown (no SFTP session); else *exists, and *size, *mtime (seconds)
 * when it exists. */
gboolean vfs_watch_stamp(const gchar* path, gboolean* exists, gint64* size, gint64* mtime);

/* Drop the last stamp read of path: the next vfs_watch_stamp() reads it
 * again (after the file was written here, or before recording it) */
void vfs_watch_forget(const gchar* path);

#endif /* VFS_WATCH_H */
