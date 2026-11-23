#ifndef SSH_TRACE_H
#define SSH_TRACE_H

/* Debug trace of the libssh links, off unless LOOKINFILES_SSH_LOG names a
 * file (the Windows build has no console): libssh's own messages and the
 * link's steps are appended there, one line each, with the time and thread.
 * Never a secret: callers log states and return codes only. GLib + libssh,
 * no widgets. */
#include <glib.h>

#ifdef HAVE_LIBSSH

/* Read LOOKINFILES_SSH_LOG once; with it set, route libssh's log there */
void ssh_trace_setup(void);
/* Whether the log is on (to skip building costly messages) */
gboolean ssh_trace_enabled(void);
/* Append one line (no-op when off) */
void ssh_trace(const gchar* format, ...) G_GNUC_PRINTF(1, 2);

#endif /* HAVE_LIBSSH */

#endif /* SSH_TRACE_H */
