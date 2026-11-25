#ifndef REMOTE_DISKS_H
#define REMOTE_DISKS_H

/* The disks of a remote machine for the Machines view, from the output of
 * REMOTE_DISKS_COMMAND run there: `findmnt -P` (Linux), else `df -P` (macOS,
 * BSD, busybox). Only the file systems a user browses are kept: block
 * devices and network shares, not proc, tmpfs, overlay, snaps... Pure (no
 * I/O), unit-tested. */
#include "core/disk_volumes.h"

/* The command line (for vfs_exec()) whose output remote_disks_parse() reads */
#define REMOTE_DISKS_COMMAND \
    "findmnt -P -b -o TARGET,SOURCE,FSTYPE,SIZE,AVAIL 2>/dev/null || df -P -k 2>/dev/null"

typedef struct {
    gchar* target; /* mount folder on the machine ("/", "/mnt/data") */
    gchar* name;   /* shown name: "File System" for "/", else the folder's name */
    gchar* source; /* "/dev/sda1", "server:/export", "" when unknown */
    gchar* fstype; /* "ext4", "nfs4", "" when unknown (df) */
    guint64 size;  /* bytes, 0 when unknown */
    guint64 avail; /* bytes free for the user */
    DiskVolumeKind kind;
} RemoteDisk;

void remote_disk_free(gpointer disk);

/* The disks in output (findmnt -P pairs, or df -P lines), sorted as
 * disk_volume_compare() (system first, then by kind and name), each mount
 * folder once: RemoteDisk, remote_disk_free. Never NULL. */
GPtrArray* remote_disks_parse(const gchar* output);

/* Whether a mount is a disk a user browses */
gboolean remote_disks_keep(const gchar* target, const gchar* source, const gchar* fstype);

/* The kind of a mount (network for nfs, cifs, sshfs or a "host:/" source;
 * optical for iso9660 and udf; removable under /media, /run/media, /Volumes) */
DiskVolumeKind remote_disk_kind(const gchar* target, const gchar* source, const gchar* fstype);

#endif /* REMOTE_DISKS_H */
