#ifndef ARCHIVE_LISTING_H
#define ARCHIVE_LISTING_H

#include <gio/gio.h>

/* Listing of archives for the preview, read with GLib / GIO only:
 *   - ZIP (and JAR, APK, wheels...): central directory, sizes, dates;
 *   - tar (ustar, GNU long names, pax paths), plain or gzip-compressed;
 *   - gzip of a single file: original name and size, start of the content;
 *   - ar (static libraries, .deb packages).
 * With libarchive (HAVE_LIBARCHIVE), every format it reads: 7-Zip, RAR, ISO,
 * cpio, RPM, CAB, and xz / bzip2 / zstd / lzip compression (of a tar or of a
 * single file). Without it these are only named (archive_listing_format_name()). Reading is bounded: at most max_entries entries
 * are kept, and a compressed tar is read for a limited time and number of
 * bytes (the listing then says it is incomplete). Unit-tested. */

typedef enum {
    ARCHIVE_ENTRY_FILE,
    ARCHIVE_ENTRY_DIR,
    ARCHIVE_ENTRY_SYMLINK,
    ARCHIVE_ENTRY_HARDLINK,
    ARCHIVE_ENTRY_OTHER     /* device, fifo */
} ArchiveEntryType;

typedef struct {
    gchar* path;
    ArchiveEntryType type;
    guint64 size;
    gint64 packed;          /* compressed size, -1 if unknown */
    gint64 mtime;           /* Unix time, -1 if unknown */
    gchar* link;            /* link target, or NULL */
    gboolean encrypted;
} ArchiveEntry;

typedef struct {
    const gchar* format;    /* "ZIP archive", "tar archive", "gzip-compressed tar archive"... (static) */
    GArray* entries;        /* ArchiveEntry: the first max_entries */
    guint n_entries;        /* entries seen (all of them unless incomplete) */
    guint n_files;
    guint n_dirs;
    guint64 total_size;     /* uncompressed size of the entries seen */
    guint64 total_packed;   /* compressed size of the entries seen (ZIP) */
    gboolean incomplete;    /* stopped before the end of a compressed tar */
    /* gzip of a single file */
    gboolean single;
    gchar* inner_name;      /* original name from the gzip header, or NULL */
    gint64 inner_size;      /* original size (gzip trailer, modulo 4 GiB), -1 if unknown */
    GBytes* inner_head;     /* first bytes of the decompressed content */
    /* JAR manifest main attributes ("Main-Class: x" lines), or NULL */
    gchar* manifest;
} ArchiveListing;

/* Name of the archive or compression format of these first bytes, or NULL */
const gchar* archive_listing_format_name(const guchar* head, gsize len);

/* The format name of a listing ("ZIP archive", "xz-compressed tar archive")
 * in the user's language; the format names stay in English in the code.
 * Caller frees. */
gchar* archive_listing_format_label(const gchar* format);

/* TRUE if the format of these bytes can be listed by archive_list() */
gboolean archive_listing_can_list(const guchar* head, gsize len);

/* List an archive on disk. NULL with error set (G_IO_ERROR_NOT_SUPPORTED for
 * a format that cannot be listed) if it cannot be read. */
ArchiveListing* archive_list(const gchar* path, guint max_entries, GCancellable* cancellable,
                             GError** error);
void archive_listing_free(ArchiveListing* listing);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ArchiveListing, archive_listing_free)

/* Bytes of decompressed content kept in inner_head */
#define ARCHIVE_INNER_HEAD (64 * 1024)

#endif /* ARCHIVE_LISTING_H */
