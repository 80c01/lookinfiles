#ifndef SSH_TARGET_H
#define SSH_TARGET_H

/* Target of an SSH connection, shared between ssh_session and ssh_link
 * without creating an include cycle (header-only). */
#include <glib.h>

/* Where to connect (strings owned by the caller) */
typedef struct {
    const gchar* host;          /* host name, IP or ~/.ssh/config Host */
    const gchar* user;          /* NULL or "": from ~/.ssh/config or the local user */
    gint port;                  /* 0 or 22: the default */
    const gchar* identity_file; /* NULL or "": the usual keys */
} SshTarget;

#endif /* SSH_TARGET_H */
