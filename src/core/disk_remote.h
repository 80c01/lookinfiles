#ifndef DISK_REMOTE_H
#define DISK_REMOTE_H

/* The tree of a remote machine's folder for a disk usage scan
 * (disk_scan.h): one find run there lists every entry with what lstat()
 * would say (type, size, blocks, date, device, inode, links), kept here by
 * folder for the scan to read instead of the disk. Pure GLib, unit-tested. */
#include <glib.h>
#include <glib/gstdio.h>

/* The shell command printing the folder itself, then every entry below it:
 * "<type> <size> <blocks> <mtime> <dev> <inode> <links> <path>\0" (type: f,
 * d, l...; blocks of 512 bytes; path relative, "." for the folder); with
 * one_filesystem, other filesystems are not entered (their mount points
 * listed). Needs GNU find (-printf). Caller frees. */
gchar* disk_remote_command(gboolean one_filesystem);

typedef struct DiskRemoteListing DiskRemoteListing;
DiskRemoteListing* disk_remote_listing_new(void);
void disk_remote_listing_free(DiskRemoteListing* l);

/* Add one record of that command (without its NUL); FALSE when it is not
 * one. *rel (may be NULL) points at its path in record. */
gboolean disk_remote_listing_add(DiskRemoteListing* l, const gchar* record, const gchar** rel);

/* The folder itself ("." was listed), NULL if not */
const GStatBuf* disk_remote_listing_root(const DiskRemoteListing* l);
/* The entries of the folder rel ("" for the folder itself, "a/b"): name ->
 * GStatBuf*, NULL when it has none listed */
GHashTable* disk_remote_listing_dir(const DiskRemoteListing* l, const gchar* rel);

#endif /* DISK_REMOTE_H */
