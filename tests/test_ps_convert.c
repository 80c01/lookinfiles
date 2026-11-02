#include "ps_convert.h"

static void test_gs_argv(void) {
    gchar** argv = ps_gs_argv("/usr/bin/gs", "/tmp/a.ps", "/tmp/o.pdf", FALSE, 20);
    g_assert_cmpstr(argv[0], ==, "/usr/bin/gs");
    g_assert_true(g_strv_contains((const gchar* const*)argv, "-dSAFER"));
    g_assert_true(g_strv_contains((const gchar* const*)argv, "-dLastPage=20"));
    g_assert_true(g_strv_contains((const gchar* const*)argv, "-sOutputFile=/tmp/o.pdf"));
    g_assert_false(g_strv_contains((const gchar* const*)argv, "-dEPSCrop"));
    g_assert_cmpstr(argv[g_strv_length(argv) - 1], ==, "/tmp/a.ps");
    g_strfreev(argv);

    argv = ps_gs_argv("gs", "/tmp/a.eps", "/tmp/o.pdf", TRUE, 0);
    g_assert_true(g_strv_contains((const gchar* const*)argv, "-dEPSCrop"));
    for (gchar** p = argv; *p != NULL; p++) {
        g_assert_false(g_str_has_prefix(*p, "-dLastPage"));
    }
    g_strfreev(argv);
}

static void test_dvips_argv(void) {
    gchar** argv = ps_dvips_argv("dvips", "/tmp/a.dvi", "/tmp/o.ps", 5);
    g_assert_true(g_strv_contains((const gchar* const*)argv, "-R2"));
    g_assert_true(g_strv_contains((const gchar* const*)argv, "-l=5"));
    g_assert_cmpstr(argv[g_strv_length(argv) - 1], ==, "/tmp/a.dvi");
    g_strfreev(argv);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ps_convert/gs_argv", test_gs_argv);
    g_test_add_func("/ps_convert/dvips_argv", test_dvips_argv);
    return g_test_run();
}
