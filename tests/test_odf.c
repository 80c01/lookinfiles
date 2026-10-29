/* Unit tests for src/odf.c (OpenDocument text and presentations to HTML). */
#include "odf.h"

#include <string.h>

static void test_text(void) {
    OdfInfo info = { 0 };
    GError* error = NULL;
    gchar* html = odf_file_to_html("tests/fixtures/preview/report.odt", 100000, &info, &error);
    g_assert_no_error(error);
    g_assert_false(info.presentation);
    g_assert_nonnull(strstr(html, "<title>Q3 report</title><meta name=\"author\" content=\"Ada Lovelace\">"));
    g_assert_nonnull(strstr(html, "<h1>Quarterly report</h1>"));     /* Title style */
    g_assert_nonnull(strstr(html, "<h1>Summary</h1>"));
    g_assert_nonnull(strstr(html, "Sales grew <i>steadily</i> and the <b><u>new office</u></b> opened.</p>"));
    g_assert_null(strstr(html, "hidden footnote"));
    g_assert_nonnull(strstr(html, "<ol>\n<li>Hire two people </li>"));
    g_assert_nonnull(strstr(html, "<a href=\"https://example.org\">version 2</a>"));
    g_assert_nonnull(strstr(html, "<th>Quarter"));
    g_assert_nonnull(strstr(html, "<td>"));
    g_assert_nonnull(strstr(html, "<img src=\"data:image/png;base64,"));
    g_assert_nonnull(strstr(html, "Line one<br>line two"));
    g_free(html);
}

static void test_truncated(void) {
    gchar* html = odf_file_to_html("tests/fixtures/preview/report.odt", 20, NULL, NULL);
    g_assert_nonnull(strstr(html, "Document truncated"));
    g_assert_null(strstr(html, "version 2"));
    g_free(html);
}

static void test_presentation(void) {
    OdfInfo info = { 0 };
    gchar* html = odf_file_to_html("tests/fixtures/preview/roadmap.odp", 100000, &info, NULL);
    g_assert_true(info.presentation);
    g_assert_cmpuint(info.n_slides, ==, 2);
    g_assert_nonnull(strstr(html, "<h2>Slide 1</h2>\n<h3>Lookin roadmap</h3>"));
    g_assert_nonnull(strstr(html, "<hr>\n<h2>Slide 2</h2>"));
    g_assert_nonnull(strstr(html, "<li>Archives </li>"));
    g_assert_null(strstr(html, "Speaker notes"));
    g_free(html);
}

static void test_not_odf(void) {
    GError* error = NULL;
    g_assert_null(odf_file_to_html("tests/fixtures/preview/sample.zip", 100, NULL, &error));
    g_assert_nonnull(error);
    g_error_free(error);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/odf/text", test_text);
    g_test_add_func("/odf/truncated", test_truncated);
    g_test_add_func("/odf/presentation", test_presentation);
    g_test_add_func("/odf/not_odf", test_not_odf);
    return g_test_run();
}
