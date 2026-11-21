#ifndef REMOTE_SESSION_H
#define REMOTE_SESSION_H

/* The connection to a remote machine: one master SSH connection per machine
 * (ssh_session.h), so the sshfs mount and the remote commands authenticate
 * once (the terminals have connections of their own); the password and
 * passphrase prompts of ssh shown as dialogs (ssh_askpass.h). */
#include "core/app_types.h"
#include "explorer/remote_store.h"

/* Listen for ssh prompts (argv0: how this program was started) and remove
 * the sockets of crashed sessions. Once, at start-up. */
void remote_session_init(AppData* app, const gchar* argv0);
/* Close every master connection; at exit */
void remote_session_shutdown(void);

/* Connect to rm (asking for a password if needed) and mount its remote
 * directory on mount_dir with sshfs. The interface stays live meanwhile
 * (nested main loop, as app_dialog_run()). TRUE when mounted; else FALSE
 * with *error_text (what ssh or sshfs said; caller frees) or, when rm is
 * already being connected, NULL. */
gboolean remote_session_mount(AppData* app, RemoteMachine* rm, const gchar* mount_dir, gchar** error_text);
/* Close the master connection of rm (after unmounting) */
void remote_session_close(RemoteMachine* rm);

#endif /* REMOTE_SESSION_H */
