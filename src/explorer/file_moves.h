#ifndef FILE_MOVES_H
#define FILE_MOVES_H

/* Moves of files and folders in the files view (rename, move to the trash),
 * with the editor tabs and expanded folders following, and undo of the last
 * one (key u, Undo in the status bar). On a remote machine (vfs.h) they go
 * over SFTP. */
#include "core/app_types.h"

/* Rename or move from to to (same filesystem); remembered for undo */
gboolean file_moves_rename(AppData* app, const gchar* from, const gchar* to, GError** error);
/* Move path to the trash; remembered for undo, with a status bar message */
gboolean file_moves_trash(AppData* app, const gchar* path, GError** error);

/* Delete path (a folder with all it holds) on a remote machine, which has
 * no trash: for good, not remembered for undo; a status bar message */
gboolean file_moves_delete_remote(AppData* app, const gchar* path, GError** error);
/* path exists (over SFTP for a remote machine's file, vfs.h) */
gboolean file_moves_exists(const gchar* path);

/* A trash or move can be undone */
gboolean file_moves_can_undo(AppData* app);
/* Undo the last trash or move. Returns the path to select (caller frees), or
 * NULL when nothing was done; the outcome is told in the status bar. */
gchar* file_moves_undo(AppData* app);

#endif /* FILE_MOVES_H */
