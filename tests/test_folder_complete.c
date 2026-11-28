/* Unit tests for src/core/folder_complete.c: folder field completion. */
#include "core/folder_complete.h"

static void check_split(const gchar* text, const gchar* dir, const gchar* prefix) {
    gchar* d = NULL;
    gchar* p = NULL;
    g_assert_true(folder_complete_split(text, "/home/me", &d, &p));
    g_assert_cmpstr(d, ==, dir);
    g_assert_cmpstr(p, ==, prefix);
    g_free(d);
    g_free(p);
}

static void test_split(void) {
    check_split("/usr/lo", "/usr", "lo");
    check_split("/usr/", "/usr", "");
    check_split("/us", "/", "us");
    check_split("/", "/", "");
    check_split("~", "/home/me", "");
    check_split("~/", "/home/me", "");
    check_split("~/pro", "/home/me", "pro");
    check_split("~/src/lo", "/home/me/src", "lo");
    gchar* d = NULL;
    gchar* p = NULL;
    g_assert_false(folder_complete_split("src", "/home/me", &d, &p));
    g_assert_false(folder_complete_split("~me/x", "/home/me", &d, &p));
    g_assert_false(folder_complete_split("", "/home/me", &d, &p));
    g_assert_null(d);
    g_assert_null(p);
}

static void check_join(const gchar* text, const gchar* name, const gchar* want) {
    gchar* out = folder_complete_join(text, name);
    g_assert_cmpstr(out, ==, want);
    g_free(out);
}

static void test_join(void) {
    check_join("~/pro", "projects", "~/projects");
    check_join("~", "projects", "~/projects");
    check_join("~/", "projects", "~/projects");
    check_join("/us", "usr", "/usr");
    check_join("/usr/", "local", "/usr/local");
}

static void test_name_score(void) {
    g_assert_cmpint(folder_complete_name_score("Projects", "Pro"), ==, 2);
    g_assert_cmpint(folder_complete_name_score("Projects", "pro"), ==, 1);
    g_assert_cmpint(folder_complete_name_score("old_projects", "pro"), ==, 0);
    g_assert_cmpint(folder_complete_name_score("music", "pro"), ==, -1);
    g_assert_cmpint(folder_complete_name_score("anything", ""), ==, 2);
    g_assert_cmpint(folder_complete_name_score(".git", ""), ==, -1);
    g_assert_cmpint(folder_complete_name_score(".git", "g"), ==, -1);
    g_assert_cmpint(folder_complete_name_score(".git", ".g"), ==, 2);
    g_assert_cmpint(folder_complete_name_score("Été", "été"), ==, 1);
}

static void test_tilde(void) {
    gchar* a = folder_complete_tilde("/home/me/src", "/home/me");
    gchar* b = folder_complete_tilde("/home/me", "/home/me");
    gchar* c = folder_complete_tilde("/home/meow", "/home/me");
    gchar* d = folder_complete_tilde("/usr", "/");
    g_assert_cmpstr(a, ==, "~/src");
    g_assert_cmpstr(b, ==, "~");
    g_assert_cmpstr(c, ==, "/home/meow");
    g_assert_cmpstr(d, ==, "/usr");
    g_free(a);
    g_free(b);
    g_free(c);
    g_free(d);
}

static void test_is_python(void) {
    g_assert_true(folder_complete_is_python("python"));
    g_assert_true(folder_complete_is_python("python3"));
    g_assert_true(folder_complete_is_python("python3.12"));
    g_assert_true(folder_complete_is_python("pypy3"));
    g_assert_true(folder_complete_is_python("python.exe"));
    g_assert_true(folder_complete_is_python("python3.12.exe"));
    g_assert_false(folder_complete_is_python("python3-config"));
    g_assert_false(folder_complete_is_python("pythonw"));
    g_assert_false(folder_complete_is_python("ipython"));
    g_assert_false(folder_complete_is_python("pip3"));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/folder_complete/split", test_split);
    g_test_add_func("/folder_complete/join", test_join);
    g_test_add_func("/folder_complete/name_score", test_name_score);
    g_test_add_func("/folder_complete/tilde", test_tilde);
    g_test_add_func("/folder_complete/is_python", test_is_python);
    return g_test_run();
}
