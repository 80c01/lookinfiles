#ifndef VFS_CACHE_H
#define VFS_CACHE_H

/* Local copies of remote files (vfs.h) for what can only open a file by
 * its path (image decoders, poppler, archive and database readers): the
 * file is read over SFTP once into the cache folder, under its own name,
 * and used again while its size and date stay the same. The oldest copies
 * go when the folder grows past a limit. For worker threads (the calls
 * wait for the network). GIO only, no widgets; unit-tested. */
#include <gio/gio.h>

/* The local copy of the remote file at path (at most max_bytes; larger:
 * NULL with G_IO_ERROR_NO_SPACE), made or reused. Caller frees. NULL with
 * error as vfs_read() otherwise. */
gchar* vfs_cache_copy(const gchar* path, guint64 max_bytes, GCancellable* cancel, GError** error);

/* Remove the least recently used copies until the copies take at most
 * max_bytes. Returns how many were removed. */
guint vfs_cache_trim(guint64 max_bytes);

/* The folder of the copies (app_cache_dir()/remote-files). Caller frees. */
gchar* vfs_cache_dir(void);

#endif /* VFS_CACHE_H */
