/* Unit tests for src/terminal/vt_screen.c: terminal screen model. */
#include "terminal/vt_screen.h"

#include <string.h>

static void feed(VtScreen* s, const gchar* text) {
    vt_screen_feed(s, text, strlen(text));
}

/* Row of the screen as text (trailing blanks dropped) */
static gchar* row_text(VtScreen* s, gint row) {
    return vt_screen_text(s, row, 0, row, vt_screen_cols(s));
}

static void assert_row(VtScreen* s, gint row, const gchar* expected) {
    gchar* text = row_text(s, row);
    g_assert_cmpstr(text, ==, expected);
    g_free(text);
}

static void assert_cursor(VtScreen* s, gint row, gint col) {
    gint r, c;
    vt_screen_cursor(s, &r, &c);
    g_assert_cmpint(r, ==, row);
    g_assert_cmpint(c, ==, col);
}

static void test_text_and_newlines(void) {
    VtScreen* s = vt_screen_new(20, 5, 100);
    feed(s, "hello\r\nworld");
    assert_row(s, 0, "hello");
    assert_row(s, 1, "world");
    assert_cursor(s, 1, 5);
    feed(s, "\rW");
    assert_row(s, 1, "World");
    vt_screen_free(s);
}

static void test_wrap_and_scrollback(void) {
    VtScreen* s = vt_screen_new(4, 2, 100);
    feed(s, "abcdef");
    assert_row(s, 0, "abcd");
    assert_row(s, 1, "ef");
    g_assert_true(vt_screen_line_wrapped(s, 0));
    gchar* joined = vt_screen_text(s, 0, 0, 1, 4);
    g_assert_cmpstr(joined, ==, "abcdef");
    g_free(joined);
    feed(s, "\r\nxy");
    g_assert_cmpint(vt_screen_scrollback(s), ==, 1);
    g_assert_cmpint(vt_screen_scrolled(s), ==, 1);
    assert_row(s, -1, "abcd");
    assert_row(s, 0, "ef");
    assert_row(s, 1, "xy");
    vt_screen_free(s);
}

static void test_pending_wrap(void) {
    /* The last column keeps the cursor there until the next character */
    VtScreen* s = vt_screen_new(3, 2, 0);
    feed(s, "abc");
    assert_cursor(s, 0, 2);
    feed(s, "\r\n");
    assert_cursor(s, 1, 0);
    assert_row(s, 1, "");
    vt_screen_free(s);
}

static void test_cursor_moves_and_erase(void) {
    VtScreen* s = vt_screen_new(10, 3, 0);
    feed(s, "0123456789\033[2;3Hxy");
    assert_row(s, 1, "  xy");
    feed(s, "\033[1;5H\033[K");
    assert_row(s, 0, "0123");
    feed(s, "\033[1;2H\033[1K");
    assert_row(s, 0, "  23");
    feed(s, "\033[2J");
    assert_row(s, 0, "");
    assert_row(s, 1, "");
    feed(s, "\033[3;10H\033[2D\033[Aq");
    assert_row(s, 1, "       q");
    vt_screen_free(s);
}

static void test_insert_delete_chars(void) {
    VtScreen* s = vt_screen_new(10, 2, 0);
    feed(s, "abcdef\033[1;3H\033[2@");
    assert_row(s, 0, "ab  cdef");
    feed(s, "\033[3P");
    assert_row(s, 0, "abdef");
    feed(s, "\033[2X");
    assert_row(s, 0, "ab  f");
    vt_screen_free(s);
}

static void test_sgr_colors(void) {
    VtScreen* s = vt_screen_new(10, 2, 0);
    feed(s, "\033[1;31ma\033[38;5;200mb\033[48;2;1;2;3mc\033[0md");
    const VtCell* cells = vt_screen_line(s, 0, NULL);
    g_assert_cmpuint(cells[0].fg, ==, VT_COLOR_INDEXED(1));
    g_assert_true(cells[0].attrs & VT_ATTR_BOLD);
    g_assert_cmpuint(cells[1].fg, ==, VT_COLOR_INDEXED(200));
    g_assert_cmpuint(cells[2].bg, ==, VT_COLOR_RGB(1, 2, 3));
    g_assert_cmpuint(cells[3].fg, ==, VT_COLOR_DEFAULT);
    g_assert_cmpuint(cells[3].attrs, ==, 0);
    feed(s, "\033[94m\033[7mx");
    cells = vt_screen_line(s, 0, NULL);
    g_assert_cmpuint(cells[4].fg, ==, VT_COLOR_INDEXED(12));
    g_assert_true(cells[4].attrs & VT_ATTR_INVERSE);
    vt_screen_free(s);
}

