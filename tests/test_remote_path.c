/* Unit tests for src/core/remote_path.c: paths of a mounted remote machine
 * shown as "<machine>:<remote path>" and read back. */
#include "core/remote_path.h"
#include "explorer/remote_store.h"

#include <glib/gstdio.h>

#define MOUNT "/run/user/1000/lookinfiles/remotes/rem_1"

static void check_display(const gchar* path, const gchar* root, const gchar* expected) {
    gchar* shown = remote_path_map_to_display(path, MOUNT, "srv", root);
    g_assert_cmpstr(shown, ==, expected);
    g_free(shown);
}

static void test_to_display(void) {
    check_display(MOUNT, NULL, "srv:~");
    check_display(MOUNT "/", "~", "srv:~");
    check_display(MOUNT "/projects/a.txt", NULL, "srv:~/projects/a.txt");
    check_display(MOUNT "/projects", "/srv/data/", "srv:/srv/data/projects");
    check_display(MOUNT "/etc/hosts", "/", "srv:/etc/hosts");
    check_display(MOUNT, "/", "srv:/");
    /* Outside the mount, or a sibling folder sharing its prefix */
    check_display("/home/me/a.txt", NULL, NULL);
    check_display(MOUNT "2/a.txt", NULL, NULL);
    gchar* shown = remote_path_map_to_display(MOUNT "/x", MOUNT "/", "srv", NULL);
    g_assert_cmpstr(shown, ==, "srv:~/x");
    g_free(shown);
}

/* local: expected local path, NULL when outside the mount; named: whether
 * the text names the machine */
static void check_parse(const gchar* text, const gchar* root, gboolean named, const gchar* local) {
    gchar* out = (gchar*)"unset";
    g_assert_cmpint(remote_path_map_from_display(text, MOUNT, "srv", root, &out), ==, named);
    g_assert_cmpstr(out, ==, local);
    g_free(out);
}

static void test_from_display(void) {
    check_parse("srv:~", NULL, TRUE, MOUNT);
    check_parse("srv:", NULL, TRUE, MOUNT);
    check_parse("  srv:~/projects/a.txt ", NULL, TRUE, MOUNT "/projects/a.txt");
    check_parse("srv:projects", NULL, TRUE, MOUNT "/projects");
    check_parse("srv:/srv/data/x", "/srv/data", TRUE, MOUNT "/x");
    check_parse("srv:/srv/data", "/srv/data/", TRUE, MOUNT);
    check_parse("srv:/etc/hosts", "/", TRUE, MOUNT "/etc/hosts");
    check_parse("srv:~/a/../b", NULL, TRUE, MOUNT "/b");
    /* Named, but out of the mounted folder */
    check_parse("srv:/etc", NULL, TRUE, NULL);
    check_parse("srv:/srv/database", "/srv/data", TRUE, NULL);
    check_parse("srv:~/../other", NULL, TRUE, NULL);
    /* Not this machine */
    check_parse("/home/me", NULL, FALSE, NULL);
    check_parse("srv2:~", NULL, FALSE, NULL);
    check_parse("sr:~", NULL, FALSE, NULL);
}

static void test_connected_machines(void) {
    AppData* app = g_new0(AppData, 1);
    RemoteMachine* off = g_new0(RemoteMachine, 1);
    off->id = g_strdup("rem_0");
    off->name = g_strdup("srv");
    off->host = g_strdup("off.example");
    off->mount_point = g_strdup("/mnt/off");
    remote_machine_add(app, off);
    RemoteMachine* on = g_new0(RemoteMachine, 1);
    on->id = g_strdup("rem_1");
    on->host = g_strdup("srv.example");
    on->remote_path = g_strdup("/data");
    on->mount_point = g_strdup(MOUNT);
    on->is_connected = TRUE;
    remote_machine_add(app, on);

    gchar* shown = remote_path_display(app, MOUNT "/a");
    g_assert_cmpstr(shown, ==, "srv.example:/data/a"); /* no name: the host */
    g_free(shown);
    shown = remote_path_display(app, "/mnt/off/a"); /* not connected */
    g_assert_cmpstr(shown, ==, "/mnt/off/a");
    g_free(shown);

    gchar* local = NULL;
    g_assert_true(remote_path_parse(app, "srv.example:/data/b", &local));
    g_assert_cmpstr(local, ==, MOUNT "/b");
    g_free(local);
    g_assert_false(remote_path_parse(app, "srv:~/b", &local));
    g_assert_null(local);
    g_assert_false(remote_path_parse(app, "/tmp", &local));

    SshTarget t = { NULL, NULL, 0, NULL };
    gchar* remote = NULL;
    gchar* machine = NULL;
    g_assert_true(remote_path_locate(app, MOUNT "/nb/a.ipynb", &t, &remote, &machine));
    g_assert_cmpstr(t.host, ==, "srv.example");
    g_assert_cmpstr(remote, ==, "/data/nb/a.ipynb");
    g_assert_cmpstr(machine, ==, "srv.example");
    g_free(remote);
    g_free(machine);
    g_assert_false(remote_path_locate(app, "/mnt/off/a", &t, &remote, &machine)); /* not connected */
    g_assert_null(remote);

    /* Names and parents, as the previews and menus show them */
    const struct {
        const gchar *path, *name, *parent;
    } names[] = {
        { MOUNT, "srv.example", "" },             /* the machine's folder: the machine */
        { MOUNT "/", "srv.example", "" },
        { MOUNT "/a", "a", "srv.example:/data" },
        { MOUNT "/a/b.txt", "b.txt", "srv.example:/data/a" },
        { "/mnt/off/a", "a", NULL },              /* not connected */
        { "/tmp/x", "x", NULL },
    };
    for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
        gchar* name = remote_path_display_name(app, names[i].path);
        gchar* parent = remote_path_display_parent(app, names[i].path);
        g_assert_cmpstr(name, ==, names[i].name);
        g_assert_cmpstr(parent, ==, names[i].parent);
        g_free(name);
        g_free(parent);
    }
    g_free(on->remote_path);
    on->remote_path = g_strdup("/");
    gchar* parent = remote_path_display_parent(app, MOUNT "/etc");
    g_assert_cmpstr(parent, ==, "srv.example:/");
    g_free(parent);
    g_free(on->remote_path);
    on->remote_path = g_strdup("~");
    parent = remote_path_display_parent(app, MOUNT "/a");
    g_assert_cmpstr(parent, ==, "srv.example:~");
    g_free(parent);

    g_assert_false(remote_path_active(app, &t, &machine)); /* no workspace machine */
    app->current_remote_id = g_strdup("rem_1");
    g_assert_true(remote_path_active(app, &t, &machine));
    g_assert_cmpstr(machine, ==, "srv.example");
    g_free(machine);
    g_free(app->current_remote_id);
    app->current_remote_id = g_strdup("rem_0"); /* not connected */
    g_assert_false(remote_path_active(app, &t, &machine));
    g_clear_pointer(&app->current_remote_id, g_free);

    on->is_connected = FALSE; /* remote_free() would unmount it */
    remote_free(app);
    g_free(app);
}

