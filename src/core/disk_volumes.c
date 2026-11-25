#include "core/disk_volumes.h"
#include "core/i18n.h"

#include <string.h>

void disk_volume_free(gpointer data) {
    DiskVolume* v = (DiskVolume*)data;
    if (v == NULL) {
        return;
    }
    g_free(v->id);
    g_free(v->name);
    g_free(v->path);
    g_free(v->device);
    g_clear_object(&v->mount);
    g_clear_object(&v->volume);
    g_free(v);
}

/* path is the Windows system drive ("C:\" for SystemDrive "C:") */
static gboolean is_system_drive(const gchar* path) {
#ifdef G_OS_WIN32
    const gchar* sys = g_getenv("SystemDrive");
    if (sys == NULL || sys[0] == '\0') {
        sys = "C:";
    }
    return g_ascii_strncasecmp(path, sys, strlen(sys)) == 0 &&
           (path[strlen(sys)] == '\0' || path[strlen(sys)] == '\\' || path[strlen(sys)] == '/') &&
           strlen(path) <= strlen(sys) + 1;
#else
    (void)path;
    return FALSE;
#endif
}

DiskVolumeKind disk_volume_kind_guess(const gchar* const* icon_names, gboolean removable, const gchar* path) {
    if (path != NULL && (strcmp(path, "/") == 0 || is_system_drive(path))) {
        return DISK_VOLUME_SYSTEM;
    }
    for (gsize i = 0; icon_names != NULL && icon_names[i] != NULL; i++) {
        const gchar* n = icon_names[i];
        if (strstr(n, "optical") != NULL) {
            return DISK_VOLUME_OPTICAL;
        }
        if (strstr(n, "remote") != NULL || strstr(n, "network") != NULL || strstr(n, "smb") != NULL) {
            return DISK_VOLUME_NETWORK;
        }
    }
    if (removable) {
        return DISK_VOLUME_REMOVABLE;
    }
    for (gsize i = 0; icon_names != NULL && icon_names[i] != NULL; i++) {
        const gchar* n = icon_names[i];
        if (strstr(n, "removable") != NULL || strstr(n, "usb") != NULL || strstr(n, "flash") != NULL) {
            return DISK_VOLUME_REMOVABLE;
        }
    }
    return DISK_VOLUME_FIXED;
}

gint disk_volume_compare(const DiskVolume* a, const DiskVolume* b) {
    if (a->kind != b->kind) {
        return (a->kind < b->kind) ? -1 : 1;
    }
    gboolean ma = (a->path != NULL), mb = (b->path != NULL);
    if (ma != mb) {
        return ma ? -1 : 1;
    }
    gchar* fa = g_utf8_casefold(a->name != NULL ? a->name : "", -1);
    gchar* fb = g_utf8_casefold(b->name != NULL ? b->name : "", -1);
    gchar* ka = g_utf8_collate_key_for_filename(fa, -1);
    gchar* kb = g_utf8_collate_key_for_filename(fb, -1);
    gint r = strcmp(ka, kb);
    g_free(ka);
    g_free(kb);
    g_free(fa);
    g_free(fb);
    return r;
}

static gint compare_ptrs(gconstpointer a, gconstpointer b) {
    return disk_volume_compare(*(const DiskVolume* const*)a, *(const DiskVolume* const*)b);
}

/* Icon names of a GIcon when it is themed, else NULL */
static const gchar* const* icon_names(GIcon* icon) {
    return G_IS_THEMED_ICON(icon) ? (const gchar* const*)g_themed_icon_get_names(G_THEMED_ICON(icon)) : NULL;
}

static gboolean drive_removable(GDrive* drive) {
    return drive != NULL && (g_drive_is_removable(drive) || g_drive_is_media_removable(drive));
}

