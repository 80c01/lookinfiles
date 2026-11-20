/* Unit tests for src/core/app_paths.c: per-user directories named after the
 * application. */
#include "core/app_paths.h"

#include <glib/gstdio.h>
#include <sys/stat.h>
#include <unistd.h>

static void test_dirs_use_app_name(void) {
    gchar* cfg = app_config_dir();
    gchar* cache = app_cache_dir();
    gchar* cfg_base = g_path_get_basename(cfg);
    gchar* cache_base = g_path_get_basename(cache);
    g_assert_cmpstr(cfg_base, ==, "lookinfiles");
    g_assert_cmpstr(cache_base, ==, "lookinfiles");
    g_free(cfg_base);
    g_free(cache_base);
    g_free(cfg);
    g_free(cache);
}

/* $XDG_RUNTIME_DIR set: a private lookinfiles folder inside it */
static void test_runtime_dir_xdg(void) {
    gchar* base = g_dir_make_tmp("app_paths_XXXXXX", NULL);
    gchar* dir = app_runtime_dir_for(base, "/nonexistent");
    gchar* want = g_build_filename(base, "lookinfiles", NULL);
    g_assert_cmpstr(dir, ==, want);
    GStatBuf st;
    g_assert_cmpint(g_lstat(dir, &st), ==, 0);
    g_assert_true(S_ISDIR(st.st_mode));
    g_assert_cmpint(st.st_mode & 077, ==, 0);
    g_rmdir(dir);
    g_rmdir(base);
    g_free(want);
    g_free(dir);
    g_free(base);
}

/* Unset (or relative): lookinfiles-<uid> in the temporary folder, never the cache */
static void test_runtime_dir_fallback(void) {
    gchar* tmp = g_dir_make_tmp("app_paths_XXXXXX", NULL);
    gchar* name = g_strdup_printf("lookinfiles-%u", (guint)getuid());
    gchar* want = g_build_filename(tmp, name, NULL);
    gchar* dir = app_runtime_dir_for(NULL, tmp);
    g_assert_cmpstr(dir, ==, want);
    g_free(dir);
    dir = app_runtime_dir_for("relative/run", tmp);
    g_assert_cmpstr(dir, ==, want);
    g_free(dir);
    g_rmdir(want);
    g_rmdir(tmp);
    g_free(want);
    g_free(name);
    g_free(tmp);
}

/* A fallback folder that is a symlink or open to others is refused */
static void test_runtime_dir_unsafe(void) {
    gchar* tmp = g_dir_make_tmp("app_paths_XXXXXX", NULL);
    gchar* name = g_strdup_printf("lookinfiles-%u", (guint)getuid());
    gchar* path = g_build_filename(tmp, name, NULL);
    gchar* target = g_build_filename(tmp, "elsewhere", NULL);
    g_assert_cmpint(g_mkdir(target, 0700), ==, 0);
    g_assert_cmpint(symlink(target, path), ==, 0);
    g_assert_null(app_runtime_dir_for(NULL, tmp));
    g_remove(path);
    g_assert_cmpint(g_mkdir(path, 0700), ==, 0);
    g_assert_cmpint(g_chmod(path, 0777), ==, 0);
    g_assert_null(app_runtime_dir_for(NULL, tmp));
    g_rmdir(path);
    g_rmdir(target);
    g_rmdir(tmp);
    g_free(target);
    g_free(path);
    g_free(name);
    g_free(tmp);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/app_paths/dirs_use_app_name", test_dirs_use_app_name);
    g_test_add_func("/app_paths/runtime_dir_xdg", test_runtime_dir_xdg);
    g_test_add_func("/app_paths/runtime_dir_fallback", test_runtime_dir_fallback);
    g_test_add_func("/app_paths/runtime_dir_unsafe", test_runtime_dir_unsafe);
    return g_test_run();
}
