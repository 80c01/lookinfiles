/* Unit tests for src/remote_store.c: ~/.ssh/config discovery and remotes.xml
 * persistence. HOME and the XDG directories point to a temporary directory. */
#include "remote_store.h"

#include <glib/gstdio.h>
#include <string.h>

static gchar* tmp_home;

static gchar* config_dir(void) {
    return g_build_filename(tmp_home, "config", "lookinfiles", NULL);
}

static void write_file(const gchar* path, const gchar* contents) {
    gchar* dir = g_path_get_dirname(path);
    g_assert_cmpint(g_mkdir_with_parents(dir, 0700), ==, 0);
    g_free(dir);
    g_assert_true(g_file_set_contents(path, contents, -1, NULL));
}

static void write_ssh_config(const gchar* contents) {
    gchar* path = g_build_filename(tmp_home, ".ssh", "config", NULL);
    write_file(path, contents);
    g_free(path);
}

static void write_remotes_xml(const gchar* contents) {
    gchar* dir = config_dir();
    gchar* path = g_build_filename(dir, "remotes.xml", NULL);
    write_file(path, contents);
    g_free(path);
    g_free(dir);
}

static void reset_files(void) {
    gchar* ssh = g_build_filename(tmp_home, ".ssh", "config", NULL);
    gchar* dir = config_dir();
    gchar* xml = g_build_filename(dir, "remotes.xml", NULL);
    g_remove(ssh);
    g_remove(xml);
    g_free(ssh);
    g_free(xml);
    g_free(dir);
}

static AppData* new_app(void) {
    AppData* app = g_new0(AppData, 1);
    remote_init(app);
    return app;
}

static void free_app(AppData* app) {
    remote_free(app);
    g_free(app->current_remote_id);
    g_free(app);
}

static void test_ssh_config(void) {
    reset_files();
    write_ssh_config("# comment\n"
                     "Host web\n"
                     "    HostName web.example.com\n"
                     "    User deploy\n"
                     "    Port=2222\n"
                     "    IdentityFile ~/.ssh/id_web\n"
                     "\n"
                     "Host *.internal\n"
                     "    User ignored\n"
                     "Host plain\n");
    AppData* app = new_app();
    g_assert_cmpuint(g_list_length(app->remotes), ==, 2);

    RemoteMachine* web = remote_find_by_id(app, "ssh_web");
    g_assert_nonnull(web);
    g_assert_cmpstr(web->name, ==, "web");
    g_assert_cmpstr(web->host, ==, "web.example.com");
    g_assert_cmpstr(web->user, ==, "deploy");
    g_assert_cmpint(web->port, ==, 2222);
    g_assert_cmpstr(web->identity_file, ==, "~/.ssh/id_web");
    g_assert_true(web->from_ssh_config);

    RemoteMachine* plain = remote_find_by_id(app, "ssh_plain");
    g_assert_nonnull(plain);
    g_assert_cmpstr(plain->host, ==, "plain");
    g_assert_cmpint(plain->port, ==, 22);
    g_assert_cmpstr(plain->remote_path, ==, "~");
    free_app(app);
}

static void test_save_and_reload(void) {
    reset_files();
    write_ssh_config("Host fromssh\n    HostName ssh.example.com\n");
    AppData* app = new_app();
    g_assert_cmpuint(g_list_length(app->remotes), ==, 1);

    RemoteMachine* rm = g_new0(RemoteMachine, 1);
    rm->id = g_strdup("rem_1");
    rm->name = g_strdup("R&D <lab> \"main\"");
    rm->host = g_strdup("10.0.0.5");
    rm->user = g_strdup("me");
    rm->port = 2200;
    rm->remote_path = g_strdup("/srv/data");
    rm->identity_file = g_strdup("");
    app->remotes = g_list_append(app->remotes, rm);
    remote_save(app);
    free_app(app);

    app = new_app();
    /* Saved machine reloaded first, ssh config host merged after it */
    g_assert_cmpuint(g_list_length(app->remotes), ==, 2);
    RemoteMachine* back = remote_find_by_id(app, "rem_1");
    g_assert_nonnull(back);
    g_assert_cmpstr(back->name, ==, "R&D <lab> \"main\"");
    g_assert_cmpstr(back->host, ==, "10.0.0.5");
    g_assert_cmpstr(back->user, ==, "me");
    g_assert_cmpint(back->port, ==, 2200);
    g_assert_cmpstr(back->remote_path, ==, "/srv/data");
    g_assert_false(back->from_ssh_config);
    g_assert_true(remote_find_by_id(app, "ssh_fromssh") != NULL);

    /* ~/.ssh/config hosts are never written to remotes.xml */
    gchar* dir = config_dir();
    gchar* xml_path = g_build_filename(dir, "remotes.xml", NULL);
    gchar* xml = NULL;
    g_assert_true(g_file_get_contents(xml_path, &xml, NULL, NULL));
    g_assert_null(strstr(xml, "fromssh"));
    g_free(xml);
    g_free(xml_path);
    g_free(dir);
    free_app(app);
}

static void test_ssh_host_already_saved(void) {
    reset_files();
    write_remotes_xml("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<remotes>\n"
                      "  <machine id=\"rem_9\" name=\"box\" host=\"box.example.com\" user=\"\" "
                      "port=\"22\" path=\"~\" key=\"\"/>\n</remotes>\n");
    write_ssh_config("Host other\n    HostName box.example.com\n");
    AppData* app = new_app();
    /* Same host as a saved machine: the ssh config entry is skipped */
    g_assert_cmpuint(g_list_length(app->remotes), ==, 1);
    g_assert_nonnull(remote_find_by_id(app, "rem_9"));
    free_app(app);
}

