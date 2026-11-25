#include "core/disk_volumes.h"

#include <string.h>

static DiskVolume* make(const gchar* name, const gchar* path, DiskVolumeKind kind) {
    DiskVolume* v = g_new0(DiskVolume, 1);
    v->id = g_strdup(path != NULL ? path : name);
    v->name = g_strdup(name);
    v->path = g_strdup(path);
    v->kind = kind;
    return v;
}

static void test_kind_guess(void) {
    const gchar* optical[] = {"drive-optical", "media-optical", NULL};
    const gchar* usb[] = {"drive-harddisk-usb", "drive-harddisk", NULL};
    const gchar* remote[] = {"folder-remote", NULL};
    const gchar* disk[] = {"drive-harddisk", NULL};
    g_assert_cmpint(disk_volume_kind_guess(disk, FALSE, "/"), ==, DISK_VOLUME_SYSTEM);
    g_assert_cmpint(disk_volume_kind_guess(optical, TRUE, "/media/cd"), ==, DISK_VOLUME_OPTICAL);
    g_assert_cmpint(disk_volume_kind_guess(usb, FALSE, "/media/key"), ==, DISK_VOLUME_REMOVABLE);
    g_assert_cmpint(disk_volume_kind_guess(disk, TRUE, "/media/key"), ==, DISK_VOLUME_REMOVABLE);
    g_assert_cmpint(disk_volume_kind_guess(remote, FALSE, "/run/user/1000/gvfs/smb"), ==, DISK_VOLUME_NETWORK);
    g_assert_cmpint(disk_volume_kind_guess(disk, FALSE, "/data"), ==, DISK_VOLUME_FIXED);
    g_assert_cmpint(disk_volume_kind_guess(NULL, FALSE, NULL), ==, DISK_VOLUME_FIXED);
}

static void test_compare_and_find(void) {
    GPtrArray* a = g_ptr_array_new_with_free_func(disk_volume_free);
    g_ptr_array_add(a, make("key", "/media/key", DISK_VOLUME_REMOVABLE));
    g_ptr_array_add(a, make("Data10", "/data10", DISK_VOLUME_FIXED));
    g_ptr_array_add(a, make("backup", NULL, DISK_VOLUME_FIXED)); /* not mounted */
    g_ptr_array_add(a, make("data2", "/data2", DISK_VOLUME_FIXED));
    g_ptr_array_add(a, make("File System", "/", DISK_VOLUME_SYSTEM));
    g_ptr_array_sort_values(a, (GCompareFunc)disk_volume_compare);
    const gchar* order[] = {"File System", "data2", "Data10", "backup", "key"};
    for (guint i = 0; i < G_N_ELEMENTS(order); i++) {
        g_assert_cmpstr(((DiskVolume*)g_ptr_array_index(a, i))->name, ==, order[i]);
    }
    g_assert_cmpstr(disk_volumes_find(a, "/media/key")->name, ==, "key");
    g_assert_null(disk_volumes_find(a, "/nowhere"));
    g_assert_null(disk_volumes_find(NULL, "/"));
    g_ptr_array_unref(a);
}

static void test_list(void) {
    GPtrArray* list = disk_volumes_list(NULL);
    g_assert_nonnull(list);
#ifndef G_OS_WIN32
    g_assert_cmpuint(list->len, >=, 1);
    DiskVolume* root = g_ptr_array_index(list, 0);
    g_assert_cmpstr(root->path, ==, "/");
    g_assert_cmpint(root->kind, ==, DISK_VOLUME_SYSTEM);
#endif
    for (guint i = 0; i < list->len; i++) {
        DiskVolume* v = g_ptr_array_index(list, i);
        g_assert_nonnull(v->id);
        g_assert_nonnull(v->name);
        g_assert_true((v->path != NULL) == (v->mount != NULL) || g_strcmp0(v->path, "/") == 0);
        g_assert_nonnull(disk_volume_glyph(v->kind));
        g_assert_nonnull(disk_volume_kind_name(v->kind));
    }
    g_ptr_array_unref(list);
}

int main(int argc, char** argv) {
    /* The in-process Unix monitor: no gvfs daemon needed */
    g_setenv("GIO_USE_VOLUME_MONITOR", "unix", TRUE);
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/disk_volumes/kind_guess", test_kind_guess);
    g_test_add_func("/disk_volumes/compare_and_find", test_compare_and_find);
    g_test_add_func("/disk_volumes/list", test_list);
    return g_test_run();
}
