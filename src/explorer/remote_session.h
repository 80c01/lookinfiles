#ifndef REMOTE_SESSION_H
#define REMOTE_SESSION_H

/* The connection to a remote machine: one master SSH connection per machine
 * (ssh_session.h) for its files (over SFTP, vfs.h), its remote commands
 * (kernels, System Monitor); the password and passphrase prompts of ssh
 * shown as dialogs (ssh_askpass.h). */
#include "core/app_types.h"
#include "explorer/remote_store.h"

/* Listen for ssh prompts (argv0: how this program was started) and remove
 * the sockets of crashed sessions. Once, at start-up. */
void remote_session_init(AppData* app, const gchar* argv0);
/* Close every master connection; at exit */
void remote_session_shutdown(void);

/* Open the master connection of rm (asking for a password if needed). The
 * interface stays live meanwhile (nested main loop, as app_dialog_run()).
 * TRUE when connected; else FALSE with *error_text (what ssh said; caller
 * frees) or, when rm is already being connected, NULL. */
gboolean remote_session_connect(AppData* app, RemoteMachine* rm, gchar** error_text);
/* Open the master connection of rm in the background unless it is open (it
 * may have dropped) */
void remote_session_open_master(RemoteMachine* rm);
/* Read the files of rm, seen at rm->mount_point, over SFTP through its
 * master connection (vfs.h) */
void remote_session_files_open(RemoteMachine* rm);
/* Close the SFTP session and the master connection of rm */
void remote_session_close(RemoteMachine* rm);

#endif /* REMOTE_SESSION_H */
