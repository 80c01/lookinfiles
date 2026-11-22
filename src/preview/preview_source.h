#ifndef PREVIEW_SOURCE_H
#define PREVIEW_SOURCE_H

/* The bytes of a file for a preview, read on a worker thread: from the
 * disk, or over SFTP for the file of a remote machine (vfs.h). No AppData,
 * no widgets. */
#include "preview/preview_cache.h"

#include <gio/gio.h>

/* The stamp of path (as file_stamp_read(); a remote file's has no inode) */
gboolean preview_source_stamp(const gchar* path, FileStamp* stamp);

typedef struct PreviewSource PreviewSource;

/* path opened for reading, NULL when it cannot be read. A remote file is
 * read at once: its first PREVIEW_BYTES when head, else all of it; a local
 * one as it is read. */
PreviewSource* preview_source_open(const gchar* path, gboolean head, GCancellable* cancel);
/* Up to n bytes into buf; 0 at the end */
gsize preview_source_read(PreviewSource* src, void* buf, gsize n);
void preview_source_close(PreviewSource* src);

/* For what opens a file by its path (images, PDF, archives, documents; a
 * player: player): a local copy of a remote machine's file (vfs_cache.h,
 * up to 64 MB, 512 MB for a player), NULL when path is not remote or cannot
 * be copied (then nothing can be shown). Caller frees. */
gchar* preview_source_local(const gchar* path, gboolean player, GCancellable* cancel);

/* For image_file_set_func() (image_utils.h): the file to decode for a
 * picture of a document; a remote machine's picture is copied (up to
 * 16 MB) off the main thread only, and none on it (never a wait for the
 * network there). Caller frees. */
gchar* preview_source_image_file(const gchar* path);

#endif /* PREVIEW_SOURCE_H */
