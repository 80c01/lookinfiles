/* Unit tests for src/office/rtf.c (RTF to HTML). */
#include "office/rtf.h"

#include <string.h>

static void test_fixture(void) {
    gchar* text = NULL;
    gsize len = 0;
    g_assert_true(g_file_get_contents("tests/fixtures/preview/notes.rtf", &text, &len, NULL));
    GError* error = NULL;
    gchar* html = rtf_to_html(text, len, 100000, &error);
    g_assert_no_error(error);
    g_assert_nonnull(strstr(html, "<title>Meeting notes</title><meta name=\"author\" content=\"Ada Lovelace\">"));
    g_assert_nonnull(strstr(html, "<p><b>Meeting notes</b></p>"));
    g_assert_nonnull(strstr(html, "Discussed the <i>new</i> preview, the <u>budget</u> and <s>old</s> items.</p>"));
    g_assert_nonnull(strstr(html, "Café crème — “quoted”<br>next line</p>"));
    g_assert_nonnull(strstr(html, "<table>\n<tr><td>Item</td><td>Cost</td></tr>\n<tr><td>Laptop</td><td>1200</td></tr>\n</table>\n<p>After the table.</p>"));
    g_assert_nonnull(strstr(html, "<img src=\"data:image/png;base64,iVBORw0KGgo"));
    g_assert_null(strstr(html, "Helvetica"));
    g_assert_null(strstr(html, "Hidden Corp"));
    g_assert_null(strstr(html, "footnote is hidden"));
    g_assert_null(strstr(html, "Riched20"));
    g_free(html);
    g_free(text);
}

static void test_escapes_and_limits(void) {
    const gchar* rtf = "{\\rtf1 a\\{b\\}c\\\\d\\~e{\\b bold}f\\par}";
    gchar* html = rtf_to_html(rtf, strlen(rtf), 1000, NULL);
    g_assert_nonnull(strstr(html, "<p>a{b}c\\d\xC2\xA0" "e<b>bold</b>f</p>"));
    g_free(html);
    gchar* cut = rtf_to_html("{\\rtf1 0123456789 0123456789\\par more\\par}", 41, 5, NULL);
    g_assert_nonnull(strstr(cut, "Document truncated"));
    g_assert_null(strstr(cut, "more"));
    g_free(cut);
    GError* error = NULL;
    g_assert_null(rtf_to_html("hello", 5, 100, &error));
    g_assert_nonnull(error);
    g_error_free(error);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/rtf/fixture", test_fixture);
    g_test_add_func("/rtf/escapes_and_limits", test_escapes_and_limits);
    return g_test_run();
}
