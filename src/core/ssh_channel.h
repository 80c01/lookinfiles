#ifndef SSH_CHANNEL_H
#define SSH_CHANNEL_H

/* A libssh channel exposed as a GIOStream (input = stdout, output = stdin),
 * with stderr as a separate GInputStream, exit status and PTY resize.
 * Every libssh call runs on the owning link thread; async methods post
 * requests to the link context and complete in the caller's thread-default
 * context, while sync methods wait on a GCond and are refused on the main
 * thread. Layer 1, no widgets. */
#include <gio/gio.h>

#ifdef HAVE_LIBSSH
#include <libssh/libssh.h>

G_BEGIN_DECLS

typedef enum {
    SSH_CHANNEL_KIND_EXEC,      /* arg: remote command line */
    SSH_CHANNEL_KIND_SUBSYSTEM, /* arg: subsystem name (e.g. "sftp") */
    SSH_CHANNEL_KIND_SHELL,     /* arg: terminal type (e.g. "xterm-256color"), cols/rows */
} SshChannelKind;

#define SSH_TYPE_CHANNEL (ssh_channel_get_type())
G_DECLARE_FINAL_TYPE(SshChannel, ssh_channel, SSH, CHANNEL, GIOStream)

/* Standard error stream of the channel (owned by ch, like get_input_stream) */
GInputStream* ssh_channel_get_stderr(SshChannel* ch);
/* Resize the PTY of a shell channel (safe from any thread; no-op if closed) */
void ssh_channel_resize_pty(SshChannel* ch, gint cols, gint rows);
/* Close the channel at once without blocking (safe from any thread) */
void ssh_channel_force_close(SshChannel* ch);
/* TRUE once an exit status was received (or the channel ended), setting *status */
gboolean ssh_channel_exit_status(SshChannel* ch, gint* status);
/* Wait for the remote command's exit status (async, cancellable) */
void ssh_channel_wait_exit_async(SshChannel* ch, GCancellable* cancel, GAsyncReadyCallback done, gpointer data);
gint ssh_channel_wait_exit_finish(SshChannel* ch, GAsyncResult* result, GError** error);
/* Synchronous wait for worker threads only (refused on the main thread) */
gboolean ssh_channel_wait_exit_sync(SshChannel* ch, GCancellable* cancel, gint* status, GError** error);

/* ------------------------------------------------------------------------- */
/* Driver interface used only by ssh_link (on the link thread)               */
/* ------------------------------------------------------------------------- */

typedef struct SshChannelCore SshChannelCore;

typedef struct {
    gpointer link;
    GMainContext* context; /* link thread's GMainContext */
    void (*link_ref)(gpointer link);
    void (*link_unref)(gpointer link);
    void (*poke)(gpointer link); /* called on the link thread to pump/re-poll */
} SshChannelDriver;

/* Create a new channel bound to driver. If open_task is non-NULL, it is
 * completed with a new ref to the SshChannel once opened (or with an error).
 * *out_core receives a new core ref for the link's channel table. */
SshChannel* ssh_channel_create(const SshChannelDriver* driver, SshChannelKind kind, const gchar* arg,
                               gint cols, gint rows, GTask* open_task, SshChannelCore** out_core);
SshChannelCore* ssh_channel_core_ref(SshChannelCore* core);
void ssh_channel_core_unref(SshChannelCore* core);

/* Link thread only: attach the libssh handle, drive I/O, or fail on loss */
void ssh_channel_attach(SshChannelCore* core, ssh_channel raw);
void ssh_channel_pump(SshChannelCore* core);
gboolean ssh_channel_wants_write(const SshChannelCore* core);
void ssh_channel_fail(SshChannelCore* core, const GError* error);
gboolean ssh_channel_is_dead(const SshChannelCore* core);

G_END_DECLS

#endif /* HAVE_LIBSSH */

#endif /* SSH_CHANNEL_H */
