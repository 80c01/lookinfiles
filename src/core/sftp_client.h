#ifndef SFTP_CLIENT_H
#define SFTP_CLIENT_H

/* An SFTP v3 client (sftp_proto.h) over a pair of streams, usually the pipes
 * of `ssh -s <machine> sftp` (sftp_client_spawn(), through the master
 * connection of ssh_session.h). Everything is asynchronous, in the main
 * loop of the thread that made the client: requests are sent at once one
 * after the other and the replies matched by id, so reads and writes of a
 * file go a window of chunks at a time rather than one round trip each.
 * Each operation is a GTask (_async / _finish; the finish functions take
 * only the result, so a callback may come after sftp_client_free(), with
 * G_IO_ERROR_CLOSED). GIO only, no widgets; unit-tested against a small
 * server in the test. */
#include "core/sftp_proto.h"
#include "core/ssh_session.h"

typedef struct SftpClient SftpClient;

/* An entry of a folder (sftp_client_list_async()) */
typedef struct {
    gchar* name;     /* file name, no folder */
    SftpAttrs attrs; /* of the entry itself (a link is not followed) */
} SftpEntry;

void sftp_entry_free(gpointer entry);

/* A client speaking on in / out (refs taken); it says hello at once */
SftpClient* sftp_client_new(GInputStream* in, GOutputStream* out);
/* `ssh -s <target> sftp` and a client on its pipes; with multiplexing, only
 * through an open master connection (else NULL with
 * G_IO_ERROR_NOT_CONNECTED, as ssh_exec_async()). The process ends with the
 * client. */
SftpClient* sftp_client_spawn(const SshTarget* target, GError** error);
/* Close the connection and free; operations still running fail with
 * G_IO_ERROR_CLOSED */
void sftp_client_free(SftpClient* client);
/* FALSE once the connection is gone (sftp_client_error() says why) */
gboolean sftp_client_is_open(const SftpClient* client);
/* Why the connection closed (NULL while open) */
const GError* sftp_client_error(const SftpClient* client);
/* Whether the server announced an extension ("posix-rename@openssh.com");
 * known once the first reply came */
gboolean sftp_client_has_extension(const SftpClient* client, const gchar* name);

/* Plain success or error (mkdir, remove, rename, write) */
gboolean sftp_client_finish(GAsyncResult* result, GError** error);

/* The absolute, normalised form of path ("." is the home folder) */
void sftp_client_realpath_async(SftpClient* client, const gchar* path, GCancellable* cancel,
                                GAsyncReadyCallback done, gpointer data);
gchar* sftp_client_realpath_finish(GAsyncResult* result, GError** error);

/* The attributes of path; follow: of the target of a link */
void sftp_client_stat_async(SftpClient* client, const gchar* path, gboolean follow, GCancellable* cancel,
                            GAsyncReadyCallback done, gpointer data);
gboolean sftp_client_stat_finish(GAsyncResult* result, SftpAttrs* attrs, GError** error);

/* The entries of a folder, without "." and "..", in the server's order
 * (SftpEntry, sftp_entry_free) */
void sftp_client_list_async(SftpClient* client, const gchar* path, GCancellable* cancel,
                            GAsyncReadyCallback done, gpointer data);
GPtrArray* sftp_client_list_finish(GAsyncResult* result, GError** error);

/* The first max_bytes bytes of a file (G_MAXUINT64: all of it). *size
 * (may be NULL) gets the file's size as the server gave it (when it gave
 * none: what was read). */
void sftp_client_read_async(SftpClient* client, const gchar* path, guint64 max_bytes, GCancellable* cancel,
                            GAsyncReadyCallback done, gpointer data);
GBytes* sftp_client_read_finish(GAsyncResult* result, guint64* size, GError** error);

/* Write contents to path, safely: into a temporary file in the same folder
 * then renamed over it, with the permissions of the file replaced (new:
 * mode, e.g. 0644, less the server's umask). In place (truncated then
 * written) when path is a link, or when the folder takes no new file. */
void sftp_client_write_async(SftpClient* client, const gchar* path, GBytes* contents, guint32 mode,
                             GCancellable* cancel, GAsyncReadyCallback done, gpointer data);

void sftp_client_mkdir_async(SftpClient* client, const gchar* path, guint32 mode, GCancellable* cancel,
                             GAsyncReadyCallback done, gpointer data);
/* Remove a file (or a link) */
void sftp_client_remove_async(SftpClient* client, const gchar* path, GCancellable* cancel,
                              GAsyncReadyCallback done, gpointer data);
/* Remove an empty folder */
void sftp_client_rmdir_async(SftpClient* client, const gchar* path, GCancellable* cancel,
                             GAsyncReadyCallback done, gpointer data);
/* Rename from to to; replace: even over an existing file (atomic with
 * posix-rename@openssh.com, else removed first), otherwise that fails */
void sftp_client_rename_async(SftpClient* client, const gchar* from, const gchar* to, gboolean replace,
                              GCancellable* cancel, GAsyncReadyCallback done, gpointer data);

#endif /* SFTP_CLIENT_H */