static void test_active_machine(void) {
    reset_files();
    write_ssh_config("Host a\nHost b\n");
    AppData* app = new_app();
    g_assert_null(remote_get_active_machine(app));
    app->current_remote_id = g_strdup("ssh_b");
    RemoteMachine* active = remote_get_active_machine(app);
    g_assert_nonnull(active);
    g_assert_cmpstr(active->name, ==, "b");
    g_assert_null(remote_find_by_id(app, "missing"));
    g_assert_null(remote_find_by_id(NULL, "ssh_a"));
    free_app(app);
}

static void test_reload_ssh_config(void) {
    reset_files();
    write_remotes_xml("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<remotes>\n"
                      "  <machine id=\"rem_1\" name=\"manual\" host=\"10.0.0.1\" user=\"\" "
                      "port=\"22\" path=\"~\" key=\"\"/>\n</remotes>\n");
    write_ssh_config("Host old\nHost busy\n");
    AppData* app = new_app();
    g_assert_cmpuint(g_list_length(app->remotes), ==, 3);
    remote_find_by_id(app, "ssh_busy")->is_connected = TRUE;

    write_ssh_config("Host new\n");
    remote_reload_ssh_config(app);
    g_assert_nonnull(remote_find_by_id(app, "rem_1"));     /* manual machine untouched */
    g_assert_nonnull(remote_find_by_id(app, "ssh_new"));   /* added to config */
    g_assert_null(remote_find_by_id(app, "ssh_old"));      /* removed from config */
    g_assert_nonnull(remote_find_by_id(app, "ssh_busy"));  /* connected: kept */
    g_assert_cmpuint(g_list_length(app->remotes), ==, 3);
    remote_find_by_id(app, "ssh_busy")->is_connected = FALSE; /* no unmount in free */
    free_app(app);
}

static void remove_tree(const gchar* path) {
    if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
        GDir* d = g_dir_open(path, 0, NULL);
        const gchar* name;
        while (d != NULL && (name = g_dir_read_name(d)) != NULL) {
            gchar* child = g_build_filename(path, name, NULL);
            remove_tree(child);
            g_free(child);
        }
        if (d != NULL) {
            g_dir_close(d);
        }
    }
    g_remove(path);
}

static void test_folders(void) {
    reset_files();
    write_ssh_config("Host fromssh\n");
    AppData* app = new_app();
    RemoteFolder* prod = remote_folder_add(app, "Prod & <test>", "starred");
    RemoteFolder* empty = remote_folder_add(app, "", "not-an-icon");
    g_assert_cmpstr(empty->name, ==, "Folder");
    g_assert_cmpstr(empty->icon, ==, "folder");
    gchar* prod_id = g_strdup(prod->id);

    RemoteMachine* rm = g_new0(RemoteMachine, 1);
    rm->id = g_strdup("rem_f");
    rm->name = g_strdup("box");
    rm->host = g_strdup("box.example.com");
    rm->user = g_strdup("");
    rm->port = 22;
    rm->remote_path = g_strdup("~");
    rm->identity_file = g_strdup("");
    remote_machine_set_folder(rm, prod);
    app->remotes = g_list_append(app->remotes, rm);
    prod->expanded = FALSE;
    g_assert_cmpuint(remote_folder_count(app, prod), ==, 1);
    g_assert_true(remote_machine_folder(app, remote_find_by_id(app, "ssh_fromssh")) == NULL);
    remote_save(app);
    free_app(app);

    app = new_app();
    g_assert_cmpuint(g_list_length(app->remote_folders), ==, 2);
    RemoteFolder* back = remote_folder_find(app, prod_id);
    g_assert_nonnull(back);
    g_assert_cmpstr(back->name, ==, "Prod & <test>");
    g_assert_cmpstr(back->icon, ==, "starred");
    g_assert_false(back->expanded);
    RemoteMachine* box = remote_find_by_id(app, "rem_f");
    g_assert_true(remote_machine_folder(app, box) == back);

    /* Removing a folder moves its machines to the top level */
    remote_folder_remove(app, back);
    g_assert_null(remote_folder_find(app, prod_id));
    g_assert_null(box->folder_id);
    g_assert_cmpuint(g_list_length(app->remote_folders), ==, 1);
    g_free(prod_id);
    free_app(app);
}

int main(int argc, char** argv) {
    /* Must happen before any GLib call caches the user directories */
    tmp_home = g_dir_make_tmp("remote_store_test_XXXXXX", NULL);
    g_assert_nonnull(tmp_home);
    gchar* cfg = g_build_filename(tmp_home, "config", NULL);
    gchar* cache = g_build_filename(tmp_home, "cache", NULL);
    g_setenv("HOME", tmp_home, TRUE);
    g_setenv("XDG_CONFIG_HOME", cfg, TRUE);
    g_setenv("XDG_CACHE_HOME", cache, TRUE);
    g_free(cfg);
    g_free(cache);

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/remote_store/ssh_config", test_ssh_config);
    g_test_add_func("/remote_store/save_and_reload", test_save_and_reload);
    g_test_add_func("/remote_store/ssh_host_already_saved", test_ssh_host_already_saved);
    g_test_add_func("/remote_store/active_machine", test_active_machine);
    g_test_add_func("/remote_store/reload_ssh_config", test_reload_ssh_config);
    g_test_add_func("/remote_store/folders", test_folders);
    gint rc = g_test_run();

    remove_tree(tmp_home);
    g_free(tmp_home);
    return rc;
}
