/* Unit tests for the Vim engine (src/vim_engine.c, vim_engine_ex.c) on a
 * headless GtkTextBuffer. Texts mark the cursor with '|'. Keys use Vim
 * notation (<Esc>, <CR>, <C-v>...) and are typed "live": keys the engine
 * leaves to the text view are inserted the way GtkTextView would. */
#include "vim_engine.h"
#include "vim_keys.h"

#include <string.h>

typedef struct {
    GString* messages;
    GString* clipboard;
    GString* ex;
    gint first, last; /* visible lines */
} Host;

static void host_message(gpointer data, const gchar* text, gboolean error) {
    Host* h = data;
    g_string_append_printf(h->messages, "%s%s\n", error ? "E:" : "", text);
}

static gboolean host_ex(gpointer data, const gchar* name, gboolean bang, const gchar* args, gchar** error) {
    Host* h = data;
    (void)error;
    g_string_append_printf(h->ex, "%s%s %s;", name, bang ? "!" : "", args);
    return strcmp(name, "set") != 0 || strcmp(args, "number") == 0;
}

static gboolean host_visible(gpointer data, gint* first, gint* last) {
    Host* h = data;
    *first = h->first;
    *last = h->last;
    return TRUE;
}

static void host_clip(gpointer data, const gchar* text) {
    Host* h = data;
    g_string_assign(h->clipboard, text);
}

static const gchar* host_file(gpointer data) {
    (void)data;
    return "test.txt";
}

typedef struct {
    GtkTextBuffer* buf;
    VimEngine* e;
    Host host;
} Fixture;

static void fx_set(Fixture* f, const gchar* text) {
    const gchar* bar = strchr(text, '|');
    GString* s = g_string_new(text);
    gint cursor = 0;
    if (bar != NULL) {
        cursor = (gint)g_utf8_pointer_to_offset(text, bar);
        g_string_erase(s, bar - text, 1);
    }
    gtk_text_buffer_set_text(f->buf, s->str, -1);
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_offset(f->buf, &it, cursor);
    gtk_text_buffer_place_cursor(f->buf, &it);
    g_string_free(s, TRUE);
}

static Fixture* fx_new(const gchar* text) {
    Fixture* f = g_new0(Fixture, 1);
    f->buf = gtk_text_buffer_new(NULL);
    gtk_text_buffer_set_enable_undo(f->buf, TRUE);
    f->host.messages = g_string_new(NULL);
    f->host.clipboard = g_string_new(NULL);
    f->host.ex = g_string_new(NULL);
    f->host.first = 0;
    f->host.last = 9;
    VimHost h = {0};
    h.message = host_message;
    h.ex_command = host_ex;
    h.visible_lines = host_visible;
    h.clipboard_set = host_clip;
    h.file_name = host_file;
    fx_set(f, text);
    /* set_text must not be an undo step of the test */
    gtk_text_buffer_set_enable_undo(f->buf, FALSE);
    gtk_text_buffer_set_enable_undo(f->buf, TRUE);
    f->e = vim_engine_new(f->buf, &h, &f->host);
    return f;
}

static void fx_free(Fixture* f) {
    vim_engine_free(f->e);
    g_object_unref(f->buf);
    g_string_free(f->host.messages, TRUE);
    g_string_free(f->host.clipboard, TRUE);
    g_string_free(f->host.ex, TRUE);
    g_free(f);
}

/* Type keys like a user: what the engine declines, the text view does */
static void fx_type(Fixture* f, const gchar* notation) {
    gchar* keys = vim_keys_from_notation(notation);
    for (const gchar* p = keys; *p; p = g_utf8_next_char(p)) {
        gunichar k = g_utf8_get_char(p);
        if (vim_engine_feed(f->e, k)) continue;
        if (k == VIM_KEY_BS) {
            GtkTextIter it;
            gtk_text_buffer_get_iter_at_mark(f->buf, &it, gtk_text_buffer_get_insert(f->buf));
            gtk_text_buffer_backspace(f->buf, &it, TRUE, TRUE);
        } else if (k == VIM_KEY_DEL) {
            GtkTextIter s, t;
            gtk_text_buffer_get_iter_at_mark(f->buf, &s, gtk_text_buffer_get_insert(f->buf));
            t = s;
            gtk_text_iter_forward_char(&t);
            gtk_text_buffer_delete_interactive(f->buf, &s, &t, TRUE);
        } else if (k == VIM_KEY_TAB) {
            gtk_text_buffer_insert_interactive_at_cursor(f->buf, "\t", -1, TRUE);
        } else if (!VIM_KEY_IS_SPECIAL(k)) {
            gchar buf[8] = {0};
            g_unichar_to_utf8(k, buf);
            gtk_text_buffer_begin_user_action(f->buf);
            gtk_text_buffer_insert_interactive_at_cursor(f->buf, buf, -1, TRUE);
            gtk_text_buffer_end_user_action(f->buf);
        }
    }
    g_free(keys);
}

