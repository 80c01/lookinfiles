#ifndef SSH_SESSION_H
#define SSH_SESSION_H

/* SSH sessions to remote machines through the OpenSSH client (`ssh`, on
 * Linux, macOS and Windows 10+), so ~/.ssh/config, the agent, ProxyJump and
 * known_hosts work as in a shell. Every ssh run builds its command line with
 * ssh_session_argv(); where OpenSSH can multiplex (not on Windows) a master
 * connection (ssh_master_start()) is opened once per machine and the remote
 * commands (ssh_exec_async(), kernels, SFTP sessions) go through it without
 * authenticating again. Never an interactive terminal (SSH_ARGV_NO_MUX): it
 * could stall the master (which writes to tty clients with blocking
 * writes). Programs started here get the variables set with
 * ssh_session_set_env() (the password prompt, ssh_askpass.h), have no
 * controlling terminal, and die with the application (Linux). GIO only, no
 * widgets; unit-tested with a fake ssh program. */
#include "core/ssh_target.h"

#include <gio/gio.h>

typedef enum {
    SSH_ARGV_TTY = 1 << 0,    /* -t: an interactive terminal (with SSH_ARGV_NO_MUX) */
    SSH_ARGV_MASTER = 1 << 1, /* -N, ControlMaster=yes: the master connection itself */
    SSH_ARGV_NO_MUX = 1 << 2, /* never through the master connection */
} SshArgvFlags;

/* The ssh program run (default "ssh"; tests give a fake one) */
void ssh_session_set_program(const gchar* program);
/* A variable set for every program started here (value NULL: none) */
void ssh_session_set_env(const gchar* name, const gchar* value);

/* Called by ssh_session_argv() when a command line goes through the master
 * connection of target (multiplexing on) but that master is not open: the
 * application opens it (that command, in BatchMode, asks nothing and fails
 * if it cannot log in on its own). Always on the main thread: from another
 * thread, the call is queued to the default main context. */
typedef void (*SshMasterMissingFunc)(const SshTarget* target, gpointer data);
void ssh_session_on_master_missing(SshMasterMissingFunc func, gpointer data);

/* "user@host", or "host" without user. Caller frees. */
gchar* ssh_session_destination(const SshTarget* target);
/* The ssh_config options of every connection ("Name=value": keep-alive,
 * host keys, multiplexing), for `ssh -o`. Caller unrefs. */
GPtrArray* ssh_session_options(const SshTarget* target, SshArgvFlags flags);
/* ssh [-p port] [-i key] -o ... [-t | -N] destination: append the remote
 * command (or nothing), then NULL. Through the master connection (no
 * SSH_ARGV_MASTER nor SSH_ARGV_NO_MUX, multiplexing on), ssh never asks
 * anything (BatchMode): a background command must not prompt every second.
 * Caller unrefs. */
GPtrArray* ssh_session_argv(const SshTarget* target, SshArgvFlags flags);
/* A launcher with the variables of ssh_session_set_env(), no controlling
 * terminal and death with the application, for any program that runs ssh.
 * Caller unrefs. */
GSubprocessLauncher* ssh_session_launcher(GSubprocessFlags flags);

/* Whether a master connection is used (OpenSSH multiplexing: not on
 * Windows, nor without a private runtime directory) */
gboolean ssh_session_can_multiplex(void);
/* The master connection's socket of target for this process, NULL as
 * ssh_session_can_multiplex(). Caller frees. */
gchar* ssh_session_control_path(const SshTarget* target);
/* Remove the sockets that processes gone left in the runtime directory.
 * Returns how many were removed. */
guint ssh_session_cleanup_stale(void);

/* The master connection of a machine */
typedef struct SshMaster SshMaster;
/* Called once: ok when connected, else error_text (what ssh said) */
typedef void (*SshMasterFunc)(SshMaster* master, gboolean ok, const gchar* error_text, gpointer data);

/* Connect to target (asking for a password through ssh_askpass if needed)
 * and call ready() in the main loop. Without multiplexing, checks that a
 * connection can be made and keeps nothing open. target is copied. */
SshMaster* ssh_master_start(const SshTarget* target, SshMasterFunc ready, gpointer data);
/* Close the connection (if open) and free; ready() is not called after */
void ssh_master_stop(SshMaster* master);
/* Whether the connection is (still) open, or was checked without multiplexing */
gboolean ssh_master_is_up(const SshMaster* master);

/* Remote command run: exit status (-1 when it could not run, or was
 * cancelled), its output and errors (never NULL), error set when it could
 * not run */
typedef void (*SshExecFunc)(gint status, const gchar* out, const gchar* err, const GError* error,
                            gpointer data);
/* Run command (a line for the remote shell, see ssh_session_quote()) on
 * target, with input (may be NULL) on its standard input; done() is called
 * in the main loop. Never blocks. With multiplexing, only through an open
 * master: else it fails at once (G_IO_ERROR_NOT_CONNECTED) and the master
 * is asked for (ssh_session_on_master_missing()). */
void ssh_exec_async(const SshTarget* target, const gchar* command, const gchar* input,
                    GCancellable* cancel, SshExecFunc done, gpointer data);
/* The same, waiting for the end (worker threads only), input (NULL: none)
 * on its standard input: TRUE when it ran, with its exit status and output
 * (bytes, NULs kept; caller unrefs); FALSE with error when it could not
 * run (not connected, cancelled, or ssh failed: status 255, its error
 * text) */
gboolean ssh_exec_sync(const SshTarget* target, const gchar* command, GBytes* input, GCancellable* cancel,
                       gint* status, GBytes** out, GError** error);
/* command started on target through its master, its output read as it
 * comes (g_subprocess_get_stdout_pipe(); stdin a pipe, errors dropped);
 * NULL with error as ssh_exec_sync() */
GSubprocess* ssh_spawn(const SshTarget* target, const gchar* command, GError** error);
/* arg quoted for a POSIX remote shell. Caller frees. */
gchar* ssh_session_quote(const gchar* arg);

#endif /* SSH_SESSION_H */
