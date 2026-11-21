#ifndef SSH_SESSION_H
#define SSH_SESSION_H

/* SSH sessions to remote machines through the OpenSSH client (`ssh`, on
 * Linux, macOS and Windows 10+), so ~/.ssh/config, the agent, ProxyJump and
 * known_hosts work as in a shell. Every ssh run builds its command line with
 * ssh_session_argv(); where OpenSSH can multiplex (not on Windows) a master
 * connection (ssh_master_start()) is opened once per machine and every
 * later ssh (sshfs, terminal, remote commands) goes through it without
 * authenticating again. Programs started here get the variables set with
 * ssh_session_set_env() (the password prompt, ssh_askpass.h), have no
 * controlling terminal, and die with the application (Linux). GIO only, no
 * widgets; unit-tested with a fake ssh program. */
#include <gio/gio.h>

/* Where to connect (strings owned by the caller) */
typedef struct {
    const gchar* host;          /* host name, IP or ~/.ssh/config Host */
    const gchar* user;          /* NULL or "": from ~/.ssh/config or the local user */
    gint port;                  /* 0 or 22: the default */
    const gchar* identity_file; /* NULL or "": the usual keys */
} SshTarget;

typedef enum {
    SSH_ARGV_TTY = 1 << 0,    /* -t: an interactive terminal */
    SSH_ARGV_MASTER = 1 << 1, /* -M -N: the master connection itself */
    SSH_ARGV_NO_MUX = 1 << 2, /* never through the master connection */
} SshArgvFlags;

/* The ssh program run (default "ssh"; tests give a fake one) */
void ssh_session_set_program(const gchar* program);
/* A variable set for every program started here (value NULL: none) */
void ssh_session_set_env(const gchar* name, const gchar* value);

/* "user@host", or "host" without user. Caller frees. */
gchar* ssh_session_destination(const SshTarget* target);
/* The ssh_config options of every connection ("Name=value": keep-alive,
 * host keys, multiplexing), for `ssh -o` or `sshfs -o`. Caller unrefs. */
GPtrArray* ssh_session_options(const SshTarget* target, SshArgvFlags flags);
/* ssh [-p port] [-i key] -o ... [-t | -M -N] destination: append the remote
 * command (or nothing), then NULL. Caller unrefs. */
GPtrArray* ssh_session_argv(const SshTarget* target, SshArgvFlags flags);
/* A launcher with the variables of ssh_session_set_env(), no controlling
 * terminal and death with the application, for any program that runs ssh
 * (sshfs). Caller unrefs. */
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
 * in the main loop. Never blocks. */
void ssh_exec_async(const SshTarget* target, const gchar* command, const gchar* input,
                    GCancellable* cancel, SshExecFunc done, gpointer data);
/* arg quoted for a POSIX remote shell. Caller frees. */
gchar* ssh_session_quote(const gchar* arg);

#endif /* SSH_SESSION_H */