static gchar* fx_text(Fixture* f, gboolean with_cursor) {
    GtkTextIter s, t, c;
    gtk_text_buffer_get_bounds(f->buf, &s, &t);
    gchar* text = gtk_text_buffer_get_text(f->buf, &s, &t, TRUE);
    if (!with_cursor) return text;
    vim_engine_get_cursor(f->e, &c);
    GString* out = g_string_new(text);
    gchar* at = g_utf8_offset_to_pointer(text, gtk_text_iter_get_offset(&c));
    g_string_insert_c(out, at - text, '|');
    g_free(text);
    return g_string_free(out, FALSE);
}

/* start text, keys, expected text with the cursor */
static void check(const gchar* start, const gchar* keys, const gchar* expected) {
    Fixture* f = fx_new(start);
    fx_type(f, keys);
    gchar* got = fx_text(f, strchr(expected, '|') != NULL);
    if (strcmp(got, expected) != 0) {
        /* Report and go on, so one run lists every failing case */
        g_printerr("\nstart:    \"%s\"\nkeys:     %s\nexpected: \"%s\"\ngot:      \"%s\"\nmessages: %s\n",
                   start, keys, expected, got, f->host.messages->str);
        g_test_fail();
    }
    g_free(got);
    fx_free(f);
}

/* ------------------------------------------------------------------------- */

static void test_motions(void) {
    check("|foo.bar baz", "w", "foo|.bar baz");
    check("|foo.bar baz", "W", "foo.bar |baz");
    check("|foo.bar baz", "e", "fo|o.bar baz");
    check("|foo.bar baz", "E", "foo.ba|r baz");
    check("foo.bar |baz", "b", "foo.|bar baz");
    check("foo.bar |baz", "B", "|foo.bar baz");
    check("foo bar |baz", "ge", "foo ba|r baz");
    check("|foo\n\nbar", "w", "foo\n|\nbar");
    check("|foo\n\nbar", "ww", "foo\n\n|bar");
    check("foo\n\n|bar", "b", "foo\n|\nbar");
    check("  foo |bar", "0", "|  foo bar");
    check("  foo |bar", "^", "  |foo bar");
    check("|foo bar", "$", "foo ba|r");
    check("|a,b,c,d", "f,;", "a,b|,c,d");
    check("|a,b,c,d", "2f,", "a,b|,c,d");
    check("|a,b,c", "t,;", "a,|b,c");
    check("a,b,c|,d", "F,,", "a,b,c|,d");
    check("|(a [b] c)", "%", "(a [b] c|)");
    check("(a [b] |c)", "%", "|(a [b] c)");
    check("|x (a)", "%", "x (a|)");
    check("|a\nb\nc\nd", "G", "a\nb\nc\n|d");
    check("a\nb\nc\n|d", "gg", "|a\nb\nc\nd");
    check("|a\nb\nc\nd", "3G", "a\nb\n|c\nd");
    check("|a\nb\n\nc\nd", "}", "a\nb\n|\nc\nd");
    check("a\nb\n\nc\n|d", "{", "a\nb\n|\nc\nd");
    check("|abcdef\nab\nabcdef", "$jj", "abcdef\nab\nabcde|f");
    check("abc|def\nab\nabcdef", "jj", "abcdef\nab\nabc|def");
    check("|one\n  two", "+", "one\n  |two");
    check("|abcdef", "4|", "abc|def");
    check("|one\ntwo\nthree", "L", "one\ntwo\n|three");
    check("one\ntwo\n|three", "H", "|one\ntwo\nthree");
    check("|One. Two. Three.", ")", "One. |Two. Three.");
    check("One. Two. |Three.", "(", "One. |Two. Three.");
    check("|abc", "5l", "ab|c");
}

