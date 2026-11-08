/* Unit tests for src/vim/vim_regex.c (Vim patterns on GRegex) and the text
 * objects of src/vim/vim_motion.c */
#include "vim/vim_regex.h"
#include "vim/vim_motion.h"

#include <string.h>

static void check_translate(const gchar* vim, const gchar* pcre) {
    gboolean caseless = FALSE;
    gchar* got = vim_regex_translate(vim, &caseless);
    g_assert_cmpstr(got, ==, pcre);
    g_free(got);
}

static void test_translate(void) {
    check_translate("foo", "foo");
    check_translate("a.c", "a.c");
    check_translate("a\\.c", "a\\.c");
    check_translate("(x)", "\\(x\\)");
    check_translate("\\(x\\)", "(x)");
    check_translate("a\\|b", "a|b");
    check_translate("a|b", "a\\|b");
    check_translate("a\\+", "a+");
    check_translate("a+", "a\\+");
    check_translate("a\\=", "a?");
    check_translate("a\\{2,3}", "a{2,3}");
    check_translate("a\\{-}", "a*?");
    check_translate("a\\{,3}", "a{0,3}");
    check_translate("\\<foo\\>", "\\b(?=\\w)foo\\b(?<=\\w)");
    check_translate("\\vfoo(bar)+", "foo(bar)+");
    check_translate("\\v<x>", "\\b(?=\\w)x\\b(?<=\\w)");
    check_translate("\\Va.b*", "a\\.b\\*");
    check_translate("\\d\\+", "\\d+");
    check_translate("[abc]x", "[abc]x");
    check_translate("[^\\]]", "[^\\]]");
    check_translate("foo\\zsbar", "foo\\Kbar");
    check_translate("foo\\zebar", "foo(?=bar)");
    check_translate("\\%(a\\|b\\)", "(?:a|b)");
    check_translate("a/b", "a\\/b");
    check_translate("\\a\\u", "[A-Za-z][A-Z]");
    gboolean caseless = FALSE;
    gchar* t = vim_regex_translate("\\cfoo", &caseless);
    g_assert_true(caseless);
    g_free(t);
}

static void test_compile_case(void) {
    GRegex* re = vim_regex_compile("foo", TRUE, TRUE, NULL);
    g_assert_true(g_regex_match(re, "FOO", 0, NULL));
    g_regex_unref(re);
    re = vim_regex_compile("Foo", TRUE, TRUE, NULL);
    g_assert_false(g_regex_match(re, "FOO", 0, NULL));
    g_regex_unref(re);
    re = vim_regex_compile("\\CFoo", TRUE, FALSE, NULL);
    g_assert_false(g_regex_match(re, "FOO", 0, NULL));
    g_regex_unref(re);
    re = vim_regex_compile("^b", FALSE, FALSE, NULL);
    g_assert_true(g_regex_match(re, "a\nb", 0, NULL));
    g_regex_unref(re);
}

static gchar* expand(const gchar* pattern, const gchar* subject, const gchar* rep) {
    GRegex* re = vim_regex_compile(pattern, FALSE, FALSE, NULL);
    GMatchInfo* mi = NULL;
    g_assert_true(g_regex_match(re, subject, 0, &mi));
    gchar* out = vim_regex_expand(rep, mi);
    g_match_info_free(mi);
    g_regex_unref(re);
    return out;
}

static void test_expand(void) {
    gchar* s = expand("\\(a\\)\\(b\\)", "ab", "\\2\\1&");
    g_assert_cmpstr(s, ==, "baab");
    g_free(s);
    s = expand("foo", "foo", "\\U&\\E!");
    g_assert_cmpstr(s, ==, "FOO!");
    g_free(s);
    s = expand("foo", "foo", "\\u&");
    g_assert_cmpstr(s, ==, "Foo");
    g_free(s);
    s = expand("x", "x", "a\\rb\\&");
    g_assert_cmpstr(s, ==, "a\nb&");
    g_free(s);
}

