/* Unit tests for src/formats/notebook.c (Jupyter notebooks to Markdown). */
#include "formats/notebook.h"

#include <string.h>

static void test_fixture(void) {
    gchar* text = NULL;
    gsize len = 0;
    g_assert_true(g_file_get_contents("tests/fixtures/preview/analysis.ipynb", &text, &len, NULL));
    NotebookInfo info = { 0 };
    GError* error = NULL;
    gchar* md = notebook_to_markdown(text, len, &info, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(info.n_cells, ==, 5);
    g_assert_cmpuint(info.n_code, ==, 4);
    g_assert_cmpstr(info.language, ==, "python");
    g_assert_cmpstr(info.kernel, ==, "Python 3");
    g_assert_true(g_str_has_prefix(md, "# Sales analysis\n\nMonthly totals, **by region**:\n- north\n- south\n\n"));
    g_assert_nonnull(strstr(md, "*In [1]:*\n\n```python\nimport math\ntotal = sum([12, 30, 8])\nprint(\"total:\", total)\n```\n\n"));
    g_assert_nonnull(strstr(md, "```\ntotal: 50\n```\n\n"));
    g_assert_nonnull(strstr(md, "*Out [2]:*\n\n```\n7.0710678118654755\n```"));
    /* The image data is one line (Jupyter's line breaks removed) */
    const gchar* img = strstr(md, "![output](data:image/png;base64,");
    g_assert_nonnull(img);
    g_assert_null(memchr(img, '\n', (gsize)(strchr(img, ')') - img)));
    /* ANSI colours removed from tracebacks */
    g_assert_nonnull(strstr(md, "ZeroDivisionError: division by zero"));
    g_assert_null(strchr(md, '\x1b'));
    g_free(md);
    notebook_info_clear(&info);
    g_free(text);
}

static void test_long_output_and_errors(void) {
    GString* nb = g_string_new("{\"cells\": [{\"cell_type\": \"code\", \"source\": \"x\", \"outputs\": "
                               "[{\"output_type\": \"stream\", \"name\": \"stderr\", \"text\": \"");
    for (gint i = 0; i < 100; i++) {
        g_string_append_printf(nb, "line %d\\n", i);
    }
    g_string_append(nb, "\"}]}]}");
    gchar* md = notebook_to_markdown(nb->str, nb->len, NULL, NULL);
    g_assert_nonnull(strstr(md, "*stderr*"));
    g_assert_nonnull(strstr(md, "line 59\n… (41 more lines)\n"));
    g_assert_null(strstr(md, "line 60"));
    g_free(md);
    g_string_free(nb, TRUE);
    GError* error = NULL;
    g_assert_null(notebook_to_markdown("{\"a\": 1}", 8, NULL, &error));
    g_assert_nonnull(error);
    g_error_free(error);
    gchar* s = notebook_strip_ansi("\x1b[1;31mred\x1b[0m text");
    g_assert_cmpstr(s, ==, "red text");
    g_free(s);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/notebook/fixture", test_fixture);
    g_test_add_func("/notebook/long_output_and_errors", test_long_output_and_errors);
    return g_test_run();
}