static void test_delete_change(void) {
    check("|hello world", "dw", "|world");
    check("hello |world", "daw", "hell|o");
    check("foo |bar baz", "daw", "foo |baz");
    check("foo |bar\nbaz", "dw", "foo| \nbaz");
    check("foo(a, |b)", "ci(x<Esc>", "foo(|x)");
    check("f(|abc)", "dt)", "f(|)");
    check("a |b c", "x", "a | c");
    check("ab|c", "x", "a|b");
    check("|abcdef", "3x", "|def");
    check("abc|def", "X", "ab|def");
    check("one\n|two\nthree", "dd", "one\n|three");
    check("one\n|two", "dd", "|one");
    check("|a\nb\nc\nd", "2dd", "|c\nd");
    check("|a\nb\nc\nd", "dj", "|c\nd");
    check("a\nb\n|c\nd", "dk", "a\n|d");
    check("a\n|b\nc", "dG", "|a");
    check("a\n|b\nc", "dgg", "|c");
    check("|a\nb\n\nc", "d}", "|\nc");
    check("|foo bar", "D", "");
    check("ab|cd", "D", "a|b");
    check("|foo bar", "cwx<Esc>", "|x bar");
    check("fo|o bar", "cwx<Esc>", "fo|x bar");
    check("|foo bar", "cWx<Esc>", "|x bar");
    check("  |foo", "ccbar<Esc>", "  ba|r");
    check("ab|cd", "Cx<Esc>", "ab|x");
    check("|abc", "sx<Esc>", "|xbc");
    check("  ab|c", "Sx<Esc>", "  |x");
    check("say \"he|llo\" now", "di\"", "say \"|\" now");
    check("x \"|a\" y", "da\"", "x |y");
    check("<a><b>|x</b></a>", "dit", "<a><b>|</b></a>");
    check("<a><b>|x</b></a>", "d2it", "<a>|</a>");
    check("<a><b>|x</b></a>", "dat", "<a>|</a>");
    check("|a\nb\n\nc", "dap", "|c");
    check("|a\nb\n\nc", "dip", "|\nc");
    check("f {\n  |x\n}", "di{", "f {\n|}");
    check("f {\n  |x\n}", "ci{y<Esc>", "f {\n  |y\n}");
    check("|abc foo", "d/foo<CR>", "|foo");
    check("a|bc", "d0", "|bc");
    check("|hello world", "d3l", "|lo world");
    check("|one two three", "d2w", "|three");
    check("|one two three", "2dw", "|three");
}

static void test_insert(void) {
    check("|", "ihello<Esc>", "hell|o");
    check("|", "3ix<Esc>", "xx|x");
    check("|ab", "Ac<Esc>", "ab|c");
    check("  |ab", "Ix<Esc>", "  |xab");
    check("|ab", "ax<Esc>", "a|xb");
    check("    |foo", "obar<Esc>", "    foo\n    ba|r");
    check("    |foo", "Obar<Esc>", "    ba|r\n    foo");
    check("|a", "2ob<Esc>", "a\nb\n|b");
    check("|", "ifoo bar<C-w><Esc>", "foo| ");
    check("|", "ifoo<CR>bar<Esc>", "foo\nba|r");
    check("  |", "ax<CR>y<Esc>", "  x\n  |y");
    check("|", "iabc<BS><Esc>", "a|b");
    check("|x", "yla<C-r>\"<Esc>", "x|x");
    check("|abc", "Rxy<Esc>", "x|yc");
    check("|abc", "Rxy<BS><Esc>", "|xbc");
    check("|ab", "Rxyz<Esc>", "xy|z");
    check("|", "i<Tab>x<Esc>", "    |x");
    check("|foo", "A<C-t><Esc>", "    fo|o");
    check("|abcd\nxyz", "jA<C-y><Esc>", "abcd\nxyz|d");
    check("|", "ifoo foobar f<C-n><Esc>", "foo foobar fo|o");
    check("|", "ifoo foobar f<C-n><C-n><Esc>", "foo foobar fooba|r");
    check("|ab", "i<C-v><Tab><Esc>", "|\tab");
    check("|abc", "ix<C-o>$y<Esc>", "xabc|y");
}

static void test_dot_repeat(void) {
    check("|foo bar baz", "dw..", "|");
    check("|foo foo", "ciwbar<Esc>w.", "bar ba|r");
    check("|a b c d", "dw2.", "|d");
    check("|x\nx\nx", "Ay<Esc>j.j.", "xy\nxy\nx|y");
    check("|a\nb", "ddp", "b\n|a");
    check("|aaaa\nbbbb", "vlldj.", "a\n|b");
    check("|ab\nab", "ix<Esc>j0.", "xab\n|xab");
    check("|one\ntwo", ">>j.", "    one\n    |two");
    check("|x1 x2 x3", "rzw.w.", "z1 z2 |z3");
    check("|ab", "3.", "|ab");
    check("|foo bar", "d/bar<CR>u.", "|bar");
}

