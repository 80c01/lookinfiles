#ifndef TRASH_RESTORE_H
#define TRASH_RESTORE_H

/* Put back a file moved to the trash (where g_file_trash() put it). Linux:
 * the freedesktop.org trash directories, files/<name> and
 * info/<name>.trashinfo with the original Path= and DeletionDate=. Windows:
 * the Recycle Bin, <drive>:\$Recycle.Bin\<user SID>\ holding $R<id><ext>
 * (the file) and $I<id><ext> (original path and deletion time). */
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

/* Original path (UTF-8) and deletion time (FILETIME, 100 ns since 1601) of
 * the bytes of a Recycle Bin $I file, version 1 (Vista to 8: path in 260
 * UTF-16 units) or 2 (Windows 10: counted path). FALSE if malformed. */
gboolean trash_win_info_parse(const guint8* data, gsize len, gchar** out_path, guint64* out_time);

/* Newest item of a Recycle Bin directory (one user's, holding $I and $R
 * files) that came from orig_path (compared as Windows paths: case and
 * slash direction ignored): returns its $R path and sets *out_info to its
 * $I file (both freed by the caller), or NULL */
gchar* trash_win_find_item(const gchar* bin_dir, const gchar* orig_path, gchar** out_info);

/* Move the newest trashed copy of orig_path back to orig_path (Linux: home
 * trash, then the trash of the mount holding it; Windows: the Recycle Bin
 * of its drive). Fails if orig_path exists again
 * or no trashed copy is found. */
gboolean trash_restore(const gchar* orig_path, GError** error);

#endif /* TRASH_RESTORE_H */
