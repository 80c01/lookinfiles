#ifndef GIT_MIRROR_H
#define GIT_MIRROR_H

/* Local read-only copies of the .git folder of remote repositories (vfs.h),
 * for the views of the Git tab that only read a repository (libgit2 opens
 * local files only): app_cache_dir()/git-mirrors/<id>/.git. The first sync
 * copies every file of .git, the next ones the files new or changed since
 * (git objects never change: mostly the refs, the index, the new packs),
 * and remove the files gone; all in one tar stream run on the machine.
 * Blocking: worker threads only. The listing and the tar reading are
 * unit-tested. */
#include <gio/gio.h>

/* The shell command, run at the top of a work tree, printing each file of
 * .git as "<size> <mtime> <path>\0" (path below .git; lock files and Git
 * LFS objects left out) */
#define GIT_MIRROR_LIST_COMMAND                                                                                    \
    "cd .git && find . -type f ! -name '*.lock' ! -path './lfs/*' -printf '%s %T@ %P\\0' 2>/dev/null"

/* The shell command, run at the top of a work tree, writing to its output
 * a ustar archive of the files of .git its input names (each ended by a NUL) */
#define GIT_MIRROR_TAR_COMMAND "cd .git && tar --format=ustar -cf - --null -T - 2>/dev/null"

/* That listing (or a manifest saved from one): path -> "<size> <mtime>" */
GHashTable* git_mirror_parse_listing(const gchar* data, gsize len);

/* Read a ustar archive from in into dir (each file written beside, then
 * renamed over the one it replaces; a path leaving dir is refused).
 * *files: how many were written. FALSE with error when the archive is cut
 * short, a file cannot be written, or cancel is triggered. */
gboolean git_mirror_extract(GInputStream* in, const gchar* dir, GCancellable* cancel, guint* files, GError** error);

/* The folder whose .git is the copy of the one of top (a remote machine's
 * work tree, its path as seen here): what to give libgit2. Caller frees. */
gchar* git_mirror_root(const gchar* top);

/* Bring the copy of top's .git up to date (one sync at a time for each
 * repository: a second waits for the first); *copied (may be NULL): the
 * files copied. FALSE with error when it could not be read. */
gboolean git_mirror_sync(const gchar* top, GCancellable* cancel, guint* copied, GError** error);

#endif /* GIT_MIRROR_H */
