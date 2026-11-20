#ifndef REMOTE_MOUNTS_H
#define REMOTE_MOUNTS_H

/* Where remote machines are mounted with sshfs, and the cleanup of mounts a
 * crashed session left behind. Mounts live in the runtime directory
 * (app_runtime_dir()/remotes/<id>), never under ~/.cache: a cache cleaner
 * that recursed into a forgotten mount would delete the remote files.
 * Each mount has an owner file (<mount>.owner, the pid of the instance that
 * mounted it) so a starting instance leaves the mounts of a running one. */
#include <glib.h>

/* app_runtime_dir()/remotes, created; NULL when there is no private runtime
 * directory. Caller frees. */
gchar* remote_mounts_root(void);
/* Mount point of a machine (created), NULL as remote_mounts_root(). Caller frees. */
gchar* remote_mounts_dir(const gchar* machine_id);

/* Mount points of the sshfs mounts in mounts_text (the format of
 * /proc/self/mounts, octal escapes decoded) inside root. Caller unrefs. */
GPtrArray* remote_mounts_parse(const gchar* mounts_text, const gchar* root);
/* Whether path is an sshfs mount point now (read from /proc/self/mounts,
 * never stat()ed: a dead mount can block) */
gboolean remote_mounts_is_mounted(const gchar* path);

/* Record this process as the owner of the mount at path */
void remote_mounts_claim(const gchar* path);
/* Pid in the owner file of the mount at path, 0 if none */
gint remote_mounts_owner(const gchar* path);

/* Unmount path (fusermount3, fusermount, then a lazy unmount when busy);
 * once unmounted, drop the owner file and the empty mount point. TRUE when
 * nothing is mounted there any more. */
gboolean remote_mounts_unmount(const gchar* path);

/* Unmount the sshfs mounts left by crashed sessions: those of the runtime
 * root whose owner is gone, and every one under the old location
 * (~/.cache/lookinfiles/remotes). Returns how many were unmounted. */
guint remote_mounts_cleanup_stale(void);

#endif /* REMOTE_MOUNTS_H */