static void test_yank_put(void) {
    check("|one\ntwo", "yyp", "one\n|one\ntwo");
    check("|one\ntwo", "yyP", "|one\none\ntwo");
    check("|abc", "ylp", "a|abc");
    check("|abc", "ylP", "|aabc");
    check("|ab", "xp", "b|a");
    check("|one\ntwo", "\"ayyj\"ap", "one\ntwo\n|one");
    check("|one\ntwo", "\"ayyj\"Ayy\"aP", "one\n|one\ntwo\ntwo");
    check("|foo bar", "yiwwviwp", "foo fo|o");
    check("|foo bar", "yiwwviwp0viwp", "ba|r foo");
    check("|a\nb", "yy3p", "a\n|a\na\na\nb");
    check("|abc", "yl3p", "aaa|abc");
    check("|one", "Yp", "one\n|one");
    check("|abc\nabc", "<C-v>jly$p", "abcab\nabcab");
    check("|one\ntwo\nthree", "dd\"1p", "two\n|one\nthree");
    check("|a b", "dwx\"-p", "|b");
    check("|ab", "yl\"_dd\"0p", "a");
}

static void test_registers_clipboard(void) {
    Fixture* f = fx_new("|hello world");
    fx_type(f, "yiw");
    g_assert_cmpstr(f->host.clipboard->str, ==, "hello");
    g_assert_cmpstr(vim_engine_register(f->e, '0', NULL), ==, "hello");
    fx_type(f, "\"ayy");
    gboolean lw = FALSE;
    g_assert_cmpstr(vim_engine_register(f->e, 'a', &lw), ==, "hello world\n");
    g_assert_true(lw);
    vim_engine_clipboard_changed(f->e, "pasted");
    fx_type(f, "$p");
    gchar* t = fx_text(f, FALSE);
    g_assert_cmpstr(t, ==, "hello worldpasted");
    g_free(t);
    fx_free(f);
}

static void test_visual(void) {
    check("hello |world", "viwd", "hello| ");
    check("|a\nb\nc", "Vjd", "|c");
    check("|abc\nabc\nabc", "<C-v>jjld", "|c\nc\nc");
    check("|abc\nabc", "<C-v>jIX<Esc>", "|Xabc\nXabc");
    check("|ab\nabcd", "<C-v>j$AZ<Esc>", "abZ\nabcdZ");
    check("|abc\nabc", "<C-v>jlcX<Esc>", "|Xc\nXc");
    check("|ab\nabcd", "<C-v>jlAZ<Esc>", "abZ\nabZcd");
    check("|abc\nabc", "<C-v>jrx", "|xbc\nxbc");
    check("|abc def", "vee~", "|ABC DEF");
    check("|abc def", "veU", "|ABC def");
    check("|ABC", "vlu", "|abC");
    check("|a\nb\nc", "VjJ", "a| b\nc");
    check("|a\nb", "Vj>", "    |a\n    b");
    check("|a\nb", "Vj2>", "        |a\n        b");
    check("|abcd", "lvlohd", "|d");
    check("|abc", "vly$p", "abca|b");
    check("a |foo b", "vawd", "a |b");
    check("f(a, |b)", "vi(d", "f(|)");
    check("f(a, |b)", "va(d", "|f");
    check("f((a|b))", "vi(i(d", "f(|)");
    check("|one two", "vee", "one two");
    check("|abc", "vlyvgvd", "|c");
    check("|x 1\nx 1\nx 1", "Vjj<C-a>", "|x 2\nx 2\nx 2");
    check("|1\n1\n1", "Vjjg<C-a>", "|2\n3\n4");
    check("|abc\nabc", "<C-v>jOd", "|bc\nbc");
    check("|aa bb", "vwy$p", "aa bbaa |b");
}

