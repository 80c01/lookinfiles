#ifndef DISK_VOLUMES_H
#define DISK_VOLUMES_H

/* The disks of this computer for the Machines view: the mounted volumes of
 * a GVolumeMonitor (partitions, USB keys, optical discs, network folders
 * with a local path; drive letters on Windows) and the volumes that can be
 * mounted, plus the root file system on Unix. Listing reads only what the
 * monitor already knows (no file system access); mounting and ejecting are
 * done by the caller through the GMount / GVolume kept here.
 * GIO only, no widgets; unit-tested. */
#include <gio/gio.h>

typedef enum {
    DISK_VOLUME_SYSTEM = 0, /* the root file system, the Windows system drive */
    DISK_VOLUME_FIXED,      /* an internal disk or partition */
    DISK_VOLUME_REMOVABLE,  /* a USB key or disk, a memory card */
    DISK_VOLUME_OPTICAL,    /* a CD, DVD or Blu-ray */
    DISK_VOLUME_NETWORK,    /* a network share */
} DiskVolumeKind;

typedef struct {
    gchar* id;     /* stable key: the mount folder, or "volume:<uuid|device|name>" */
    gchar* name;   /* shown name ("Data", "USB KEY", "C:") */
    gchar* path;   /* mount folder, NULL when not mounted */
    gchar* device; /* "/dev/sdb1", NULL when unknown */
    DiskVolumeKind kind;
    gboolean can_mount;   /* not mounted, and can be */
    gboolean can_unmount; /* mounted, and can be unmounted */
    gboolean can_eject;   /* the drive or mount can be ejected */
    GMount* mount;        /* reference, NULL when not mounted */
    GVolume* volume;      /* reference, NULL for none (root, some mounts) */
} DiskVolume;

void disk_volume_free(gpointer volume);

/* The disks of monitor (NULL: g_volume_monitor_get()), sorted by
 * disk_volume_compare(), on Unix the root "/" first: DiskVolume,
 * disk_volume_free. Never NULL. */
GPtrArray* disk_volumes_list(GVolumeMonitor* monitor);

/* Order: system, fixed, removable, optical, network; mounted before not
 * mounted; then by name (case-insensitive, natural) */
gint disk_volume_compare(const DiskVolume* a, const DiskVolume* b);

/* Kind from the icon names of a mount or volume (GThemedIcon names, NULL
 * ended, NULL allowed), whether its drive is removable and its path */
DiskVolumeKind disk_volume_kind_guess(const gchar* const* icon_names, gboolean removable, const gchar* path);

/* The disk of volumes whose id is id, NULL when none (borrowed) */
DiskVolume* disk_volumes_find(GPtrArray* volumes, const gchar* id);

/* Glyph name (icons.h) of a kind */
const gchar* disk_volume_glyph(DiskVolumeKind kind);

/* Translated name of a kind: "System disk", "Removable disk"... */
const gchar* disk_volume_kind_name(DiskVolumeKind kind);

#endif /* DISK_VOLUMES_H */
