#ifndef SSH_WATCH_H
#define SSH_WATCH_H

/* A level-triggered GSource on a socket read and written by someone else
 * (libssh): ready while the socket is readable / writable as asked, never
 * closes it. Not GSocket: on Windows a GSocket remembers an event until its
 * own I/O clears it, so a socket read by libssh stays "ready" for ever (a
 * core at 100 %, the connection never progressing). Unix: a plain GPollFD.
 * Windows: a WSAEVENT to wake the loop, WSAPoll() for the real state.
 * GLib only, no widgets; unit-tested. */
#include <glib.h>

/* fd: the socket (an int on Unix, a SOCKET on Windows); cond: G_IO_IN and/or
 * G_IO_OUT (G_IO_HUP and G_IO_ERR are always watched). Set the callback with
 * g_source_set_callback() (a GSourceFunc), attach, destroy and unref as any
 * source. NULL when the socket cannot be watched. */
GSource* ssh_watch_new(gint64 fd, GIOCondition cond);

#endif /* SSH_WATCH_H */