static void test_case_indent_format(void) {
    check("|abc", "~~", "AB|c");
    check("|foo bar", "gUiw", "|FOO bar");
    check("|foo bar", "gUU", "|FOO BAR");
    check("|Foo", "g~~", "|fOO");
    check("|abc", "g??", "|nop");
    check("|foo", ">>", "    |foo");
    check("    |foo", "<<", "|foo");
    check("|a\nb\nc", "2>>", "    |a\n    b\nc");
    check("|a\nb\nc", ">j", "    |a\n    b\nc");
    check("|{\nfoo\n}", "=G", "|{\n    foo\n}");
    check("|f(x)\n  y", "==", "|f(x)\n  y");
    check("|aaa bbb ccc ddd", ":set tw=10<CR>gqq", "aaa bbb\n|ccc ddd");
    check("|foo\n   bar", "J", "foo| bar");
    check("|foo\n   bar", "gJ", "foo|   bar");
    check("|a\nb\nc", "3J", "a b| c");
    check("|foo \nbar", "J", "foo |bar");
    check("|foo\n)", "J", "foo|)");
    check("|abc", "rx", "|xbc");
    check("|abc", "3rx", "xx|x");
    check("|abc", "4rx", "|abc");
    check("ab|cd", "r<CR>", "ab\n|d");
    check("x |9 y", "<C-a>", "x 1|0 y");
    check("|x -3", "5<C-a>", "x |2");
    check("|x 0x0f", "<C-a>", "x 0x1|0");
    check("|x 007", "<C-a>", "x 00|8");
    check("|x 5", "10<C-x>", "x -|5");
}

static void test_undo(void) {
    check("|abc", "xxu", "|bc");
    check("|abc", "xxuu", "|abc");
    check("|abc", "xxuu<C-r>", "|bc");
    check("|", "ihello<Esc>u", "|");
    check("|foo bar", "cwx<Esc>u", "|foo bar");
    check("|a\nb", "ddu", "|a\nb");
    check("|a", "ox<Esc>oy<Esc>u", "a\n|x");
    check("|a b c", "dwdw2u", "|a b c");
}

static void test_undo_outside_changes(void) {
    Fixture* f = fx_new("|abc");
    /* Two edits in one user action of the application (an Org command) */
    GtkTextIter it;
    gtk_text_buffer_begin_user_action(f->buf);
    gtk_text_buffer_get_end_iter(f->buf, &it);
    gtk_text_buffer_insert(f->buf, &it, "1", -1);
    gtk_text_buffer_get_end_iter(f->buf, &it);
    gtk_text_buffer_insert(f->buf, &it, "2", -1);
    gtk_text_buffer_end_user_action(f->buf);
    fx_type(f, "xu");
    gchar* t = fx_text(f, FALSE);
    g_assert_cmpstr(t, ==, "abc12");
    g_free(t);
    fx_type(f, "u");
    t = fx_text(f, FALSE);
    g_assert_cmpstr(t, ==, "abc");
    g_free(t);
    fx_free(f);
}

static void test_marks_jumps(void) {
    check("|a\nb\nc", "majd'a", "|c");
    check("ab|cd", "mx0d`x", "|cd");
    check("|a\nb\nc", "Gma``", "|a\nb\nc");
    check("|a\nb\nc", "G''", "|a\nb\nc");
    check("|a\nb\nc\nd", "3Gggj<C-o>", "a\nb\n|c\nd");
    check("|a\nb\nc\nd", "3Ggg<C-o>", "a\nb\n|c\nd");
    check("|a\nb\nc\nd", "3Ggg<C-o><Tab>", "|a\nb\nc\nd");
    check("|abc\nx", "ix<Esc>jgiy<Esc>", "x|yabc\nx");
}

static void test_search(void) {
    check("|foo bar foo", "/foo<CR>", "foo bar |foo");
    check("|foo bar foo", "/foo<CR>n", "|foo bar foo");
    check("|foo bar foo", "/foo<CR>N", "|foo bar foo");
    check("foo bar |foo", "?bar<CR>", "foo |bar foo");
    check("|foo bar foo", "*", "foo bar |foo");
    check("foo bar |foo", "#", "|foo bar foo");
    check("|foo foobar foo", "*", "foo foobar |foo");
    check("|foo foobar", "g*", "foo |foobar");
    check("|Foo foo", "/foo<CR>", "Foo |foo");
    check("|foo Foo", "/Foo<CR>", "foo |Foo");
    check("|a1 b22 c333", "/\\d\\{2}<CR>", "a1 b|22 c333");
    check("|ab abc", "/\\<abc\\><CR>", "ab |abc");
    check("|foo(bar)", "/(<CR>", "foo|(bar)");
    check("|foo bar", "/o\\zsb\\|ar<CR>", "foo b|ar");
    check("|x foo y", "/\\vf(o)+<CR>", "x |foo y");
    check("|xx\nfoo\nfoo", "/foo<CR>/<CR>", "xx\nfoo\n|foo");
    check("|a.c abc", "/a\\.c<CR>n", "|a.c abc");
    check("|abc", "/zzz<CR>", "|abc");
    check("x |foo\nfoo", "gd", "x foo\nfoo");
}

