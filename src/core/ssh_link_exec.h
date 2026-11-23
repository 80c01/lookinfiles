#ifndef SSH_LINK_EXEC_H
#define SSH_LINK_EXEC_H

/* A remote command run on an `exec` channel of a libssh link (ssh_link.h):
 * no ssh program started, no login of its own. ssh_session.c uses it for
 * ssh_exec_sync() / ssh_exec_async() when the machine has a link. Built
 * only with libssh. GIO only, no widgets; unit-tested with test_ssh_link. */
#include "core/ssh_link.h"

#include <gio/gio.h>

/* Run command on link: write input (NULL: none) to its stdin, then close
 * it; collect stdout and stderr (read together, so a full stderr never
 * stalls stdout). *status is the command's exit status. FALSE (error set,
 * *out and *err NULL) when the channel could not open, broke, ended
 * without a status, or cancel fired. Any thread; on the main thread it
 * waits for a worker thread (as g_subprocess_communicate() would block). */
gboolean ssh_link_exec(SshLink* link, const gchar* command, GBytes* input, GCancellable* cancel,
                       gint* status, GBytes** out, GBytes** err, GError** error);

#endif /* SSH_LINK_EXEC_H */