/* A disk outside the remote directory gets a mount of its own, seen as
 * "machine:/mnt/data/..." everywhere */
static void test_disk_mounts(void) {
    gchar* runtime = g_dir_make_tmp("lookin-rp-XXXXXX", NULL);
    g_setenv("XDG_RUNTIME_DIR", runtime, TRUE);
    AppData* app = g_new0(AppData, 1);
    RemoteMachine* rm = g_new0(RemoteMachine, 1);
    rm->id = g_strdup("rem_9");
    rm->name = g_strdup("box");
    rm->host = g_strdup("box.example");
    rm->remote_path = g_strdup("~");
    rm->mount_point = g_strdup(MOUNT);
    rm->is_connected = TRUE;
    remote_machine_add(app, rm);

    gchar* disk = remote_machine_disk_local(rm, "/mnt/data/");
    g_assert_nonnull(disk);
    g_assert_true(g_file_test(disk, G_FILE_TEST_IS_DIR));
    g_assert_false(g_str_has_prefix(disk, MOUNT));
    gchar* again = remote_machine_disk_local(rm, "/mnt/data"); /* the same one */
    g_assert_cmpstr(again, ==, disk);
    g_free(again);
    const gchar *local = NULL, *root = NULL;
    g_assert_true(remote_machine_mount_nth(rm, 1, &local, &root));
    g_assert_cmpstr(local, ==, disk);
    g_assert_cmpstr(root, ==, "/mnt/data");
    g_assert_false(remote_machine_mount_nth(rm, 2, NULL, NULL));

    gchar* inside = g_build_filename(disk, "a", "b.txt", NULL);
    gchar* shown = remote_path_display(app, inside);
    g_assert_cmpstr(shown, ==, "box:/mnt/data/a/b.txt");
    g_free(shown);
    gchar* name = remote_path_display_name(app, disk);
    g_assert_cmpstr(name, ==, "data");
    g_free(name);
    gchar* parent = remote_path_display_parent(app, inside);
    g_assert_cmpstr(parent, ==, "box:/mnt/data/a");
    g_free(parent);
    gchar* remote = NULL;
    g_assert_true(remote_path_locate(app, inside, NULL, &remote, NULL));
    g_assert_cmpstr(remote, ==, "/mnt/data/a/b.txt");
    g_free(remote);
    gchar* parsed = NULL;
    g_assert_true(remote_path_parse(app, "box:/mnt/data/a/b.txt", &parsed));
    g_assert_cmpstr(parsed, ==, inside);
    g_free(parsed);
    g_assert_true(remote_path_parse(app, "box:~/x", &parsed)); /* the home folder still */
    g_assert_cmpstr(parsed, ==, MOUNT "/x");
    g_free(parsed);
    g_assert_true(remote_path_parse(app, "box:/etc/x", &parsed)); /* named, outside every mount */
    g_assert_null(parsed);
    g_free(inside);

    /* A disk inside an absolute remote directory: below the mount point */
    g_free(rm->remote_path);
    rm->remote_path = g_strdup("/srv");
    gchar* below = remote_machine_disk_local(rm, "/srv/www");
    g_assert_cmpstr(below, ==, MOUNT "/www");
    g_free(below);
    g_free(rm->remote_path);
    rm->remote_path = g_strdup("/");
    below = remote_machine_disk_local(rm, "/");
    g_assert_cmpstr(below, ==, MOUNT);
    g_free(below);

    remote_machine_clear_disk_mounts(rm);
    g_assert_false(g_file_test(disk, G_FILE_TEST_EXISTS)); /* its empty folder removed */
    g_free(disk);
    rm->is_connected = FALSE;
    remote_free(app);
    g_free(app);
    gchar* rt_app = g_build_filename(runtime, "lookinfiles", "remotes", NULL);
    g_rmdir(rt_app);
    g_free(rt_app);
    rt_app = g_build_filename(runtime, "lookinfiles", NULL);
    g_rmdir(rt_app);
    g_free(rt_app);
    g_rmdir(runtime);
    g_free(runtime);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/remote_path/disk_mounts", test_disk_mounts);
    g_test_add_func("/remote_path/to_display", test_to_display);
    g_test_add_func("/remote_path/from_display", test_from_display);
    g_test_add_func("/remote_path/connected_machines", test_connected_machines);
    return g_test_run();
}
