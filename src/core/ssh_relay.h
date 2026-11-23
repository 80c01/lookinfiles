#ifndef SSH_RELAY_H
#define SSH_RELAY_H

/* Loopback TCP relay between a child process (Jupyter kernel, language server,
 * terminal) and an SSH channel on an existing SshLink. Both sides live here:
 * the async server on a thread of its own in the application instance, and the
 * helper invoked as `lookinfiles --ssh-channel <port> <id> [command...]`.
 * Layer 1, GIO + libssh, no widgets. */
#include "core/ssh_link.h"

#include <gio/gio.h>

#ifdef HAVE_LIBSSH

G_BEGIN_DECLS

/* Path of this executable for relay command lines (NULL disables relay argv) */
void ssh_relay_set_program(const gchar* program);

/* Listen on 127.0.0.1:0 on a dedicated thread with its own GMainContext.
 * Idempotent: returns TRUE when already serving. */
gboolean ssh_relay_serve(GError** error);
/* Bound loopback port, or 0 when not serving */
guint16 ssh_relay_port(void);
/* Stop listening, abort active relays, delete every remaining ticket file */
void ssh_relay_stop(void);

/* Issue a single-use ticket for link (written 0600 to
 * app_runtime_dir()/relay/<id>) and return [<program>, "--ssh-channel",
 * "<port>", "<id>"] (caller appends the remote command and NULL, then unrefs).
 * Returns NULL when not serving, program is unset, link is not up, or
 * app_runtime_dir() is unavailable. */
GPtrArray* ssh_relay_argv(SshLink* link, gboolean tty);

/* Whether this process was started as a relay helper (--ssh-channel ...) */
gboolean ssh_relay_is_helper(int argc, char** argv);
/* Helper entry point: read and delete the ticket file, connect to the
 * instance, copy stdio <-> SSH channel, return remote exit status (255 on
 * connection loss). */
int ssh_relay_helper_main(int argc, char** argv);

/* Test hook: ticket lifetime in milliseconds (0 restores the 60 s default) */
void ssh_relay_set_ticket_ttl_ms(guint ttl_ms);

G_END_DECLS

#endif /* HAVE_LIBSSH */

#endif /* SSH_RELAY_H */
