/* Unit tests for src/html_text.c: entities, attributes, tag stripping */
#include <string.h>
#include "html_text.h"

/* Decode every entity of s */
static gchar* decode(const gchar* s) {
    GString* out = g_string_new(NULL);
    const gchar* p = s;
    while (*p != '\0') {
        if (*p == '&') {
            html_text_append_entity(out, &p);
        } else {
            g_string_append_c(out, *p++);
        }
    }
    return g_string_free(out, FALSE);
}

static void check_decode(const gchar* in, const gchar* expected) {
    gchar* got = decode(in);
    g_assert_cmpstr(got, ==, expected);
    g_free(got);
}

static void test_entities(void) {
    check_decode("a &amp; b &lt;c&gt;", "a & b <c>");
    check_decode("&quot;x&apos;", "\"x'");
    check_decode("&AMP;", "&");                    /* names ignore case */
    check_decode("caf&#233; &#xE9;&#XE9;", "café éé");
    check_decode("wait&hellip; &rarr; &alpha;", "wait… → α");
    check_decode("&Delta;&delta;", "Δδ");               /* names are case-sensitive */
    check_decode("&Dagger;&dagger; &rArr;&rarr;", "‡† ⇒→");
    check_decode("&unknown; stays", "&unknown; stays");
    check_decode("R&D no semicolon", "R&D no semicolon");
    check_decode("&#0; &#xD800;", "&#0; &#xD800;"); /* not a character */
    check_decode("&averyveryverylongname;", "&averyveryverylongname;");
}

static void check_attr(const gchar* tag, const gchar* name, const gchar* expected) {
    gchar* got = html_text_attr(tag, name);
    g_assert_cmpstr(got, ==, expected);
    g_free(got);
}

static void test_attr(void) {
    check_attr("<a href=\"x.html\" title='T'>", "href", "x.html");
    check_attr("<a href=\"x.html\" title='T'>", "title", "T");
    check_attr("<img SRC=pic.png width=10>", "src", "pic.png");
    check_attr("<img src=pic.png/>", "src", "pic.png");
    check_attr("<input value = \"v\">", "value", "v");
    check_attr("<a data-href=\"no\" href=\"yes\">", "href", "yes"); /* whole name only */
    check_attr("<a href=\"x\">", "title", NULL);
    check_attr("<br>", "class", NULL);
    check_attr(NULL, "href", NULL);
}

static void test_find_case(void) {
    const gchar* s = "abc <TITLE>x</title>";
    g_assert_true(html_text_find_case(s, "<title>", NULL) == s + 4);
    g_assert_true(html_text_find_case(s, "</TITLE>", NULL) == s + 12);
    g_assert_null(html_text_find_case(s, "<title>", s + 8)); /* would end past limit */
    g_assert_null(html_text_find_case(s, "<body", NULL));
    g_assert_null(html_text_find_case(NULL, "x", NULL));
}

static void check_strip(const gchar* in, const gchar* expected) {
    gchar* got = html_text_strip(in);
    g_assert_cmpstr(got, ==, expected);
    g_free(got);
}

static void test_strip(void) {
    check_strip("<b>Bold</b> and <i>it&amp;alic</i>", "Bold and it&alic");
    check_strip("  lots \n\t of   space  ", "lots of space");
    check_strip("<p>a</p><p>b</p>", "ab");
    check_strip("unclosed <tag", "unclosed tag");
    check_strip(NULL, "");
}

static void check_lang(const gchar* tag, const gchar* expected) {
    gchar* got = html_text_code_lang(tag);
    g_assert_cmpstr(got, ==, expected);
    g_free(got);
}

static void test_code_lang(void) {
    check_lang("<code class=\"language-python\">", "python");
    check_lang("<code class=\"hljs lang-c extra\">", "c");
    check_lang("<code class=\"plain\">", NULL);
    check_lang("<code>", NULL);
}

static void check_meta(const gchar* html, const gchar* name, const gchar* expected) {
    gchar* got = html_text_meta(html, name);
    g_assert_cmpstr(got, ==, expected);
    g_free(got);
}

static void test_meta(void) {
    const gchar* html =
        "<head><META name=\"Author\" content=\"Ada\">"
        "<meta property=\"og:title\" content=\"Hello\"/>"
        "<meta charset=\"utf-8\"></head>";
    check_meta(html, "author", "Ada");
    check_meta(html, "og:title", "Hello");
    check_meta(html, "description", NULL);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/html_text/entities", test_entities);
    g_test_add_func("/html_text/attr", test_attr);
    g_test_add_func("/html_text/find_case", test_find_case);
    g_test_add_func("/html_text/strip", test_strip);
    g_test_add_func("/html_text/code_lang", test_code_lang);
    g_test_add_func("/html_text/meta", test_meta);
    return g_test_run();
}
