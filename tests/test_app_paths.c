/* Unit tests for src/app_paths.c: moving the config directory of the old
 * application name to the new one. */
#include "app_paths.h"

#include <glib/gstdio.h>

static gchar* make_base(void) {
    gchar* base = g_dir_make_tmp("app_paths-XXXXXX", NULL);
    g_assert_nonnull(base);
    return base;
}

static gchar* sub(const gchar* base, const gchar* a, const gchar* b) {
    return g_build_filename(base, a, b, NULL);
}

static void remove_tree(const gchar* path) {
    GDir* d = g_dir_open(path, 0, NULL);
    if (d != NULL) {
        const gchar* name;
        while ((name = g_dir_read_name(d)) != NULL) {
            gchar* child = g_build_filename(path, name, NULL);
            if (g_file_test(child, G_FILE_TEST_IS_DIR)) {
                remove_tree(child);
            } else {
                g_unlink(child);
            }
            g_free(child);
        }
        g_dir_close(d);
    }
    g_rmdir(path);
}

static void test_moves_old_dir(void) {
    gchar* base = make_base();
    gchar* old_dir = sub(base, APP_LEGACY_DIR_NAME, NULL);
    g_assert_cmpint(g_mkdir(old_dir, 0700), ==, 0);
    gchar* old_file = sub(base, APP_LEGACY_DIR_NAME, "bookmarks.xml");
    g_assert_true(g_file_set_contents(old_file, "data", -1, NULL));

    g_assert_true(app_paths_migrate_legacy_in(base));

    gchar* new_file = sub(base, APP_DIR_NAME, "bookmarks.xml");
    gchar* contents = NULL;
    g_assert_true(g_file_get_contents(new_file, &contents, NULL, NULL));
    g_assert_cmpstr(contents, ==, "data");
    g_assert_false(g_file_test(old_dir, G_FILE_TEST_EXISTS));

    g_free(contents);
    g_free(new_file);
    g_free(old_file);
    g_free(old_dir);
    remove_tree(base);
    g_free(base);
}

static void test_keeps_existing_new_dir(void) {
    gchar* base = make_base();
    gchar* old_dir = sub(base, APP_LEGACY_DIR_NAME, NULL);
    gchar* new_dir = sub(base, APP_DIR_NAME, NULL);
    g_assert_cmpint(g_mkdir(old_dir, 0700), ==, 0);
    g_assert_cmpint(g_mkdir(new_dir, 0700), ==, 0);

    g_assert_false(app_paths_migrate_legacy_in(base));
    g_assert_true(g_file_test(old_dir, G_FILE_TEST_IS_DIR));
    g_assert_true(g_file_test(new_dir, G_FILE_TEST_IS_DIR));

    g_free(old_dir);
    g_free(new_dir);
    remove_tree(base);
    g_free(base);
}

static void test_nothing_to_move(void) {
    gchar* base = make_base();
    g_assert_false(app_paths_migrate_legacy_in(base));
    gchar* new_dir = sub(base, APP_DIR_NAME, NULL);
    g_assert_false(g_file_test(new_dir, G_FILE_TEST_EXISTS));
    g_assert_false(app_paths_migrate_legacy_in(NULL));
    g_free(new_dir);
    remove_tree(base);
    g_free(base);
}

static void test_dirs_use_app_name(void) {
    gchar* cfg = app_config_dir();
    gchar* cache = app_cache_dir();
    gchar* cfg_base = g_path_get_basename(cfg);
    gchar* cache_base = g_path_get_basename(cache);
    g_assert_cmpstr(cfg_base, ==, "lookin");
    g_assert_cmpstr(cache_base, ==, "lookin");
    g_free(cfg_base);
    g_free(cache_base);
    g_free(cfg);
    g_free(cache);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/app_paths/moves_old_dir", test_moves_old_dir);
    g_test_add_func("/app_paths/keeps_existing_new_dir", test_keeps_existing_new_dir);
    g_test_add_func("/app_paths/nothing_to_move", test_nothing_to_move);
    g_test_add_func("/app_paths/dirs_use_app_name", test_dirs_use_app_name);
    return g_test_run();
}
