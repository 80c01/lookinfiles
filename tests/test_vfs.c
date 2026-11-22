/* Unit tests for src/core/vfs.c: the "ssh" run is this test program again,
 * which then serves SFTP on its standard input and output
 * (sftp_test_server.h) over the files of a temporary folder; the master
 * connection's socket is a plain file, so ssh_session.c takes it as open. */
#include "core/vfs.h"
#include "explorer/dir_scan.h"
#include "preview/preview_source.h"
#include "core/vfs_cache.h"
#include "core/vfs_watch.h"
#include "sftp_test_server.h"

#include <glib/gstdio.h>
#include <string.h>
#include <unistd.h>

static gchar* root;
static gchar* program;

/* Called as ssh: "... -s <destination> sftp" */
static gboolean is_ssh_call(int argc, char** argv) {
    return argc >= 3 && strcmp(argv[argc - 1], "sftp") == 0 && strcmp(argv[argc - 3], "-s") == 0;
}

static int serve_stdio(void) {
    Server s = { .fd = 0, .out_fd = 1 };
    server_main(&s);
    return 0;
}

static const SshTarget TARGET = { .host = "vfs-test-host" };

/* The master connection taken as open: its socket path exists */
static gchar* fake_master(void) {
    gchar* sock = ssh_session_control_path(&TARGET);
    if (sock != NULL) g_assert_true(g_file_set_contents(sock, "", 0, NULL));
    return sock;
}

static void test_remote_path(void) {
    struct {
        const gchar *root, *rel, *out;
    } cases[] = {
        { "~", "", "." },           { "~", "a/b", "a/b" },   { "~/", "a", "a" },
        { "~/proj", "", "proj" },   { "~/proj", "x", "proj/x" }, { "/srv/", "a", "/srv/a" },
        { "/", "", "/" },           { "/", "etc", "/etc" },  { "", "a", "a" },
        { NULL, "", "." },
    };
    for (guint i = 0; i < G_N_ELEMENTS(cases); i++) {
        gchar* out = vfs_remote_path(cases[i].root, cases[i].rel);
        g_assert_cmpstr(out, ==, cases[i].out);
        g_free(out);
    }
}

static gint by_name(gconstpointer a, gconstpointer b) {
    return strcmp((*(VfsEntry* const*)a)->name, (*(VfsEntry* const*)b)->name);
}

