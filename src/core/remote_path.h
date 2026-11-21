#ifndef REMOTE_PATH_H
#define REMOTE_PATH_H

/* Paths on a connected remote machine written as the user knows them,
 * "<machine>:<remote path>" (e.g. "server:~/projects/a.txt"), instead of the
 * local folder where the machine is mounted. Dialogs show a path with
 * remote_path_display() and read the text typed back with remote_path_parse().
 * The map functions are pure (unit-tested); the others look at the connected
 * machines of app. */
#include "core/app_types.h"

/* "<name>:<root><rest>" for a path at or under mount (rest: what follows the
 * mount point), NULL for a path outside it. root is the remote folder that is
 * mounted (NULL, "" or "~": the home directory, written "~"). */
gchar* remote_path_map_to_display(const gchar* path, const gchar* mount, const gchar* name,
                                  const gchar* root);
/* Local path under mount of the text "<name>:<remote path>": the remote path
 * is root or under it, or relative to root. FALSE when the text does not start
 * with "<name>:"; TRUE with *out_local NULL when the remote path is outside
 * the mounted folder. Caller frees *out_local. */
gboolean remote_path_map_from_display(const gchar* text, const gchar* mount, const gchar* name,
                                      const gchar* root, gchar** out_local);

/* path written "<machine>:<remote path>" when it is under the mount of a
 * connected machine, else a copy of path. Caller frees. */
gchar* remote_path_display(AppData* app, const gchar* path);
/* The local path of text written "<machine>:<remote path>" for a connected
 * machine (as remote_path_map_from_display()); FALSE when text names no
 * connected machine (a local path then). */
gboolean remote_path_parse(AppData* app, const gchar* text, gchar** out_local);

#endif /* REMOTE_PATH_H */
