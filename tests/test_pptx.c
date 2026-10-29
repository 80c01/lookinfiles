/* Unit tests for src/pptx.c (PowerPoint presentations to HTML). */
#include "pptx.h"

#include <string.h>

static void test_slides(void) {
    guint n = 0;
    GError* error = NULL;
    gchar* html = pptx_file_to_html("tests/fixtures/preview/roadmap.pptx", 100000, &n, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(n, ==, 2);
    /* Presentation order (from the slide list), not file names */
    const gchar* s1 = strstr(html, "<h2>Slide 1</h2>\n<h3>Lookin roadmap</h3>\n<p><i>Autumn 2026</i></p>");
    const gchar* s2 = strstr(html, "<h2>Slide 2</h2>\n<h3>Previews</h3>");
    g_assert_true(s1 != NULL && s2 != NULL && s1 < s2);
    g_assert_nonnull(strstr(html, "<ul>\n<li><b>Archives</b></li>\n<ul>\n<li>ZIP and tar</li>\n</ul>\n<li>Spreadsheets</li>\n</ul>"));
    g_assert_nonnull(strstr(html, "<tr><td>Format </td><td>Level </td></tr>"));
    g_assert_nonnull(strstr(html, "<img src=\"data:image/png;base64,"));
    g_assert_null(strstr(html, "Secret speaker notes"));
    g_free(html);
}

static void test_not_a_presentation(void) {
    GError* error = NULL;
    g_assert_null(pptx_file_to_html("tests/fixtures/preview/budget.xlsx", 100, NULL, &error));
    g_assert_nonnull(error);
    g_error_free(error);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/pptx/slides", test_slides);
    g_test_add_func("/pptx/not_a_presentation", test_not_a_presentation);
    return g_test_run();
}
