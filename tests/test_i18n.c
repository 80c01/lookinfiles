#include "core/i18n.h"

#include <glib/gstdio.h>

/* A temporary directory with the given subdirectories created */
static gchar* make_tree(const char* const* subdirs) {
    gchar* root = g_dir_make_tmp("lookin-i18n-XXXXXX", NULL);
    g_assert_nonnull(root);
    for (gsize i = 0; subdirs[i] != NULL; i++) {
        gchar* p = g_build_filename(root, subdirs[i], NULL);
        g_assert_cmpint(g_mkdir_with_parents(p, 0700), ==, 0);
        g_free(p);
    }
    return root;
}

static void remove_tree(const char* path) {
    GDir* dir = g_dir_open(path, 0, NULL);
    if (dir != NULL) {
        const char* name;
        while ((name = g_dir_read_name(dir)) != NULL) {
            gchar* child = g_build_filename(path, name, NULL);
            if (g_file_test(child, G_FILE_TEST_IS_DIR)) {
                remove_tree(child);
            } else {
                g_unlink(child);
            }
            g_free(child);
        }
        g_dir_close(dir);
    }
    g_rmdir(path);
}

static void test_fallback(void) {
    g_unsetenv("LOOKINFILES_LOCALEDIR");
    gchar* dir = i18n_locale_dir(NULL, "/usr/share/locale");
    g_assert_cmpstr(dir, ==, "/usr/share/locale");
    g_free(dir);

    const char* none[] = { NULL };
    gchar* root = make_tree(none);
    dir = i18n_locale_dir(root, "/fallback");
    g_assert_cmpstr(dir, ==, "/fallback");
    g_free(dir);
    remove_tree(root);
    g_free(root);
}

static void test_environment_wins(void) {
    const char* subdirs[] = { "build/locale", NULL };
    gchar* root = make_tree(subdirs);
    g_setenv("LOOKINFILES_LOCALEDIR", "/somewhere", TRUE);
    gchar* dir = i18n_locale_dir(root, "/fallback");
    g_assert_cmpstr(dir, ==, "/somewhere");
    g_free(dir);
    g_unsetenv("LOOKINFILES_LOCALEDIR");
    remove_tree(root);
    g_free(root);
}

static void test_source_tree_first(void) {
    g_unsetenv("LOOKINFILES_LOCALEDIR");
    const char* subdirs[] = { "bin/build/locale", "bin/share/locale", "share/locale", NULL };
    gchar* root = make_tree(subdirs);
    gchar* bin = g_build_filename(root, "bin", NULL);
    gchar* dir = i18n_locale_dir(bin, "/fallback");
    gchar* want = g_build_filename(bin, "build", "locale", NULL);
    g_assert_cmpstr(dir, ==, want);
    g_free(dir);
    g_free(want);
    g_free(bin);
    remove_tree(root);
    g_free(root);
}

static void test_portable_then_installed(void) {
    g_unsetenv("LOOKINFILES_LOCALEDIR");
    const char* portable[] = { "share/locale", NULL };
    gchar* root = make_tree(portable);
    gchar* dir = i18n_locale_dir(root, "/fallback");
    gchar* want = g_build_filename(root, "share", "locale", NULL);
    g_assert_cmpstr(dir, ==, want);
    g_free(dir);
    g_free(want);
    remove_tree(root);
    g_free(root);

    const char* installed[] = { "usr/bin", "usr/share/locale", NULL };
    root = make_tree(installed);
    gchar* bin = g_build_filename(root, "usr", "bin", NULL);
    dir = i18n_locale_dir(bin, "/fallback");
    want = g_build_filename(bin, "..", "share", "locale", NULL);
    g_assert_cmpstr(dir, ==, want);
    g_free(dir);
    g_free(want);
    g_free(bin);
    remove_tree(root);
    g_free(root);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/i18n/fallback", test_fallback);
    g_test_add_func("/i18n/environment_wins", test_environment_wins);
    g_test_add_func("/i18n/source_tree_first", test_source_tree_first);
    g_test_add_func("/i18n/portable_then_installed", test_portable_then_installed);
    return g_test_run();
}
