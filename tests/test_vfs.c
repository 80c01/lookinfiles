/* Unit tests for src/core/vfs.c: the "ssh" run is this test program again,
 * which then serves SFTP on its standard input and output
 * (sftp_test_server.h) over the files of a temporary folder; the master
 * connection's socket is a plain file, so ssh_session.c takes it as open. */
#include "core/vfs.h"
#include "explorer/dir_scan.h"
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

int main(int argc, char** argv) {
    if (is_ssh_call(argc, argv)) return serve_stdio();
    g_test_init(&argc, &argv, NULL);
    program = g_canonicalize_filename(argv[0], NULL);
    ssh_session_set_program(program);
    gchar* runtime = g_dir_make_tmp("vfs-run-XXXXXX", NULL);
    g_setenv("XDG_RUNTIME_DIR", runtime, TRUE); /* the sockets' folder */
    root = g_dir_make_tmp("vfs-XXXXXX", NULL);
    g_test_add_func("/vfs/remote_path", test_remote_path);
    g_test_add_func("/vfs/list", test_list);
    g_test_add_func("/vfs/threads", test_threads);
    g_test_add_func("/vfs/dir_scan", test_dir_scan);
    int status = g_test_run();
    vfs_shutdown();
    const gchar* rm_argv[] = { "rm", "-rf", root, runtime, NULL };
    g_spawn_sync(NULL, (gchar**)rm_argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, NULL);
    g_free(root);
    g_free(runtime);
    g_free(program);
    return status;
}
