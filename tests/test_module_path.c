/* Unit tests of the module lookup (src/core/module_path.c) */
#include "core/module_path.h"

#include <glib/gstdio.h>
#include <stdlib.h>

#define FILE_NAME "lookinfiles-test.so"

static gchar* root = NULL;

/* An empty file at root/rel/FILE_NAME; returns its path */
static gchar* touch(const gchar* rel) {
    gchar* dir = g_build_filename(root, rel, NULL);
    g_mkdir_with_parents(dir, 0755);
    gchar* path = g_build_filename(dir, FILE_NAME, NULL);
    g_assert_true(g_file_set_contents(path, "", 0, NULL));
    g_free(dir);
    return path;
}

static void test_order(void) {
    g_unsetenv("LOOKINFILES_MODULEDIR");
    gchar* bin = g_build_filename(root, "app", "bin", NULL);
    g_mkdir_with_parents(bin, 0755);
    gchar* fallback_dir = g_build_filename(root, "fallback", NULL);

    /* Nothing anywhere */
    g_assert_null(module_path_find(bin, fallback_dir, FILE_NAME));
    g_assert_null(module_path_find(NULL, NULL, FILE_NAME));
    g_assert_null(module_path_find(bin, fallback_dir, NULL));

    /* Each candidate wins over the ones after it */
    gchar* fallback = touch("fallback");
    gchar* found = module_path_find(bin, fallback_dir, FILE_NAME);
    g_assert_cmpstr(found, ==, fallback);
    g_free(found);

    gchar* relocated = touch("app/lib/lookinfiles");
    found = module_path_find(bin, fallback_dir, FILE_NAME);
    g_assert_nonnull(found);
    g_assert_true(g_str_has_suffix(found, "bin/../lib/lookinfiles/" FILE_NAME));
    g_free(found);

    gchar* tree = touch("app/bin/build");
    found = module_path_find(bin, fallback_dir, FILE_NAME);
    g_assert_cmpstr(found, ==, tree);
    g_free(found);

    gchar* beside = touch("app/bin");
    found = module_path_find(bin, fallback_dir, FILE_NAME);
    g_assert_cmpstr(found, ==, beside);
    g_free(found);

    gchar* env_file = touch("env");
    gchar* env_dir = g_build_filename(root, "env", NULL);
    g_setenv("LOOKINFILES_MODULEDIR", env_dir, TRUE);
    found = module_path_find(bin, fallback_dir, FILE_NAME);
    g_assert_cmpstr(found, ==, env_file);
    g_free(found);
    g_unsetenv("LOOKINFILES_MODULEDIR");

    /* Without the program's directory: the fallback */
    found = module_path_find(NULL, fallback_dir, FILE_NAME);
    g_assert_cmpstr(found, ==, fallback);
    g_free(found);

    const gchar* files[] = {fallback, relocated, tree, beside, env_file};
    for (gsize i = 0; i < G_N_ELEMENTS(files); i++) {
        g_remove(files[i]);
        g_free((gchar*)files[i]);
    }
    g_free(env_dir);
    g_free(fallback_dir);
    g_free(bin);
}

static void test_exe_dir(void) {
#ifdef __linux__
    gchar* dir = module_path_exe_dir();
    g_assert_nonnull(dir);
    g_assert_true(g_file_test(dir, G_FILE_TEST_IS_DIR));
    g_free(dir);
#endif
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    root = g_dir_make_tmp("test_module_path_XXXXXX", NULL);
    g_test_add_func("/module_path/order", test_order);
    g_test_add_func("/module_path/exe_dir", test_exe_dir);
    int status = g_test_run();
    gchar* cmd = g_strdup_printf("rm -rf '%s'", root);
    if (system(cmd) != 0) {
        g_printerr("cannot remove %s\n", root);
    }
    g_free(cmd);
    g_free(root);
    return status;
}
