#ifndef ZIP_READER_H
#define ZIP_READER_H

#include <glib.h>

/* Read-only access to the entries of a ZIP archive (stored or deflated,
 * ZIP64 included, no encryption). Reads the central directory once, then
 * one entry at a time on demand. Pure GLib/GIO: usable in a worker thread. */
typedef struct ZipReader ZipReader;

/* Open an archive on disk. NULL with error set if it is not a readable ZIP. */
ZipReader* zip_reader_open(const gchar* path, GError** error);

/* Open an archive held in memory (keeps a reference to data) */
ZipReader* zip_reader_new_from_bytes(GBytes* data, GError** error);

void zip_reader_free(ZipReader* zip);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ZipReader, zip_reader_free)

/* Number of entries, and the name of entry i (as stored: '/' separated) */
guint zip_reader_get_n_entries(const ZipReader* zip);
const gchar* zip_reader_get_name(const ZipReader* zip, guint i);

typedef struct {
    const gchar* name;        /* owned by the reader */
    guint64 size;             /* uncompressed */
    guint64 compressed_size;
    guint16 method;           /* 0 stored, 8 deflated, others unsupported */
    gboolean encrypted;
    gboolean is_dir;          /* name ends with '/' */
    gint64 mtime;             /* DOS local date and time as if UTC, -1 if invalid */
} ZipEntryInfo;

/* Details of entry i; FALSE if there is none */
gboolean zip_reader_get_info(const ZipReader* zip, guint i, ZipEntryInfo* info);

/* TRUE if the archive has an entry with this exact name */
gboolean zip_reader_has(const ZipReader* zip, const gchar* name);

/* Uncompressed size of an entry, -1 if there is none */
gint64 zip_reader_get_size(const ZipReader* zip, const gchar* name);

/* Uncompressed content of an entry. Fails (NULL, error set) if the entry is
 * missing, encrypted, uses another compression method, is damaged or is
 * larger than max_size bytes. The data is followed by a NUL byte that is not
 * counted in the size, so text entries can be used as strings. */
GBytes* zip_reader_read(ZipReader* zip, const gchar* name, gsize max_size, GError** error);

#endif /* ZIP_READER_H */