static void add_mount(GPtrArray* out, GMount* mount) {
    if (g_mount_is_shadowed(mount)) {
        return;
    }
    GFile* root = g_mount_get_root(mount);
    gchar* path = (root != NULL) ? g_file_get_path(root) : NULL;
    g_clear_object(&root);
    /* Without a local path the explorer cannot browse it; "/" is added apart */
    if (path == NULL || disk_volumes_find(out, path) != NULL) {
        g_free(path);
        return;
    }
    DiskVolume* v = g_new0(DiskVolume, 1);
    v->id = g_strdup(path);
    v->path = path;
    v->name = g_mount_get_name(mount);
    v->mount = g_object_ref(mount);
    v->volume = g_mount_get_volume(mount);
    if (v->volume != NULL) {
        v->device = g_volume_get_identifier(v->volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
    }
    v->can_unmount = g_mount_can_unmount(mount);
    v->can_eject = g_mount_can_eject(mount);
    GDrive* drive = g_mount_get_drive(mount);
    GIcon* icon = g_mount_get_icon(mount);
    v->kind = disk_volume_kind_guess(icon_names(icon), drive_removable(drive), path);
    g_clear_object(&icon);
    g_clear_object(&drive);
    g_ptr_array_add(out, v);
}

static void add_unmounted_volume(GPtrArray* out, GVolume* volume) {
    GMount* mount = g_volume_get_mount(volume);
    if (mount != NULL) {
        g_object_unref(mount);
        return;
    }
    if (!g_volume_can_mount(volume)) {
        return;
    }
    gchar* key = g_volume_get_uuid(volume);
    gchar* device = g_volume_get_identifier(volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
    gchar* name = g_volume_get_name(volume);
    if (key == NULL) {
        key = g_strdup(device != NULL ? device : name);
    }
    gchar* id = g_strconcat("volume:", key != NULL ? key : "", NULL);
    g_free(key);
    if (disk_volumes_find(out, id) != NULL) {
        g_free(id);
        g_free(device);
        g_free(name);
        return;
    }
    DiskVolume* v = g_new0(DiskVolume, 1);
    v->id = id;
    v->name = (name != NULL) ? name : g_strdup(device != NULL ? device : "");
    v->device = device;
    v->volume = g_object_ref(volume);
    v->can_mount = TRUE;
    v->can_eject = g_volume_can_eject(volume);
    GDrive* drive = g_volume_get_drive(volume);
    GIcon* icon = g_volume_get_icon(volume);
    v->kind = disk_volume_kind_guess(icon_names(icon), drive_removable(drive), NULL);
    g_clear_object(&icon);
    g_clear_object(&drive);
    g_ptr_array_add(out, v);
}

GPtrArray* disk_volumes_list(GVolumeMonitor* monitor) {
    GPtrArray* out = g_ptr_array_new_with_free_func(disk_volume_free);
#ifndef G_OS_WIN32
    DiskVolume* root = g_new0(DiskVolume, 1);
    root->id = g_strdup("/");
    root->path = g_strdup("/");
    root->name = g_strdup(_("File System"));
    root->kind = DISK_VOLUME_SYSTEM;
    g_ptr_array_add(out, root);
#endif

    GVolumeMonitor* own = NULL;
    if (monitor == NULL) {
        monitor = own = g_volume_monitor_get();
    }
    GList* mounts = g_volume_monitor_get_mounts(monitor);
    for (GList* l = mounts; l != NULL; l = l->next) {
        add_mount(out, G_MOUNT(l->data));
    }
    g_list_free_full(mounts, g_object_unref);
    GList* volumes = g_volume_monitor_get_volumes(monitor);
    for (GList* l = volumes; l != NULL; l = l->next) {
        add_unmounted_volume(out, G_VOLUME(l->data));
    }
    g_list_free_full(volumes, g_object_unref);
    g_clear_object(&own);

    g_ptr_array_sort(out, compare_ptrs);
    return out;
}

DiskVolume* disk_volumes_find(GPtrArray* volumes, const gchar* id) {
    for (guint i = 0; volumes != NULL && id != NULL && i < volumes->len; i++) {
        DiskVolume* v = g_ptr_array_index(volumes, i);
        if (g_strcmp0(v->id, id) == 0) {
            return v;
        }
    }
    return NULL;
}

const gchar* disk_volume_glyph(DiskVolumeKind kind) {
    switch (kind) {
    case DISK_VOLUME_REMOVABLE: return "drive-removable";
    case DISK_VOLUME_OPTICAL: return "drive-optical";
    case DISK_VOLUME_NETWORK: return "folder-remote";
    case DISK_VOLUME_SYSTEM:
    case DISK_VOLUME_FIXED:
    default: return "root";
    }
}

const gchar* disk_volume_kind_name(DiskVolumeKind kind) {
    switch (kind) {
    case DISK_VOLUME_SYSTEM: return _("System disk");
    case DISK_VOLUME_REMOVABLE: return _("Removable disk");
    case DISK_VOLUME_OPTICAL: return _("Optical disc");
    case DISK_VOLUME_NETWORK: return _("Network folder");
    case DISK_VOLUME_FIXED:
    default: return _("Disk");
    }
}