static void test_scroll_region(void) {
    VtScreen* s = vt_screen_new(5, 4, 100);
    feed(s, "a\r\nb\r\nc\r\nd");
    feed(s, "\033[2;3r\033[3;1H\n");
    assert_row(s, 0, "a");
    assert_row(s, 1, "c");
    assert_row(s, 2, "");
    assert_row(s, 3, "d");
    g_assert_cmpint(vt_screen_scrollback(s), ==, 0); /* not the full screen */
    feed(s, "\033[2;1H\033M");
    assert_row(s, 1, "");
    assert_row(s, 2, "c");
    vt_screen_free(s);
}

static void test_insert_delete_lines(void) {
    VtScreen* s = vt_screen_new(5, 3, 100);
    feed(s, "a\r\nb\r\nc\033[1;1H\033[L");
    assert_row(s, 0, "");
    assert_row(s, 1, "a");
    assert_row(s, 2, "b");
    feed(s, "\033[2M");
    assert_row(s, 0, "b");
    assert_row(s, 1, "");
    g_assert_cmpint(vt_screen_scrollback(s), ==, 0);
    vt_screen_free(s);
}

static void test_alternate_screen(void) {
    VtScreen* s = vt_screen_new(10, 3, 100);
    feed(s, "shell\033[?1049h");
    g_assert_true(vt_screen_modes(s) & VT_MODE_ALT_SCREEN);
    assert_row(s, 0, "");
    feed(s, "\033[Hvim");
    assert_row(s, 0, "vim");
    feed(s, "\033[?1049l");
    g_assert_false(vt_screen_modes(s) & VT_MODE_ALT_SCREEN);
    assert_row(s, 0, "shell");
    assert_cursor(s, 0, 5);
    vt_screen_free(s);
}

static void test_modes(void) {
    VtScreen* s = vt_screen_new(10, 3, 0);
    feed(s, "\033[?1h\033[?2004h\033[?25l");
    guint m = vt_screen_modes(s);
    g_assert_true(m & VT_MODE_APP_CURSOR);
    g_assert_true(m & VT_MODE_BRACKETED_PASTE);
    g_assert_true(m & VT_MODE_CURSOR_HIDDEN);
    feed(s, "\033[?1l\033[?25h");
    m = vt_screen_modes(s);
    g_assert_false(m & VT_MODE_APP_CURSOR);
    g_assert_false(m & VT_MODE_CURSOR_HIDDEN);
    vt_screen_free(s);
}

static void test_title(void) {
    VtScreen* s = vt_screen_new(10, 3, 0);
    guint changes = vt_screen_feed(s, "\033]0;my title\007x", 14);
    g_assert_true(changes & VT_CHANGE_TITLE);
    g_assert_cmpstr(vt_screen_title(s), ==, "my title");
    feed(s, "\033]2;other\033\\");
    g_assert_cmpstr(vt_screen_title(s), ==, "other");
    assert_row(s, 0, "x");
    g_assert_false(vt_screen_feed(s, "\033]2;other\007", 10) & VT_CHANGE_TITLE);
    vt_screen_free(s);
}

static void test_replies(void) {
    VtScreen* s = vt_screen_new(10, 5, 0);
    feed(s, "\033[3;4H\033[6n");
    gsize len = 0;
    gchar* reply = vt_screen_take_reply(s, &len);
    g_assert_cmpstr(reply, ==, "\033[3;4R");
    g_assert_cmpuint(len, ==, 6);
    g_free(reply);
    g_assert_null(vt_screen_take_reply(s, NULL));
    feed(s, "\033[c");
    reply = vt_screen_take_reply(s, NULL);
    g_assert_nonnull(reply);
    g_assert_true(g_str_has_prefix(reply, "\033[?"));
    g_free(reply);
    vt_screen_free(s);
}

