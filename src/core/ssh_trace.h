#ifndef SSH_TRACE_H
#define SSH_TRACE_H

/* Debug trace of the libssh links, off unless LOOKINFILES_SSH_LOG names a
 * file (the Windows build has no console): libssh's own messages and the
 * link's steps are appended there, one line each, with the time and thread.
 * Never a secret: callers log states and return codes only. GLib + libssh,
 * no widgets. */
#include <glib.h>

#ifdef HAVE_LIBSSH
#include <libssh/libssh.h>

/* Read LOOKINFILES_SSH_LOG once */
void ssh_trace_setup(void);
/* Route libssh's own messages of the calling thread there (libssh keeps its
 * log level and callback per thread: call it on every link thread) */
void ssh_trace_thread(void);
/* One line with the session's resolved options (host, user, port, proxy) */
void ssh_trace_session(ssh_session session);
/* TRUE for the first calls on *counter, then one in a thousand: a busy
 * loop stays visible without filling the disk */
gboolean ssh_trace_sample(guint* counter);
/* Whether the log is on (to skip building costly messages) */
gboolean ssh_trace_enabled(void);
/* Append one line (no-op when off) */
void ssh_trace(const gchar* format, ...) G_GNUC_PRINTF(1, 2);

#endif /* HAVE_LIBSSH */

#endif /* SSH_TRACE_H */
