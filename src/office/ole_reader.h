#ifndef OLE_READER_H
#define OLE_READER_H

#include <glib.h>

/* Read-only access to OLE2 compound files (Microsoft Compound File Binary:
 * Office 97-2003 .doc / .xls / .ppt, Outlook .msg, .msi...): a small FAT
 * file system inside the file. Header, FAT and DIFAT, mini FAT and mini
 * stream, directory tree; streams are read on demand with a size limit.
 * Loops, out-of-range sectors and oversized tables fail cleanly (corrupted
 * files must not hang or crash). The file is mapped, not read whole. Pure
 * GLib: usable in a worker thread. */

#define OLE_MAGIC "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1"

typedef struct OleReader OleReader;

typedef enum {
    OLE_ENTRY_STORAGE = 1,
    OLE_ENTRY_STREAM = 2,
    OLE_ENTRY_ROOT = 5
} OleEntryType;

typedef struct {
    const gchar* name;   /* UTF-8, as stored ("WordDocument", "\005SummaryInformation") */
    const gchar* path;   /* '/'-separated path from the root ("__attach_version1.0_#00000000/__substg1.0_3001001F") */
    OleEntryType type;
    guint64 size;        /* streams */
    gint depth;          /* 0 for children of the root */
} OleEntryInfo;

/* Open a compound file on disk. NULL with error set if it is not a readable
 * compound file. */
OleReader* ole_reader_open(const gchar* path, GError** error);
/* Open a compound file held in memory (keeps a reference to data) */
OleReader* ole_reader_new_from_bytes(GBytes* data, GError** error);
void ole_reader_free(OleReader* ole);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(OleReader, ole_reader_free)

/* Storages and streams below the root, in tree order (a storage before its
 * children, children sorted by name) */
guint ole_reader_get_n_entries(const OleReader* ole);
gboolean ole_reader_get_info(const OleReader* ole, guint i, OleEntryInfo* info);

/* Index of the entry with this path (case-insensitive, like the format), -1
 * if none */
gint ole_reader_find(const OleReader* ole, const gchar* path);
gboolean ole_reader_has(const OleReader* ole, const gchar* path);

/* Content of a stream. Fails (NULL, error set) if the stream is missing,
 * damaged, or larger than max_size bytes. The data is followed by a NUL byte
 * that is not counted in the size. */
GBytes* ole_reader_read(OleReader* ole, const gchar* path, gsize max_size, GError** error);
GBytes* ole_reader_read_entry(OleReader* ole, guint i, gsize max_size, GError** error);

/* Class id (16 bytes) of the root storage, or NULL when all zero */
const guint8* ole_reader_get_root_clsid(const OleReader* ole);

#endif /* OLE_READER_H */
