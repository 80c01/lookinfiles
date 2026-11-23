#ifndef SSH_PROCESS_H
#define SSH_PROCESS_H

/* A remote command kept running while its output is read
 * (ssh_spawn(), vfs_spawn()): either an ssh program (GSubprocess) or, with
 * libssh, an exec channel of the machine's link, so that no ssh program
 * logs in by itself. Standard error is dropped. Its streams block: worker
 * threads only. GIO only, no widgets. */
#include <gio/gio.h>

typedef struct SshProcess SshProcess;

/* Wrap p (a ref is taken), started with stdin and stdout pipes */
SshProcess* ssh_process_new_subprocess(GSubprocess* p);
/* Wrap an opened exec channel (a GObject from ssh_channel.h, NULL-free; a
 * ref is taken). Its standard error is read and dropped by a thread. */
SshProcess* ssh_process_new_channel(GIOStream* channel);

/* The command's standard input (close it for EOF) and output (owned by p) */
GOutputStream* ssh_process_get_stdin(SshProcess* p);
GInputStream* ssh_process_get_stdout(SshProcess* p);
/* Stop the command at once (its streams then fail) */
void ssh_process_force_exit(SshProcess* p);
/* Wait for the end: its exit status, -1 when unknown */
gint ssh_process_wait(SshProcess* p);
/* Stop it if still running, and free */
void ssh_process_free(SshProcess* p);

#endif /* SSH_PROCESS_H */
