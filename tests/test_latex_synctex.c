/* Unit tests for src/latex/latex_synctex.c: SyncTeX places of a source line. */
#include "latex/latex_synctex.h"

#include <glib/gstdio.h>

static void test_parse_view(void) {
    const gchar* out =
        "This is SyncTeX command line utility, version 1.5\n"
        "SyncTeX result begin\n"
        "Output:main.pdf\n"
        "Page:3\n"
        "x:195.232300\n"
        "y:156.585541\n"
        "h:133.768356\n"
        "v:158.522720\n"
        "W:343.711060\n"
        "H:8.855677\n"
        "before:\n"
        "offset:-1\n"
        "middle:\n"
        "after:\n"
        "Output:main.pdf\n"
        "Page:4\n"
        "x:1.0\n"
        "y:2.0\n"
        "SyncTeX result end\n";
    gint page = -1;
    gdouble y = 0.0;
    g_assert_true(latex_synctex_parse_view(out, &page, &y));
    g_assert_cmpint(page, ==, 2);
    g_assert_cmpfloat_with_epsilon(y, 156.585541, 1e-6);
}

static void test_parse_none(void) {
    gint page = 7;
    gdouble y = 1.0;
    g_assert_false(latex_synctex_parse_view(NULL, &page, &y));
    g_assert_false(latex_synctex_parse_view("This is SyncTeX command line utility, version 1.5\n", &page, &y));
    g_assert_false(latex_synctex_parse_view("SyncTeX result begin\nSyncTeX result end\n", &page, &y));
    g_assert_false(latex_synctex_parse_view("SyncTeX result begin\nPage:2\nSyncTeX result end\n", &page, &y));
    g_assert_cmpint(page, ==, 7);
}

static void test_edit(void) {
    gchar* arg = latex_synctex_edit_arg(2, 120.5, 300.25, "/doc/main.pdf");
    g_assert_cmpstr(arg, ==, "3:120.50:300.25:/doc/main.pdf");
    g_free(arg);
    const gchar* out =
        "This is SyncTeX command line utility, version 1.5\n"
        "SyncTeX result begin\n"
        "Output:/doc/./main.pdf\n"
        "Input:/doc/./chapters/one.tex\n"
        "Line:42\n"
        "Column:-1\n"
        "Offset:0\n"
        "Context:\n"
        "Output:/doc/./main.pdf\n"
        "Input:/doc/./main.tex\n"
        "Line:7\n"
        "SyncTeX result end\n";
    gchar* input = NULL;
    gint line = -1;
    g_assert_true(latex_synctex_parse_edit(out, "/doc", &input, &line));
    g_assert_cmpstr(input, ==, "/doc/chapters/one.tex");
    g_assert_cmpint(line, ==, 41);
    g_free(input);
    /* A relative input, from the folder of the PDF */
    g_assert_true(latex_synctex_parse_edit("SyncTeX result begin\nInput:./intro.tex\nLine:3\nSyncTeX result end\n",
                                           "/doc", &input, &line));
    g_assert_cmpstr(input, ==, "/doc/intro.tex");
    g_assert_cmpint(line, ==, 2);
    g_free(input);
    input = NULL;
    g_assert_false(latex_synctex_parse_edit(NULL, "/doc", &input, &line));
    g_assert_false(latex_synctex_parse_edit("SyncTeX result begin\nSyncTeX result end\n", "/doc", &input, &line));
    g_assert_false(latex_synctex_parse_edit("SyncTeX result begin\nInput:a.tex\nLine:0\n", "/doc", &input, &line));
    g_assert_null(input);
}

static void test_file(void) {
    gchar* dir = g_dir_make_tmp("latex_synctex_XXXXXX", NULL);
    gchar* pdf = g_build_filename(dir, "main.pdf", NULL);
    gchar* gz = g_build_filename(dir, "main.synctex.gz", NULL);
    gchar* plain = g_build_filename(dir, "main.synctex", NULL);
    g_assert_null(latex_synctex_file(pdf));
    g_assert_true(g_file_set_contents(plain, "", 0, NULL));
    gchar* f = latex_synctex_file(pdf);
    g_assert_cmpstr(f, ==, plain);
    g_free(f);
    g_assert_true(g_file_set_contents(gz, "", 0, NULL));
    f = latex_synctex_file(pdf);
    g_assert_cmpstr(f, ==, gz);
    g_free(f);
    g_assert_null(latex_synctex_file("main.tex"));
    g_assert_null(latex_synctex_file(NULL));
    g_unlink(gz);
    g_unlink(plain);
    g_rmdir(dir);
    g_free(plain);
    g_free(gz);
    g_free(pdf);
    g_free(dir);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/latex_synctex/parse_view", test_parse_view);
    g_test_add_func("/latex_synctex/parse_none", test_parse_none);
    g_test_add_func("/latex_synctex/edit", test_edit);
    g_test_add_func("/latex_synctex/file", test_file);
    return g_test_run();
}
