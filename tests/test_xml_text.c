/* Unit tests for src/core/xml_text.c: the visible text of an XML line. */
#include "core/xml_text.h"

#include <string.h>

/* The text of line with the match of needle (its first occurrence) moved
 * onto it; checks that the match still shows needle_text */
static gchar* clean(const gchar* line, const gchar* needle, const gchar* needle_text) {
    const gchar* at = strstr(line, needle);
    g_assert_nonnull(at);
    gint column = (gint)(at - line);
    gint length = (gint)strlen(needle);
    gchar* text = xml_text_line(line, &column, &length);
    if (text != NULL) {
        g_assert_cmpint(length, ==, (gint)strlen(needle_text));
        g_assert_cmpint(strncmp(text + column, needle_text, (gsize)length), ==, 0);
    }
    return text;
}

static void test_plain(void) {
    gchar* t = clean("just words", "words", "words");
    g_assert_cmpstr(t, ==, "just words");
    g_free(t);
}

static void test_tags(void) {
    gchar* t = clean("  <text id=\"2\" x=\"60\">First <span s=\"b\">idea</span>", "idea", "idea");
    g_assert_cmpstr(t, ==, "  First idea");
    g_free(t);
    /* The second line of an element's text */
    t = clean("second line</text>", "line", "line");
    g_assert_cmpstr(t, ==, "second line");
    g_free(t);
    /* The end of a tag begun on the line before */
    t = clean(" w=\"3\">tail", "tail", "tail");
    g_assert_cmpstr(t, ==, "tail");
    g_free(t);
}

static void test_markup_hits(void) {
    /* In an attribute, a tag name, or across a tag */
    g_assert_null(clean("  <shape fill=\"none\"/>", "fill", ""));
    g_assert_null(clean("  <text x=\"1\">a</text>", "text", ""));
    g_assert_null(clean("First <span s=\"b\">idea", "First <span", ""));
    /* Inside an entity */
    g_assert_null(clean("a &amp; b", "amp", ""));
}

static void test_entities(void) {
    gchar* t = clean("&lt;tag&gt; &amp; &#233;t&#xE9; word", "word", "word");
    g_assert_cmpstr(t, ==, "<tag> & \xc3\xa9t\xc3\xa9 word");
    g_free(t);
    /* Not an entity: kept as is */
    t = clean("R&D team", "team", "team");
    g_assert_cmpstr(t, ==, "R&D team");
    g_free(t);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xml_text/plain", test_plain);
    g_test_add_func("/xml_text/tags", test_tags);
    g_test_add_func("/xml_text/markup_hits", test_markup_hits);
    g_test_add_func("/xml_text/entities", test_entities);
    return g_test_run();
}
