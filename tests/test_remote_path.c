/* Unit tests for src/core/remote_path.c: paths of a mounted remote machine
 * shown as "<machine>:<remote path>" and read back. */
#include "core/remote_path.h"
#include "explorer/remote_store.h"

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

    on->is_connected = FALSE; /* remote_free() would unmount it */
    remote_free(app);
    g_free(app);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/remote_path/to_display", test_to_display);
    g_test_add_func("/remote_path/from_display", test_from_display);
    g_test_add_func("/remote_path/connected_machines", test_connected_machines);
    return g_test_run();
}
