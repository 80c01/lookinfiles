#ifndef TRASH_RESTORE_H
#define TRASH_RESTORE_H

/* Put back a file moved to the trash, from the freedesktop.org trash
 * directories (the ones g_file_trash() writes to): files/<name> and
 * info/<name>.trashinfo with the original Path= and DeletionDate=. */
#include <glib.h>

/* Original path and deletion date ("2026-10-31T12:00:00") of a .trashinfo
 * text. topdir is the mount point of a $topdir/.Trash-$uid trash (its paths
 * may be relative to it), NULL for the home trash. FALSE if malformed. */
gboolean trash_info_parse(const gchar* contents, const gchar* topdir,
                          gchar** out_path, gchar** out_date);

/* Newest item of trash_dir (holding files/ and info/) that came from
 * orig_path: returns its path under files/ and sets *out_info to its
 * .trashinfo (both freed by the caller), or NULL */
gchar* trash_find_item(const gchar* trash_dir, const gchar* topdir,
                       const gchar* orig_path, gchar** out_info);

/* Move the newest trashed copy of orig_path back to orig_path (home trash,
 * then the trash of the mount holding it). Fails if orig_path exists again
 * or no trashed copy is found. */
gboolean trash_restore(const gchar* orig_path, GError** error);

#endif /* TRASH_RESTORE_H */