static void test_search_buffer(void) {
    GtkTextBuffer* buf = gtk_text_buffer_new(NULL);
    gtk_text_buffer_set_text(buf, "é foo\nbar foo", -1);
    GRegex* re = vim_regex_compile("foo", FALSE, FALSE, NULL);
    GtkTextIter from, ms, me;
    gtk_text_buffer_get_start_iter(buf, &from);
    gboolean wrapped = TRUE;
    g_assert_true(vim_regex_search(buf, re, &from, TRUE, TRUE, &wrapped, &ms, &me));
    g_assert_cmpint(gtk_text_iter_get_offset(&ms), ==, 2);
    g_assert_cmpint(gtk_text_iter_get_offset(&me), ==, 5);
    g_assert_false(wrapped);
    from = ms;
    g_assert_true(vim_regex_search(buf, re, &from, TRUE, TRUE, &wrapped, &ms, &me));
    g_assert_cmpint(gtk_text_iter_get_offset(&ms), ==, 10);
    from = ms;
    g_assert_true(vim_regex_search(buf, re, &from, TRUE, TRUE, &wrapped, &ms, &me));
    g_assert_true(wrapped);
    g_assert_cmpint(gtk_text_iter_get_offset(&ms), ==, 2);
    g_assert_false(vim_regex_search(buf, re, &ms, FALSE, FALSE, &wrapped, &ms, &me));
    gint index = 0;
    g_assert_cmpint(vim_regex_count(buf, re, &ms, &index), ==, 2);
    g_assert_cmpint(index, ==, 1);
    g_regex_unref(re);
    g_object_unref(buf);
}

/* Text object on text with '|' at the cursor: the selected text */
static gchar* textobj(const gchar* text, gunichar obj, gboolean around, gint count) {
    GtkTextBuffer* buf = gtk_text_buffer_new(NULL);
    const gchar* bar = strchr(text, '|');
    GString* s = g_string_new(text);
    gint cur = (gint)g_utf8_pointer_to_offset(text, bar);
    g_string_erase(s, bar - text, 1);
    gtk_text_buffer_set_text(buf, s->str, -1);
    g_string_free(s, TRUE);
    GtkTextIter a, b;
    gtk_text_buffer_get_iter_at_offset(buf, &a, cur);
    b = a;
    gboolean lw = FALSE;
    gchar* out = NULL;
    if (vim_textobj(buf, obj, around, count, &a, &b, &lw)) out = gtk_text_iter_get_slice(&a, &b);
    g_object_unref(buf);
    return out;
}

#define ASSERT_OBJ(text, obj, around, count, expected) \
    do {                                                \
        gchar* got = textobj(text, obj, around, count); \
        g_assert_cmpstr(got, ==, expected);             \
        g_free(got);                                    \
    } while (0)

static void test_textobjects(void) {
    ASSERT_OBJ("foo b|ar baz", 'w', FALSE, 1, "bar");
    ASSERT_OBJ("foo b|ar baz", 'w', TRUE, 1, "bar ");
    ASSERT_OBJ("foo b|ar", 'w', TRUE, 1, " bar");
    ASSERT_OBJ("foo b|ar baz", 'w', FALSE, 3, "bar baz");
    ASSERT_OBJ("a.b|c.d", 'W', FALSE, 1, "a.bc.d");
    ASSERT_OBJ("x \"a|b\" y", '"', FALSE, 1, "ab");
    ASSERT_OBJ("x \"a|b\" y", '"', TRUE, 1, "\"ab\" ");
    ASSERT_OBJ("|x \"ab\" y", '"', FALSE, 1, "ab");
    ASSERT_OBJ("x 'a\\'|b'", '\'', FALSE, 1, "a\\'b");
    ASSERT_OBJ("f(a, (b|), c)", '(', FALSE, 1, "b");
    ASSERT_OBJ("f(a, (b|), c)", '(', FALSE, 2, "a, (b), c");
    ASSERT_OBJ("f(a, (b|), c)", 'b', TRUE, 1, "(b)");
    ASSERT_OBJ("|(a)", '(', FALSE, 1, "a");
    ASSERT_OBJ("(a|)", ')', TRUE, 1, "(a)");
    ASSERT_OBJ("x [1, |2]", '[', FALSE, 1, "1, 2");
    ASSERT_OBJ("<div>a<b>|c</b></div>", 't', FALSE, 1, "c");
    ASSERT_OBJ("<div>a<b>|c</b></div>", 't', FALSE, 2, "a<b>c</b>");
    ASSERT_OBJ("<div>a<b>c</b>|</div>", 't', TRUE, 1, "<div>a<b>c</b></div>");
    ASSERT_OBJ("a\n|b\n\nc", 'p', FALSE, 1, "a\nb\n");
    ASSERT_OBJ("a\n|b\n\nc", 'p', TRUE, 1, "a\nb\n\n");
    ASSERT_OBJ("One. T|wo. Three.", 's', FALSE, 1, "Two.");
    ASSERT_OBJ("One. T|wo. Three.", 's', TRUE, 1, "Two. ");
    ASSERT_OBJ("x (|", '(', FALSE, 1, NULL);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vim_regex/translate", test_translate);
    g_test_add_func("/vim_regex/compile_case", test_compile_case);
    g_test_add_func("/vim_regex/expand", test_expand);
    g_test_add_func("/vim_regex/search_buffer", test_search_buffer);
    g_test_add_func("/vim_motion/textobjects", test_textobjects);
    return g_test_run();
}
