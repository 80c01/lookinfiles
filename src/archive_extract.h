#ifndef ARCHIVE_EXTRACT_H
#define ARCHIVE_EXTRACT_H

#include <gio/gio.h>

/* Reading one entry out of an archive, so the explorer can browse archives
 * like folders: the entry is written to a cache file and previewed from
 * there. ZIP is read with GIO only; every other format needs libarchive
 * (HAVE_LIBARCHIVE). Safe to call from a worker thread. Unit-tested. */

/* TRUE if the explorer can open this file name as a folder (by extension and
 * by the formats this build can read) */
gboolean archive_extract_can_browse(const gchar* name);

/* Path of an entry relative to the archive root: without "./" or leading and
 * trailing '/'. NULL if the entry would leave the root (".." component) or is
 * empty. Free with g_free(). */
gchar* archive_extract_clean_path(const gchar* entry);

/* Cache file of an entry: <cache dir>/archive/<id of the archive>/<clean path>.
 * The id changes when the archive's size or modification time does. NULL if
 * the archive cannot be read or the entry path is not safe. */
gchar* archive_extract_cache_path(const gchar* archive, const gchar* entry);

/* Write the entry (exact name as listed by archive_list()) to dest, creating
 * its folders. Fails with G_IO_ERROR_FAILED when the entry is larger than
 * max_size or is missing, G_IO_ERROR_NOT_SUPPORTED for a format this build
 * cannot read. */
gboolean archive_extract_entry(const gchar* archive, const gchar* entry, const gchar* dest,
                               guint64 max_size, GCancellable* cancellable, GError** error);

#endif /* ARCHIVE_EXTRACT_H */