static void test_list(void) {
    gchar* sock = fake_master();
    if (sock == NULL) {
        g_test_skip("no multiplexing here");
        return;
    }
    gchar* sub = g_build_filename(root, "sub", NULL);
    g_assert_cmpint(g_mkdir(sub, 0755), ==, 0);
    gchar* file = g_build_filename(root, "file.txt", NULL);
    g_assert_true(g_file_set_contents(file, "12345", 5, NULL));
    gchar* link = g_build_filename(root, "to-sub", NULL);
    g_assert_cmpint(symlink(sub, link), ==, 0);
    gchar* dangling = g_build_filename(root, "dangling", NULL);
    g_assert_cmpint(symlink("/no/such/target", dangling), ==, 0);

    const gchar* mount = "/virtual/machine";
    vfs_mount(mount, &TARGET, root);
    g_assert_true(vfs_is_remote(mount));
    g_assert_true(vfs_is_remote("/virtual/machine/sub"));
    g_assert_false(vfs_is_remote("/virtual/machine2"));
    g_assert_false(vfs_is_remote("/virtual"));

    GError* err = NULL;
    GPtrArray* list = vfs_list("/virtual/machine/", NULL, &err);
    g_assert_no_error(err);
    g_assert_nonnull(list);
    g_assert_cmpuint(list->len, ==, 4);
    g_ptr_array_sort(list, by_name);
    VfsEntry* e = list->pdata[0];
    g_assert_cmpstr(e->name, ==, "dangling");
    g_assert_true(e->is_link);
    g_assert_false(e->is_dir);
    e = list->pdata[1];
    g_assert_cmpstr(e->name, ==, "file.txt");
    g_assert_false(e->is_dir);
    g_assert_cmpint(e->size, ==, 5);
    g_assert_cmpint(e->mtime, >, 0);
    e = list->pdata[2];
    g_assert_cmpstr(e->name, ==, "sub");
    g_assert_true(e->is_dir);
    e = list->pdata[3];
    g_assert_cmpstr(e->name, ==, "to-sub");
    g_assert_true(e->is_link);
    g_assert_true(e->is_dir); /* the target's */
    g_ptr_array_unref(list);

    list = vfs_list("/virtual/machine/sub", NULL, &err);
    g_assert_no_error(err);
    g_assert_cmpuint(list->len, ==, 0);
    g_ptr_array_unref(list);

    g_assert_true(vfs_file_test("/virtual/machine/sub", G_FILE_TEST_IS_DIR));
    g_assert_true(vfs_file_test("/virtual/machine/to-sub", G_FILE_TEST_IS_DIR)); /* followed */
    g_assert_false(vfs_file_test("/virtual/machine/file.txt", G_FILE_TEST_IS_DIR));
    g_assert_true(vfs_file_test("/virtual/machine/file.txt", G_FILE_TEST_IS_REGULAR));
    g_assert_true(vfs_file_test("/virtual/machine/file.txt", G_FILE_TEST_EXISTS));
    g_assert_false(vfs_file_test("/virtual/machine/nope", G_FILE_TEST_EXISTS));
    g_assert_true(vfs_file_test(root, G_FILE_TEST_IS_DIR)); /* a local path */

    VfsEntry info;
    g_assert_true(vfs_stat("/virtual/machine/to-sub", &info, NULL, &err));
    g_assert_true(info.is_dir); /* followed */
    g_assert_true(vfs_stat("/virtual/machine/file.txt", &info, NULL, &err));
    g_assert_false(info.is_dir);
    g_assert_cmpint(info.size, ==, 5);
    g_assert_false(vfs_stat("/virtual/machine/nope", &info, NULL, &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_assert_false(vfs_error_is_connection(err));
    g_clear_error(&err);

    guint64 size = 0;
    GBytes* bytes = vfs_read("/virtual/machine/file.txt", G_MAXUINT64, &size, NULL, &err);
    g_assert_no_error(err);
    g_assert_cmpmem(g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes), "12345", 5);
    g_assert_cmpuint(size, ==, 5);
    g_bytes_unref(bytes);
    bytes = vfs_read("/virtual/machine/file.txt", 2, &size, NULL, &err);
    g_assert_cmpmem(g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes), "12", 2);
    g_assert_cmpuint(size, ==, 5);
    g_bytes_unref(bytes);
    g_assert_null(vfs_read("/virtual/machine/sub", 10, NULL, NULL, &err)); /* a folder */
    g_assert_nonnull(err);
    g_clear_error(&err);

    g_assert_null(vfs_list("/virtual/machine/missing", NULL, &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_clear_error(&err);
    g_assert_null(vfs_list("/elsewhere", NULL, &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
    g_clear_error(&err);

    /* Without the master connection: at once, no ssh run */
    g_assert_cmpint(g_unlink(sock), ==, 0);
    vfs_unmount(mount);
    vfs_mount(mount, &TARGET, root); /* a new mount: a new connection */
    gint64 start = g_get_monotonic_time();
    g_assert_null(vfs_list(mount, NULL, &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED);
    g_assert_true(vfs_error_is_connection(err));
    g_clear_error(&err);
    g_assert_cmpint(g_get_monotonic_time() - start, <, 5 * G_USEC_PER_SEC);

    vfs_unmount(mount);
    g_assert_false(vfs_is_remote(mount));
    g_free(dangling);
    g_free(link);
    g_free(file);
    g_free(sub);
    g_free(sock);
}

/* From several threads at once, as the explorer's workers */
static gpointer list_in_thread(gpointer data) {
    (void)data;
    guint ok = 0;
    for (gint i = 0; i < 5; i++) {
        GPtrArray* list = vfs_list("/virtual/m2", NULL, NULL);
        if (list != NULL && list->len == 3) ok++;
        if (list != NULL) g_ptr_array_unref(list);
    }
    return GUINT_TO_POINTER(ok);
}

static void test_threads(void) {
    gchar* sock = fake_master();
    if (sock == NULL) {
        g_test_skip("no multiplexing here");
        return;
    }
    gchar* dir = g_build_filename(root, "m2", NULL);
    g_assert_cmpint(g_mkdir(dir, 0755), ==, 0);
    for (gint i = 0; i < 3; i++) {
        gchar* f = g_strdup_printf("%s/f%d", dir, i);
        g_assert_true(g_file_set_contents(f, "", 0, NULL));
        g_free(f);
    }
    vfs_mount("/virtual/m2", &TARGET, dir);
    GThread* threads[4];
    for (guint i = 0; i < G_N_ELEMENTS(threads); i++) threads[i] = g_thread_new("lister", list_in_thread, NULL);
    for (guint i = 0; i < G_N_ELEMENTS(threads); i++) {
        g_assert_cmpuint(GPOINTER_TO_UINT(g_thread_join(threads[i])), ==, 5);
    }
    vfs_unmount("/virtual/m2");
    g_unlink(sock);
    g_free(sock);
    g_free(dir);
}

/* The explorer's listing: over SFTP while the master is open, else from
 * the mount (here a local folder with other files) */
static void test_dir_scan(void) {
    gchar* sock = fake_master();
    if (sock == NULL) {
        g_test_skip("no multiplexing here");
        return;
    }
    gchar* remote = g_build_filename(root, "ds-remote", NULL);
    gchar* mount = g_build_filename(root, "ds-mount", NULL);
    g_assert_cmpint(g_mkdir(remote, 0755), ==, 0);
    g_assert_cmpint(g_mkdir(mount, 0755), ==, 0);
    const gchar* names[] = { "b.txt", "a.txt", ".hidden" };
    for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
        gchar* f = g_build_filename(remote, names[i], NULL);
        g_assert_true(g_file_set_contents(f, "x", 1, NULL));
        g_free(f);
    }
    gchar* d = g_build_filename(remote, "zdir", NULL);
    g_assert_cmpint(g_mkdir(d, 0755), ==, 0);
    gchar* local_only = g_build_filename(mount, "on-the-mount", NULL);
    g_assert_true(g_file_set_contents(local_only, "", 0, NULL));
    vfs_mount(mount, &TARGET, remote);

    DirScanNode* node = dir_scan_read(mount, FALSE, DIR_SCAN_SORT_NAME, NULL, NULL);
    g_assert_false(node->unreadable);
    g_assert_cmpuint(node->entries->len, ==, 3); /* no .hidden */
    g_assert_cmpstr(g_array_index(node->entries, DirScanEntry, 0).name, ==, "zdir"); /* folders first */
    g_assert_true(g_array_index(node->entries, DirScanEntry, 0).is_dir);
    g_assert_cmpstr(g_array_index(node->entries, DirScanEntry, 1).name, ==, "a.txt");
    g_assert_cmpint(g_array_index(node->entries, DirScanEntry, 1).size, ==, 1);
    dir_scan_node_free(node);

    gchar* missing = g_build_filename(mount, "missing", NULL);
    node = dir_scan_read(missing, FALSE, DIR_SCAN_SORT_NAME, NULL, NULL);
    g_assert_true(node->unreadable);
    dir_scan_node_free(node);

    g_unlink(sock); /* no master (and a new connection): the mount is read */
    vfs_unmount(mount);
    vfs_mount(mount, &TARGET, remote);
    node = dir_scan_read(mount, FALSE, DIR_SCAN_SORT_NAME, NULL, NULL);
    g_assert_cmpuint(node->entries->len, ==, 1);
    g_assert_cmpstr(g_array_index(node->entries, DirScanEntry, 0).name, ==, "on-the-mount");
    dir_scan_node_free(node);

    vfs_unmount(mount);
    g_free(missing);
    g_free(local_only);
    g_free(d);
    g_free(mount);
    g_free(remote);
    g_free(sock);
}

/* The preview's reads: over SFTP, the mount when no session can serve */
static void test_preview_source(void) {
    gchar* sock = fake_master();
    if (sock == NULL) {
        g_test_skip("no multiplexing here");
        return;
    }
    gchar* remote = g_build_filename(root, "ps-remote", NULL);
    gchar* mount = g_build_filename(root, "ps-mount", NULL);
    g_assert_cmpint(g_mkdir(remote, 0755), ==, 0);
    g_assert_cmpint(g_mkdir(mount, 0755), ==, 0);
    gchar* rf = g_build_filename(remote, "f.txt", NULL);
    gchar* mf = g_build_filename(mount, "f.txt", NULL);
    g_assert_true(g_file_set_contents(rf, "remote text", -1, NULL));
    g_assert_true(g_file_set_contents(mf, "mount", -1, NULL));
    vfs_mount(mount, &TARGET, remote);

    FileStamp st;
    g_assert_true(preview_source_stamp(mf, &st));
    g_assert_cmpint(st.size, ==, 11); /* the remote file's */
    PreviewSource* src = preview_source_open(mf, FALSE, NULL);
    g_assert_nonnull(src);
    gchar buf[64];
    gsize n = preview_source_read(src, buf, 6);
    n += preview_source_read(src, buf + n, sizeof buf - n);
    g_assert_cmpuint(preview_source_read(src, buf, sizeof buf), ==, 0);
    preview_source_close(src);
    g_assert_cmpmem(buf, n, "remote text", 11);
    gchar* missing = g_build_filename(mount, "nope", NULL);
    g_assert_false(preview_source_stamp(missing, &st));
    g_assert_null(preview_source_open(missing, TRUE, NULL));

    g_unlink(sock); /* no session: the mount */
    vfs_unmount(mount);
    vfs_mount(mount, &TARGET, remote);
    g_assert_true(preview_source_stamp(mf, &st));
    g_assert_cmpint(st.size, ==, 5);
    src = preview_source_open(mf, TRUE, NULL);
    n = preview_source_read(src, buf, sizeof buf);
    preview_source_close(src);
    g_assert_cmpmem(buf, n, "mount", 5);

    vfs_unmount(mount);
    g_free(missing);
    g_free(mf);
    g_free(rf);
    g_free(mount);
    g_free(remote);
    g_free(sock);
}

static void test_write_ops(void) {
    gchar* sock = fake_master();
    if (sock == NULL) {
        g_test_skip("no multiplexing here");
        return;
    }
    gchar* remote = g_build_filename(root, "w-remote", NULL);
    g_assert_cmpint(g_mkdir(remote, 0755), ==, 0);
    const gchar* m = "/virtual/w";
    vfs_mount(m, &TARGET, remote);
    GError* err = NULL;

    GBytes* text = g_bytes_new_static("saved\n", 6);
    g_assert_true(vfs_write("/virtual/w/a.txt", text, 0644, &err));
    g_assert_no_error(err);
    gchar* on_disk = NULL;
    gchar* a = g_build_filename(remote, "a.txt", NULL);
    g_assert_true(g_file_get_contents(a, &on_disk, NULL, NULL));
    g_assert_cmpstr(on_disk, ==, "saved\n");
    g_free(on_disk);

    g_assert_true(vfs_mkdir_with_parents("/virtual/w/x/y/z", 0755, &err));
    g_assert_no_error(err);
    gchar* z = g_build_filename(remote, "x", "y", "z", NULL);
    g_assert_true(g_file_test(z, G_FILE_TEST_IS_DIR));
    g_assert_true(vfs_mkdir_with_parents("/virtual/w/x/y", 0755, &err)); /* already there */
    g_assert_false(vfs_mkdir_with_parents("/virtual/w/a.txt", 0755, &err)); /* a file */
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_EXISTS);
    g_clear_error(&err);
    g_assert_true(vfs_write("/virtual/w/x/y/f", text, 0644, &err));

    g_assert_true(vfs_rename("/virtual/w/a.txt", "/virtual/w/b.txt", FALSE, &err));
    g_assert_no_error(err);
    g_assert_false(g_file_test(a, G_FILE_TEST_EXISTS));
    g_assert_false(vfs_rename("/virtual/w/b.txt", "/elsewhere/b.txt", FALSE, &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
    g_clear_error(&err);

    g_assert_true(vfs_remove("/virtual/w/b.txt", &err));
    gchar* x = g_build_filename(remote, "x", NULL);
    gchar* link = g_build_filename(remote, "x", "y", "to-remote", NULL);
    g_assert_cmpint(symlink(remote, link), ==, 0); /* removed, not followed */
    g_assert_true(vfs_delete_tree("/virtual/w/x", &err));
    g_assert_no_error(err);
    g_assert_false(g_file_test(x, G_FILE_TEST_EXISTS));
    g_assert_true(g_file_test(remote, G_FILE_TEST_IS_DIR));
    g_assert_false(vfs_delete_tree("/virtual/w/none", &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_clear_error(&err);
    g_assert_false(vfs_delete_tree("/virtual/w/", &err)); /* never the machine's folder */
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
    g_clear_error(&err);
    g_assert_true(g_file_test(remote, G_FILE_TEST_IS_DIR));

    vfs_unmount(m);
    g_bytes_unref(text);
    g_free(link);
    g_free(x);
    g_free(z);
    g_free(a);
    g_free(remote);
    g_free(sock);
}

static void test_cache(void) {
    gchar* sock = fake_master();
    if (sock == NULL) {
        g_test_skip("no multiplexing here");
        return;
    }
    gchar* remote = g_build_filename(root, "c-remote", NULL);
    g_assert_cmpint(g_mkdir(remote, 0755), ==, 0);
    gchar* rf = g_build_filename(remote, "pic.png", NULL);
    g_assert_true(g_file_set_contents(rf, "first", -1, NULL));
    vfs_mount("/virtual/c", &TARGET, remote);
    GError* err = NULL;

    gchar* local = vfs_cache_copy("/virtual/c/pic.png", 1000, NULL, &err);
    g_assert_no_error(err);
    g_assert_true(g_str_has_suffix(local, "/pic.png")); /* its own name: readers go by the extension */
    gchar* text = NULL;
    g_assert_true(g_file_get_contents(local, &text, NULL, NULL));
    g_assert_cmpstr(text, ==, "first");
    g_free(text);

    /* The same file: the copy is used again (marked to see it) */
    g_assert_true(g_file_set_contents(local, "kept!", -1, NULL));
    gchar* again = vfs_cache_copy("/virtual/c/pic.png", 1000, NULL, &err);
    g_assert_cmpstr(again, ==, local);
    g_assert_true(g_file_get_contents(again, &text, NULL, NULL));
    g_assert_cmpstr(text, ==, "kept!");
    g_free(text);
    g_free(again);

    /* Changed: copied again */
    g_assert_true(g_file_set_contents(rf, "second version", -1, NULL));
    again = vfs_cache_copy("/virtual/c/pic.png", 1000, NULL, &err);
    g_assert_true(g_file_get_contents(again, &text, NULL, NULL));
    g_assert_cmpstr(text, ==, "second version");
    g_free(text);
    g_free(again);

    g_assert_null(vfs_cache_copy("/virtual/c/pic.png", 3, NULL, &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_NO_SPACE);
    g_clear_error(&err);
    g_assert_null(vfs_cache_copy("/virtual/c/none", 1000, NULL, &err));
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
    g_clear_error(&err);

    /* Preview: a copy for a remote file, nothing for a local one */
    gchar* via = preview_source_local("/virtual/c/pic.png", FALSE, NULL);
    g_assert_cmpstr(via, ==, local);
    g_free(via);
    g_assert_null(preview_source_local(rf, FALSE, NULL));

    /* A large file: copied a piece at a time */
    gchar* rbig = g_build_filename(remote, "movie.mkv", NULL);
    gsize big_len = 8 * 1024 * 1024 * 2 + 12345; /* more than two pieces */
    gchar* big = g_malloc(big_len);
    for (gsize i = 0; i < big_len; i++) big[i] = (gchar)(i * 31 + i / 4099);
    g_assert_true(g_file_set_contents(rbig, big, (gssize)big_len, NULL));
    gchar* lbig = vfs_cache_copy("/virtual/c/movie.mkv", G_MAXUINT64, NULL, &err);
    g_assert_no_error(err);
    gchar* back = NULL;
    gsize back_len = 0;
    g_assert_true(g_file_get_contents(lbig, &back, &back_len, NULL));
    g_assert_cmpmem(back, back_len, big, big_len);
    g_free(back);
    g_free(lbig);
    g_free(big);
    g_unlink(rbig);
    g_free(rbig);

    /* Trim: the oldest go first */
    gchar* rf2 = g_build_filename(remote, "b.bin", NULL);
    g_assert_true(g_file_set_contents(rf2, "0123456789", -1, NULL));
    gchar* local2 = vfs_cache_copy("/virtual/c/b.bin", 1000, NULL, &err);
    g_assert_nonnull(local2);
    g_assert_cmpuint(vfs_cache_trim(G_MAXUINT64), ==, 0);
    g_assert_cmpuint(vfs_cache_trim(0), ==, 3); /* pic.png, movie.mkv, b.bin */
    g_assert_false(g_file_test(local, G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(local2, G_FILE_TEST_EXISTS));

    vfs_unmount("/virtual/c");
    g_free(local2);
    g_free(rf2);
    g_free(local);
    g_free(rf);
    g_free(remote);
    g_free(sock);
}

static guint watch_calls;

static void on_watch_changed(gpointer data) {
    (void)data;
    watch_calls++;
}

/* Run the main loop until *calls reaches n (10 s at most) */
static void wait_calls(guint* calls, guint n) {
    gint64 end = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
    while (*calls < n && g_get_monotonic_time() < end) g_main_context_iteration(NULL, FALSE);
}

static void test_watch(void) {
    gchar* sock = fake_master();
    if (sock == NULL) {
        g_test_skip("no multiplexing here");
        return;
    }
    gchar* remote = g_build_filename(root, "wa-remote", NULL);
    g_assert_cmpint(g_mkdir(remote, 0755), ==, 0);
    gchar* rf = g_build_filename(remote, "f", NULL);
    g_assert_true(g_file_set_contents(rf, "one", -1, NULL));
    vfs_mount("/virtual/wa", &TARGET, remote);

    gboolean exists = FALSE;
    gint64 size = 0;
    gint64 mtime = 0;
    g_assert_true(vfs_watch_stamp("/virtual/wa/f", &exists, &size, &mtime));
    g_assert_true(exists);
    g_assert_cmpint(size, ==, 3);

    watch_calls = 0;
    VfsWatch* w = vfs_watch_new("/virtual/wa/f", on_watch_changed, NULL);
    for (gint i = 0; i < 50; i++) g_main_context_iteration(NULL, FALSE); /* the first reading */
    g_usleep(300 * 1000);
    for (gint i = 0; i < 50; i++) g_main_context_iteration(NULL, FALSE);
    g_assert_cmpuint(watch_calls, ==, 0); /* unchanged */

    g_assert_true(g_file_set_contents(rf, "three", -1, NULL));
    vfs_watch_poll(w);
    wait_calls(&watch_calls, 1);
    g_assert_cmpuint(watch_calls, ==, 1);
    g_assert_true(vfs_watch_stamp("/virtual/wa/f", &exists, &size, NULL)); /* the watch's reading */
    g_assert_cmpint(size, ==, 5);

    g_assert_cmpint(g_unlink(rf), ==, 0); /* removed: a change too */
    vfs_watch_poll(w);
    wait_calls(&watch_calls, 2);
    g_assert_cmpuint(watch_calls, ==, 2);
    g_assert_true(vfs_watch_stamp("/virtual/wa/f", &exists, NULL, NULL));
    g_assert_false(exists);

    /* Without a watch: the last reading until forgotten */
    vfs_watch_free(w);
    g_assert_true(g_file_set_contents(rf, "back again", -1, NULL));
    g_assert_true(vfs_watch_stamp("/virtual/wa/f", &exists, NULL, NULL));
    g_assert_false(exists);
    vfs_watch_forget("/virtual/wa/f");
    g_assert_true(vfs_watch_stamp("/virtual/wa/f", &exists, &size, NULL));
    g_assert_true(exists);
    g_assert_cmpint(size, ==, 10);
    g_assert_cmpuint(watch_calls, ==, 2); /* freed: no call */

    vfs_unmount("/virtual/wa");
    g_free(rf);
    g_free(remote);
    g_free(sock);
}

int main(int argc, char** argv) {
    if (is_ssh_call(argc, argv)) return serve_stdio();
    g_test_init(&argc, &argv, NULL);
    program = g_canonicalize_filename(argv[0], NULL);
    ssh_session_set_program(program);
    gchar* runtime = g_dir_make_tmp("vfs-run-XXXXXX", NULL);
    g_setenv("XDG_RUNTIME_DIR", runtime, TRUE); /* the sockets' folder */
    g_setenv("XDG_CACHE_HOME", runtime, TRUE);   /* the copies' */
    root = g_dir_make_tmp("vfs-XXXXXX", NULL);
    g_test_add_func("/vfs/remote_path", test_remote_path);
    g_test_add_func("/vfs/list", test_list);
    g_test_add_func("/vfs/threads", test_threads);
    g_test_add_func("/vfs/dir_scan", test_dir_scan);
    g_test_add_func("/vfs/preview_source", test_preview_source);
    g_test_add_func("/vfs/write_ops", test_write_ops);
    g_test_add_func("/vfs/cache", test_cache);
    g_test_add_func("/vfs/watch", test_watch);
    int status = g_test_run();
    vfs_shutdown();
    const gchar* rm_argv[] = { "rm", "-rf", root, runtime, NULL };
    g_spawn_sync(NULL, (gchar**)rm_argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, NULL);
    g_free(root);
    g_free(runtime);
    g_free(program);
    return status;
}
