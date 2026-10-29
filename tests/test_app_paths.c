/* Unit tests for src/app_paths.c: per-user directories named after the
 * application. */
#include "app_paths.h"

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
    g_test_add_func("/app_paths/dirs_use_app_name", test_dirs_use_app_name);
    return g_test_run();
}