static void test_macros(void) {
    check("|1\n1\n1", "qa<C-a>jq2@a", "2\n2\n|2");
    check("|a\nb\nc", "qqA!<Esc>jq@q@@", "a!\nb!\nc|!");
    check("|x\nx\nx\nx", "qaI- <Esc>jq10@a", "- x\n- x\n- x\n-| x");
    check("|a b\nc d", "qa:s/ /_/<CR>jq@a", "a_b\n|c_d");
    check("|ab\nab", "qadwq\"ap", "d|w\nab");
    check("|1 2", "qwwq@w", "1 |2");
}

static void test_ex(void) {
    check("foo foo\n|foo", ":%s/foo/bar/g<CR>", "bar bar\nbar");
    check("|foo foo", ":s/foo/bar/<CR>", "bar foo");
    check("|a=b", ":s/\\(\\w\\)=\\(\\w\\)/\\2=\\1/<CR>", "b=a");
    check("|foo", ":s/o/\\U&/g<CR>", "fOO");
    check("|foo bar", ":s/\\w\\+/\\u&/g<CR>", "Foo Bar");
    check("|a,b", ":s/,/\\r/<CR>", "a\nb");
    check("|aaa", ":s/a/b/<CR>:s<CR>", "bba");
    check("|aaa\naaa", ":s/a/b/<CR>j:&&<CR>", "baa\nbaa");
    check("|aaa\naaa", ":s/a/b/g<CR>j&", "bbb\nbaa");
    check("|aaa\naaa", ":s/a/b/g<CR>jg&", "bbb\nbbb");
    check("x1\ny\nx2", ":g/x/d<CR>", "y");
    check("x1\ny\nx2", ":v/x/d<CR>", "x1\nx2");
    check("x1\ny\nx2", ":g!/x/d<CR>", "x1\nx2");
    check("a\nb\nc", ":g/^/m0<CR>", "c\nb\na");
    check("a\nb", ":g/./normal Ax<CR>", "ax\nbx");
    check("a\nb", ":%normal Ax<CR>", "ax\nbx");
    check("a\n|b", ":m0<CR>", "b\na");
    check("|a\nb\nc", ":m$<CR>", "b\nc\na");
    check("|a\nb\nc", ":1,2m$<CR>", "c\na\nb");
    check("|a", ":t.<CR>", "a\na");
    check("|a\nb", ":t$<CR>", "a\nb\na");
    check("|a\nb\nc\nd", ":2,3d<CR>", "a\nd");
    check("|a\nb\nc\nd", ":2d 2<CR>", "a\nd");
    check("|a\nb\nc", ":2<CR>", "a\n|b\nc");
    check("|a\nb\nc", ":$<CR>", "a\nb\n|c");
    check("|a\nb\nc", ":99<CR>", "a\nb\n|c");
    check("c\na\nb", ":sort<CR>", "a\nb\nc");
    check("c\na\nb", ":sort!<CR>", "c\nb\na");
    check("10\n9\nx", ":sort n<CR>", "x\n9\n10");
    check("b\na\nb\na", ":sort u<CR>", "a\nb");
    check("B\na\nC", ":sort i<CR>", "a\nB\nC");
    check("|a\nb\nc", ":%j<CR>", "a b c");
    check("|a\nb\nc", ":j<CR>", "a b\nc");
    check("|a\nb", ":j!<CR>", "ab");
    check("|a\nb", ":%><CR>", "    a\n    b");
    check("|a", ":>><CR>", "        a");
    check("|a\nb", ":yank<CR>:$put<CR>", "a\nb\na");
    check("|a\nb", ":2yank x<CR>:0put x<CR>", "b\na\nb");
    check("|a\nb\nc", ":/c/d<CR>", "a\nb");
    check("|a\nb\nc\nd", ":/b/,/c/d<CR>", "a\nd");
    check("|a\nb\nc\nd", ":.,+1d<CR>", "c\nd");
    check("|a\nb\nc\nd", "jVj:d<CR>", "a\nd");
    check("|a\nb\nc\nd", "jVj:s/$/!/<CR>", "a\nb!\nc!\nd");
    check("|a\nb\nc", "2:d<CR>", "c");
    check("|abc", ":center 9<CR>", "   abc");
    check("|abc", ":right 6<CR>", "   abc");
    check("|   abc", ":left<CR>", "abc");
    check("|\tx", ":retab<CR>", "        x");
    check("|a", ":kx<CR>ix<Esc>'x", "|xa");
    check("|abc", "x:undo<CR>", "abc");
}

