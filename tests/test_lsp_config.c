/* Unit tests for src/code/lsp_config.c: language server commands from the
 * defaults and lsp.ini. */
#include "code/lsp_config.h"

#include <glib/gstdio.h>

static void test_keys(void) {
    g_assert_cmpstr(lsp_config_key(CODE_LANG_C), ==, "c");
    g_assert_cmpstr(lsp_config_key(CODE_LANG_JS), ==, "javascript");
    g_assert_null(lsp_config_key(CODE_LANG_MAKE));
    g_assert_null(lsp_config_command(CODE_LANG_MAKE, NULL));
}

static void test_config_file(void) {
    gchar* dir = g_dir_make_tmp("lsp_config_XXXXXX", NULL);
    gchar* ini = g_build_filename(dir, "lsp.ini", NULL);
    /* "sh" and "true" exist everywhere: stand-ins for servers */
    g_assert_true(g_file_set_contents(ini,
                                      "[servers]\n"
                                      "python=sh -c 'exec my-server --stdio'\n"
                                      "c=true --background-index\n"
                                      "rust=none\n"
                                      "go=lookin-missing-server\n",
                                      -1, NULL));
    gchar** argv = lsp_config_command(CODE_LANG_PYTHON, ini);
    g_assert_nonnull(argv);
    g_assert_true(g_path_is_absolute(argv[0]));
    g_assert_cmpstr(argv[1], ==, "-c");
    g_assert_cmpstr(argv[2], ==, "exec my-server --stdio");
    g_strfreev(argv);
    /* C++ uses the "c" entry */
    argv = lsp_config_command(CODE_LANG_CPP, ini);
    g_assert_nonnull(argv);
    g_assert_cmpstr(argv[1], ==, "--background-index");
    g_strfreev(argv);
    /* Disabled, or configured but not installed: no server (no default either) */
    g_assert_null(lsp_config_command(CODE_LANG_RUST, ini));
    g_assert_null(lsp_config_command(CODE_LANG_GO, ini));
    g_unlink(ini);
    g_rmdir(dir);
    g_free(ini);
    g_free(dir);
}

static void test_defaults(void) {
    /* Whatever is installed: a default command is absolute when found */
    gchar** argv = lsp_config_command(CODE_LANG_C, "/nonexistent/lsp.ini");
    if (argv != NULL) {
        g_assert_true(g_path_is_absolute(argv[0]));
        g_strfreev(argv);
    }
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/lsp_config/keys", test_keys);
    g_test_add_func("/lsp_config/config_file", test_config_file);
    g_test_add_func("/lsp_config/defaults", test_defaults);
    return g_test_run();
}