static void test_utf8_split_and_wide(void) {
    VtScreen* s = vt_screen_new(10, 2, 0);
    const gchar* e = "\xc3\xa9"; /* é */
    vt_screen_feed(s, e, 1);
    vt_screen_feed(s, e + 1, 1);
    feed(s, "\xe6\x97\xa5x"); /* 日 (double width) then x */
    assert_row(s, 0, "\xc3\xa9\xe6\x97\xa5x");
    const VtCell* cells = vt_screen_line(s, 0, NULL);
    g_assert_cmpuint(cells[1].ch, ==, 0x65e5);
    g_assert_true(cells[2].attrs & VT_ATTR_WIDE_TAIL);
    g_assert_cmpuint(cells[3].ch, ==, 'x');
    assert_cursor(s, 0, 4);
    vt_screen_free(s);
}

static void test_line_drawing_charset(void) {
    VtScreen* s = vt_screen_new(10, 2, 0);
    feed(s, "\033(0lqk\033(Bq");
    assert_row(s, 0, "\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x90q");
    vt_screen_free(s);
}

static void test_tabs_and_backspace(void) {
    VtScreen* s = vt_screen_new(20, 2, 0);
    feed(s, "a\tb\bc");
    assert_row(s, 0, "a       c");
    vt_screen_free(s);
}

static void test_resize(void) {
    VtScreen* s = vt_screen_new(10, 4, 100);
    feed(s, "1\r\n2\r\n3\r\n4");
    vt_screen_resize(s, 5, 2);
    g_assert_cmpint(vt_screen_rows(s), ==, 2);
    assert_row(s, 0, "3");
    assert_row(s, 1, "4");
    assert_cursor(s, 1, 1);
    g_assert_cmpint(vt_screen_scrollback(s), ==, 2);
    vt_screen_resize(s, 5, 4);
    assert_row(s, 0, "1");
    assert_row(s, 3, "4");
    assert_cursor(s, 3, 1);
    g_assert_cmpint(vt_screen_scrollback(s), ==, 0);
    vt_screen_free(s);
}

static void test_scrollback_limit(void) {
    VtScreen* s = vt_screen_new(5, 1, 3);
    feed(s, "a\r\nb\r\nc\r\nd\r\ne");
    g_assert_cmpint(vt_screen_scrollback(s), ==, 3);
    assert_row(s, -3, "b");
    assert_row(s, -1, "d");
    assert_row(s, 0, "e");
    g_assert_null(vt_screen_line(s, -4, NULL));
    feed(s, "\033[3J");
    g_assert_cmpint(vt_screen_scrollback(s), ==, 0);
    vt_screen_free(s);
}

static void test_unknown_sequences_ignored(void) {
    VtScreen* s = vt_screen_new(10, 2, 0);
    feed(s, "\033[?9001h\033[?1004h\033P1$r\033\\\033[2 qok\033[>4;1m");
    assert_row(s, 0, "ok");
    g_assert_true(vt_screen_modes(s) & VT_MODE_FOCUS_EVENTS);
    vt_screen_free(s);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vt_screen/text_and_newlines", test_text_and_newlines);
    g_test_add_func("/vt_screen/wrap_and_scrollback", test_wrap_and_scrollback);
    g_test_add_func("/vt_screen/pending_wrap", test_pending_wrap);
    g_test_add_func("/vt_screen/cursor_moves_and_erase", test_cursor_moves_and_erase);
    g_test_add_func("/vt_screen/insert_delete_chars", test_insert_delete_chars);
    g_test_add_func("/vt_screen/sgr_colors", test_sgr_colors);
    g_test_add_func("/vt_screen/scroll_region", test_scroll_region);
    g_test_add_func("/vt_screen/insert_delete_lines", test_insert_delete_lines);
    g_test_add_func("/vt_screen/alternate_screen", test_alternate_screen);
    g_test_add_func("/vt_screen/modes", test_modes);
    g_test_add_func("/vt_screen/title", test_title);
    g_test_add_func("/vt_screen/replies", test_replies);
    g_test_add_func("/vt_screen/utf8_split_and_wide", test_utf8_split_and_wide);
    g_test_add_func("/vt_screen/line_drawing_charset", test_line_drawing_charset);
    g_test_add_func("/vt_screen/tabs_and_backspace", test_tabs_and_backspace);
    g_test_add_func("/vt_screen/resize", test_resize);
    g_test_add_func("/vt_screen/scrollback_limit", test_scrollback_limit);
    g_test_add_func("/vt_screen/unknown_sequences_ignored", test_unknown_sequences_ignored);
    return g_test_run();
}