static void test_ex_errors_and_host(void) {
    Fixture* f = fx_new("|abc");
    fx_type(f, ":foo<CR>");
    g_assert_nonnull(strstr(f->host.messages->str, "E492"));
    fx_type(f, ":s/zzz/y/<CR>");
    g_assert_nonnull(strstr(f->host.messages->str, "E486"));
    fx_type(f, ":w<CR>:q!<CR>:wq<CR>ZZ");
    g_assert_cmpstr(f->host.ex->str, ==, "write ;quit! ;wq ;xit ;");
    g_string_truncate(f->host.ex, 0);
    fx_type(f, ":set number<CR>");
    g_assert_cmpstr(f->host.ex->str, ==, "set number;");
    fx_type(f, ":set bogus<CR>");
    g_assert_nonnull(strstr(f->host.messages->str, "E518"));
    fx_type(f, ":set sw=2 noic<CR>");
    g_assert_cmpint(vim_engine_options(f->e)->shiftwidth, ==, 2);
    g_assert_false(vim_engine_options(f->e)->ignorecase);
    fx_type(f, ":set ic!<CR>");
    g_assert_true(vim_engine_options(f->e)->ignorecase);
    g_string_truncate(f->host.messages, 0);
    fx_type(f, ":set sw?<CR>");
    g_assert_nonnull(strstr(f->host.messages->str, "shiftwidth=2"));
    fx_type(f, ">>");
    gchar* t = fx_text(f, FALSE);
    g_assert_cmpstr(t, ==, "  abc");
    g_free(t);
    g_string_truncate(f->host.messages, 0);
    fx_type(f, "\"qyy:reg q<CR>");
    g_assert_nonnull(strstr(f->host.messages->str, "abc^J"));
    fx_free(f);
}

static void test_cmdline_history_completion(void) {
    Fixture* f = fx_new("|abc");
    fx_type(f, ":s/a/b/<CR>:s/b/c/<CR>");
    vim_engine_feed(f->e, ':');
    gchar* h = vim_engine_cmdline_history(f->e, -1, "");
    g_assert_cmpstr(h, ==, "s/b/c/");
    g_free(h);
    h = vim_engine_cmdline_history(f->e, -1, "");
    g_assert_cmpstr(h, ==, "s/a/b/");
    g_free(h);
    h = vim_engine_cmdline_history(f->e, -1, "");
    g_assert_null(h);
    h = vim_engine_cmdline_history(f->e, 1, "");
    g_assert_cmpstr(h, ==, "s/b/c/");
    g_free(h);
    gsize at = 99;
    gchar** c = vim_engine_cmdline_matches(f->e, "noh", &at);
    g_assert_cmpuint(g_strv_length(c), ==, 1);
    g_assert_cmpstr(c[0], ==, "nohlsearch");
    g_assert_cmpuint(at, ==, 0);
    g_strfreev(c);
    c = vim_engine_cmdline_matches(f->e, "%so", &at);
    g_assert_cmpuint(g_strv_length(c), ==, 1);
    g_assert_cmpstr(c[0], ==, "sort");
    g_assert_cmpuint(at, ==, 1);
    g_strfreev(c);
    /* Every match, sorted */
    c = vim_engine_cmdline_matches(f->e, "w", &at);
    g_assert_cmpuint(g_strv_length(c), ==, 4);
    g_assert_cmpstr(c[0], ==, "wall");
    g_assert_cmpstr(c[1], ==, "wq");
    g_assert_cmpstr(c[3], ==, "write");
    g_strfreev(c);
    c = vim_engine_cmdline_matches(f->e, "set hls", &at);
    g_assert_cmpstr(c[0], ==, "hlsearch");
    g_assert_cmpuint(at, ==, 4);
    g_strfreev(c);
    c = vim_engine_cmdline_matches(f->e, "set noig", &at);
    g_assert_cmpuint(g_strv_length(c), ==, 1);
    g_assert_cmpstr(c[0], ==, "noignorecase");
    g_strfreev(c);
    c = vim_engine_cmdline_matches(f->e, "set no", &at);
    g_assert_true(g_strv_contains((const gchar* const*)c, "nonumber"));
    g_assert_false(g_strv_contains((const gchar* const*)c, "nononumber"));
    g_strfreev(c);
    g_assert_null(vim_engine_cmdline_matches(f->e, "zzz", &at));
    g_assert_null(vim_engine_cmdline_matches(f->e, "e foo", &at));
    vim_engine_cmdline_cancel(f->e);
    g_assert_cmpint(vim_engine_mode(f->e), ==, VIM_MODE_NORMAL);
    fx_free(f);
}

