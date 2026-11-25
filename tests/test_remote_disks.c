#include "core/remote_disks.h"

static RemoteDisk* at(GPtrArray* a, guint i) {
    return g_ptr_array_index(a, i);
}

static void test_findmnt(void) {
    const gchar* out =
        "TARGET=\"/\" SOURCE=\"/dev/nvme0n1p2\" FSTYPE=\"ext4\" SIZE=\"500000000000\" AVAIL=\"120000000000\"\n"
        "TARGET=\"/proc\" SOURCE=\"proc\" FSTYPE=\"proc\" SIZE=\"\" AVAIL=\"\"\n"
        "TARGET=\"/run/user/1000\" SOURCE=\"tmpfs\" FSTYPE=\"tmpfs\" SIZE=\"1\" AVAIL=\"1\"\n"
        "TARGET=\"/boot/efi\" SOURCE=\"/dev/nvme0n1p1\" FSTYPE=\"vfat\" SIZE=\"1\" AVAIL=\"1\"\n"
        "TARGET=\"/snap/core/1\" SOURCE=\"/dev/loop3\" FSTYPE=\"squashfs\" SIZE=\"1\" AVAIL=\"0\"\n"
        "TARGET=\"/mnt/my\\x20data\" SOURCE=\"/dev/sdb1\" FSTYPE=\"xfs\" SIZE=\"2000\" AVAIL=\"1000\"\n"
        "TARGET=\"/srv/share\" SOURCE=\"nas:/export\" FSTYPE=\"nfs4\" SIZE=\"10\" AVAIL=\"5\"\n"
        "TARGET=\"/run/media/ann/KEY\" SOURCE=\"/dev/sdc1\" FSTYPE=\"vfat\" SIZE=\"8\" AVAIL=\"4\"\n"
        "TARGET=\"/var/lib/docker/overlay2/x/merged\" SOURCE=\"overlay\" FSTYPE=\"overlay\" SIZE=\"1\" AVAIL=\"1\"\n"
        "TARGET=\"/tank\" SOURCE=\"tank\" FSTYPE=\"zfs\" SIZE=\"9\" AVAIL=\"3\"\n";
    GPtrArray* d = remote_disks_parse(out);
    g_assert_cmpuint(d->len, ==, 5);
    g_assert_cmpstr(at(d, 0)->target, ==, "/");
    g_assert_cmpint(at(d, 0)->kind, ==, DISK_VOLUME_SYSTEM);
    g_assert_cmpstr(at(d, 0)->name, ==, "File System");
    g_assert_cmpuint(at(d, 0)->avail, ==, G_GUINT64_CONSTANT(120000000000));
    g_assert_cmpstr(at(d, 1)->target, ==, "/mnt/my data"); /* fixed: "my data" before "tank" */
    g_assert_cmpstr(at(d, 1)->name, ==, "my data");
    g_assert_cmpstr(at(d, 1)->fstype, ==, "xfs");
    g_assert_cmpstr(at(d, 2)->target, ==, "/tank");
    g_assert_cmpstr(at(d, 3)->target, ==, "/run/media/ann/KEY");
    g_assert_cmpint(at(d, 3)->kind, ==, DISK_VOLUME_REMOVABLE);
    g_assert_cmpstr(at(d, 4)->target, ==, "/srv/share");
    g_assert_cmpint(at(d, 4)->kind, ==, DISK_VOLUME_NETWORK);
    g_ptr_array_unref(d);
}

static void test_df(void) {
    const gchar* out =
        "Filesystem     1024-blocks      Used Available Capacity  Mounted on\n"
        "/dev/disk3s1s1   482797652  10245184 289318780     4%    /\n"
        "devfs                  205       205         0   100%    /dev\n"
        "/dev/disk3s5     482797652 180000000 289318780    39%    /System/Volumes/Data\n"
        "map auto_home            0         0         0   100%    /System/Volumes/Data/home\n"
        "/dev/disk5s1      15000000   1000000  14000000     7%    /Volumes/USB KEY\n"
        "//ann@nas/films  100000000  50000000  50000000    50%    /Volumes/films\n";
    GPtrArray* d = remote_disks_parse(out);
    g_assert_cmpuint(d->len, ==, 3);
    g_assert_cmpstr(at(d, 0)->target, ==, "/");
    g_assert_cmpuint(at(d, 0)->size, ==, G_GUINT64_CONSTANT(482797652) * 1024);
    g_assert_cmpstr(at(d, 1)->target, ==, "/Volumes/USB KEY");
    g_assert_cmpint(at(d, 1)->kind, ==, DISK_VOLUME_REMOVABLE);
    g_assert_cmpstr(at(d, 2)->target, ==, "/Volumes/films");
    g_assert_cmpint(at(d, 2)->kind, ==, DISK_VOLUME_NETWORK);
    g_ptr_array_unref(d);
}

static void test_edge(void) {
    GPtrArray* d = remote_disks_parse(NULL);
    g_assert_cmpuint(d->len, ==, 0);
    g_ptr_array_unref(d);
    d = remote_disks_parse("");
    g_assert_cmpuint(d->len, ==, 0);
    g_ptr_array_unref(d);
    /* The same folder twice (mounted over): once */
    d = remote_disks_parse("TARGET=\"/data\" SOURCE=\"/dev/sdb1\" FSTYPE=\"ext4\" SIZE=\"1\" AVAIL=\"1\"\n"
                           "TARGET=\"/data\" SOURCE=\"/dev/sdc1\" FSTYPE=\"ext4\" SIZE=\"2\" AVAIL=\"2\"\n");
    g_assert_cmpuint(d->len, ==, 1);
    g_assert_cmpstr(at(d, 0)->source, ==, "/dev/sdb1");
    g_ptr_array_unref(d);
    g_assert_false(remote_disks_keep("relative", "/dev/sda", "ext4"));
    g_assert_true(remote_disks_keep("/run/media/a/b", "/dev/sdb1", "vfat"));
    g_assert_false(remote_disks_keep("/run/lock", "tmpfs", "tmpfs"));
    g_assert_cmpint(remote_disk_kind("/cd", "/dev/sr0", "iso9660"), ==, DISK_VOLUME_OPTICAL);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/remote_disks/findmnt", test_findmnt);
    g_test_add_func("/remote_disks/df", test_df);
    g_test_add_func("/remote_disks/edge", test_edge);
    return g_test_run();
}
