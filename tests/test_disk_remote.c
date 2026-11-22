/* Unit tests for disk_remote: find's records kept by folder */
#include "core/disk_remote.h"

#include <string.h>
#include <sys/stat.h>

static void test_listing(void) {
    DiskRemoteListing* l = disk_remote_listing_new();
    g_assert_null(disk_remote_listing_root(l));
    const gchar* rel = NULL;
    g_assert_true(disk_remote_listing_add(l, "d 4096 8 1795343863.2176861390 2049 12 5 .", &rel));
    g_assert_cmpstr(rel, ==, ".");
    g_assert_true(disk_remote_listing_add(l, "f 14 8 1795343863.2 2049 13 2 a b.txt", NULL));
    g_assert_true(disk_remote_listing_add(l, "d 4096 8 1795343800 2049 14 2 src", NULL));
    g_assert_true(disk_remote_listing_add(l, "l 7 0 1795343800 2049 15 1 src/link", NULL));
    g_assert_true(disk_remote_listing_add(l, "d 4096 8 1795343800 77 16 2 src/mnt", NULL));
    g_assert_false(disk_remote_listing_add(l, "f 14", NULL));
    g_assert_false(disk_remote_listing_add(l, "f x 8 1 2 3 4 n", NULL));
    g_assert_false(disk_remote_listing_add(l, "", NULL));

    const GStatBuf* root = disk_remote_listing_root(l);
    g_assert_nonnull(root);
    g_assert_true(S_ISDIR(root->st_mode));
    g_assert_cmpuint((guint64)root->st_dev, ==, 2049);

    GHashTable* top = disk_remote_listing_dir(l, "");
    g_assert_cmpuint(g_hash_table_size(top), ==, 2);
    const GStatBuf* f = g_hash_table_lookup(top, "a b.txt");
    g_assert_true(S_ISREG(f->st_mode));
    g_assert_cmpint((gint64)f->st_size, ==, 14);
    g_assert_cmpint((gint64)f->st_blocks, ==, 8);
    g_assert_cmpint((gint64)f->st_mtime, ==, 1795343863);
    g_assert_cmpuint((guint64)f->st_ino, ==, 13);
    g_assert_cmpuint((guint64)f->st_nlink, ==, 2);

    GHashTable* src = disk_remote_listing_dir(l, "src");
    g_assert_cmpuint(g_hash_table_size(src), ==, 2);
    g_assert_true(S_ISLNK(((const GStatBuf*)g_hash_table_lookup(src, "link"))->st_mode));
    g_assert_cmpuint((guint64)((const GStatBuf*)g_hash_table_lookup(src, "mnt"))->st_dev, ==, 77);
    g_assert_null(disk_remote_listing_dir(l, "src/mnt"));
    disk_remote_listing_free(l);
}

static void test_command(void) {
    gchar* c = disk_remote_command(TRUE);
    g_assert_nonnull(strstr(c, "-maxdepth 0 -printf '%y %s %b %T@ %D %i %n .\\0'"));
    g_assert_nonnull(strstr(c, "-mindepth 1 -xdev -printf '%y %s %b %T@ %D %i %n %P\\0'"));
    g_free(c);
    c = disk_remote_command(FALSE);
    g_assert_null(strstr(c, "-xdev"));
    g_free(c);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/disk_remote/listing", test_listing);
    g_test_add_func("/disk_remote/command", test_command);
    return g_test_run();
}