static void test_modes_and_labels(void) {
    Fixture* f = fx_new("|abc");
    g_assert_cmpstr(vim_engine_mode_label(f->e), ==, "NORMAL");
    fx_type(f, "i");
    g_assert_cmpstr(vim_engine_mode_label(f->e), ==, "-- INSERT --");
    fx_type(f, "<Esc>v");
    g_assert_cmpstr(vim_engine_mode_label(f->e), ==, "-- VISUAL --");
    fx_type(f, "V");
    g_assert_cmpstr(vim_engine_mode_label(f->e), ==, "-- VISUAL LINE --");
    fx_type(f, "<C-v>");
    g_assert_cmpstr(vim_engine_mode_label(f->e), ==, "-- VISUAL BLOCK --");
    fx_type(f, "<Esc>R");
    g_assert_cmpstr(vim_engine_mode_label(f->e), ==, "-- REPLACE --");
    fx_type(f, "<Esc>2d");
    g_assert_cmpint(vim_engine_mode(f->e), ==, VIM_MODE_OPERATOR_PENDING);
    gchar* pk = vim_engine_pending_keys(f->e);
    g_assert_cmpstr(pk, ==, "2d");
    g_free(pk);
    fx_type(f, "<Esc>qa");
    g_assert_cmpint(vim_engine_recording(f->e), ==, 'a');
    fx_type(f, "q");
    g_assert_cmpint(vim_engine_recording(f->e), ==, 0);
    /* A mouse selection starts Visual mode */
    GtkTextIter s, t;
    gtk_text_buffer_get_iter_at_offset(f->buf, &s, 0);
    gtk_text_buffer_get_iter_at_offset(f->buf, &t, 2);
    gtk_text_buffer_select_range(f->buf, &t, &s);
    fx_type(f, "d");
    gchar* text = fx_text(f, FALSE);
    g_assert_cmpstr(text, ==, "c");
    g_free(text);
    /* Unknown Ctrl keys go to the application */
    g_assert_false(vim_engine_feed(f->e, VIM_KEY_CTRL('s')));
    fx_free(f);
}

static void test_scrolling_keys(void) {
    Fixture* f = fx_new("|0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20");
    fx_type(f, "<C-d>");
    GtkTextIter c;
    vim_engine_get_cursor(f->e, &c);
    g_assert_cmpint(gtk_text_iter_get_line(&c), ==, 5);
    fx_type(f, "M");
    vim_engine_get_cursor(f->e, &c);
    g_assert_cmpint(gtk_text_iter_get_line(&c), ==, 4);
    fx_free(f);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vim_engine/motions", test_motions);
    g_test_add_func("/vim_engine/delete_change", test_delete_change);
    g_test_add_func("/vim_engine/insert", test_insert);
    g_test_add_func("/vim_engine/dot_repeat", test_dot_repeat);
    g_test_add_func("/vim_engine/yank_put", test_yank_put);
    g_test_add_func("/vim_engine/registers_clipboard", test_registers_clipboard);
    g_test_add_func("/vim_engine/visual", test_visual);
    g_test_add_func("/vim_engine/case_indent_format", test_case_indent_format);
    g_test_add_func("/vim_engine/undo", test_undo);
    g_test_add_func("/vim_engine/undo_outside_changes", test_undo_outside_changes);
    g_test_add_func("/vim_engine/marks_jumps", test_marks_jumps);
    g_test_add_func("/vim_engine/search", test_search);
    g_test_add_func("/vim_engine/macros", test_macros);
    g_test_add_func("/vim_engine/ex", test_ex);
    g_test_add_func("/vim_engine/ex_errors_and_host", test_ex_errors_and_host);
    g_test_add_func("/vim_engine/cmdline_history_completion", test_cmdline_history_completion);
    g_test_add_func("/vim_engine/modes_and_labels", test_modes_and_labels);
    g_test_add_func("/vim_engine/scrolling_keys", test_scrolling_keys);
    return g_test_run();
}
