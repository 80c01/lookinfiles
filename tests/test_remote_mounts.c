/* Unit tests for src/explorer/remote_mounts.c: mount points in the runtime
 * directory, /proc/self/mounts parsing, owner files. $XDG_RUNTIME_DIR points
 * to a temporary directory; nothing is mounted. */
#include "explorer/remote_mounts.h"

#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

static gchar* tmp_run;

static void test_dir_in_runtime(void) {
    gchar* dir = remote_mounts_dir("rem_1");
    gchar* want = g_build_filename(tmp_run, "lookinfiles", "remotes", "rem_1", NULL);
    g_assert_cmpstr(dir, ==, want);
    g_assert_true(g_file_test(dir, G_FILE_TEST_IS_DIR));
    g_assert_false(remote_mounts_is_mounted(dir));
    g_assert_null(remote_mounts_dir(""));
    g_assert_null(remote_mounts_dir(NULL));
    g_free(want);
    g_free(dir);
}

static void test_parse(void) {
    const gchar* text =
        "proc /proc proc rw,nosuid 0 0\n"
        "me@host: /run/user/1000/lookinfiles/remotes/rem_1 fuse.sshfs rw,user_id=1000 0 0\n"
        "me@box:/srv /run/user/1000/lookinfiles/remotes/ssh_my\\040box fuse.sshfs rw 0 0\n"
        "tmpfs /run/user/1000/lookinfiles/remotes/rem_2 tmpfs rw 0 0\n"
        "me@other: /home/me/mnt fuse.sshfs rw 0 0\n"
        "me@x: /run/user/1000/lookinfiles/remotes fuse.sshfs rw 0 0\n"
        "me@y: /run/user/1000/lookinfiles/remotes2/a fuse.sshfs rw 0 0\n";
    GPtrArray* found = remote_mounts_parse(text, "/run/user/1000/lookinfiles/remotes");
    g_assert_cmpuint(found->len, ==, 2);
    g_assert_cmpstr(g_ptr_array_index(found, 0), ==, "/run/user/1000/lookinfiles/remotes/rem_1");
    g_assert_cmpstr(g_ptr_array_index(found, 1), ==, "/run/user/1000/lookinfiles/remotes/ssh_my box");
    g_ptr_array_unref(found);

    found = remote_mounts_parse(text, "/run/user/1000/lookinfiles/remotes/");
    g_assert_cmpuint(found->len, ==, 2);
    g_ptr_array_unref(found);

    found = remote_mounts_parse(NULL, "/x");
    g_assert_cmpuint(found->len, ==, 0);
    g_ptr_array_unref(found);
}

static void test_owner_and_unmount(void) {
    gchar* dir = remote_mounts_dir("rem_owner");
    g_assert_cmpint(remote_mounts_owner(dir), ==, 0);
    remote_mounts_claim(dir);
    g_assert_cmpint(remote_mounts_owner(dir), ==, (gint)getpid());

    /* Not mounted: the owner file and the empty mount point go */
    g_assert_true(remote_mounts_unmount(dir));
    g_assert_cmpint(remote_mounts_owner(dir), ==, 0);
    g_assert_false(g_file_test(dir, G_FILE_TEST_EXISTS));
    g_free(dir);

    /* A folder with files (not a mount) is never emptied */
    dir = remote_mounts_dir("rem_full");
    gchar* file = g_build_filename(dir, "keep.txt", NULL);
    g_assert_true(g_file_set_contents(file, "data", -1, NULL));
    g_assert_true(remote_mounts_unmount(dir));
    g_assert_true(g_file_test(file, G_FILE_TEST_IS_REGULAR));
    g_remove(file);
    g_rmdir(dir);
    g_free(file);
    g_free(dir);
}

static void remove_tree(const gchar* path) {
    GDir* d = g_dir_open(path, 0, NULL);
    if (d != NULL) {
        const gchar* name;
        while ((name = g_dir_read_name(d)) != NULL) {
            gchar* child = g_build_filename(path, name, NULL);
            remove_tree(child);
            g_free(child);
        }
        g_dir_close(d);
        g_rmdir(path);
    } else {
        g_remove(path);
    }
}

int main(int argc, char** argv) {
    tmp_run = g_dir_make_tmp("remote_mounts_test_XXXXXX", NULL);
    g_assert_nonnull(tmp_run);
    g_setenv("XDG_RUNTIME_DIR", tmp_run, TRUE);

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/remote_mounts/dir_in_runtime", test_dir_in_runtime);
    g_test_add_func("/remote_mounts/parse", test_parse);
    g_test_add_func("/remote_mounts/owner_and_unmount", test_owner_and_unmount);
    gint rc = g_test_run();

    remove_tree(tmp_run);
    g_free(tmp_run);
    return rc;
}
