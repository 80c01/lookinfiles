/* Unit tests for src/vim/vim_parse.c: the Normal / Visual command grammar */
#include "vim/vim_parse.h"
#include "vim/vim_keys.h"

#include <string.h>

static VimParseStatus parse(const gchar* notation, gboolean visual, VimCmd* cmd) {
    gchar* keys = vim_keys_from_notation(notation);
    glong n = 0;
    gunichar* k = g_utf8_to_ucs4_fast(keys, -1, &n);
    VimParseStatus st = vim_parse(k, (gsize)n, visual, FALSE, cmd);
    g_free(k);
    g_free(keys);
    return st;
}

static void test_incomplete_invalid(void) {
    VimCmd c;
    g_assert_cmpint(parse("d", FALSE, &c), ==, VIM_PARSE_INCOMPLETE);
    g_assert_cmpint(parse("2", FALSE, &c), ==, VIM_PARSE_INCOMPLETE);
    g_assert_cmpint(parse("\"", FALSE, &c), ==, VIM_PARSE_INCOMPLETE);
    g_assert_cmpint(parse("\"a2d3", FALSE, &c), ==, VIM_PARSE_INCOMPLETE);
    g_assert_cmpint(parse("g", FALSE, &c), ==, VIM_PARSE_INCOMPLETE);
    g_assert_cmpint(parse("di", FALSE, &c), ==, VIM_PARSE_INCOMPLETE);
    g_assert_cmpint(parse("f", FALSE, &c), ==, VIM_PARSE_INCOMPLETE);
    g_assert_cmpint(parse("dQ", FALSE, &c), ==, VIM_PARSE_INVALID);
    g_assert_cmpint(parse("d<Esc>", FALSE, &c), ==, VIM_PARSE_INVALID);
    g_assert_cmpint(parse("f<Esc>", FALSE, &c), ==, VIM_PARSE_INVALID);
    g_assert_cmpint(parse("diz", FALSE, &c), ==, VIM_PARSE_INVALID);
    g_assert_cmpint(parse("gZ", FALSE, &c), ==, VIM_PARSE_INVALID);
}

static void test_operators(void) {
    VimCmd c;
    g_assert_cmpint(parse("2d3w", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(c.kind, ==, VIM_CMD_OPERATOR);
    g_assert_cmpint(c.count, ==, 6);
    g_assert_true(vim_cmd_op_is(&c, 'd', 0));
    g_assert_true(vim_cmd_is(&c, 'w', 0));

    g_assert_cmpint(parse("\"a3yy", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(c.kind, ==, VIM_CMD_OPERATOR_LINES);
    g_assert_cmpint(c.reg, ==, 'a');
    g_assert_cmpint(c.count, ==, 3);

    g_assert_cmpint(parse("3\"bdd", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(c.reg, ==, 'b');

    g_assert_cmpint(parse("ci(", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_true(vim_cmd_is(&c, 'i', '('));

    g_assert_cmpint(parse("gUiw", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_true(vim_cmd_op_is(&c, 'g', 'U'));
    g_assert_true(vim_cmd_is(&c, 'i', 'w'));

    g_assert_cmpint(parse("g~~", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(c.kind, ==, VIM_CMD_OPERATOR_LINES);
    g_assert_cmpint(parse("gUgU", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(c.kind, ==, VIM_CMD_OPERATOR_LINES);

    g_assert_cmpint(parse("dfx", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_true(vim_cmd_is(&c, 'f', 0));
    g_assert_cmpint(c.arg, ==, 'x');

    g_assert_cmpint(parse("dgg", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_true(vim_cmd_is(&c, 'g', 'g'));

    g_assert_cmpint(parse("d0", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_true(vim_cmd_is(&c, '0', 0));

    g_assert_cmpint(parse("d10j", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(c.count, ==, 10);

    g_assert_cmpint(parse("dvj", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(c.force, ==, 'v');

    g_assert_cmpint(parse("d/", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_true(vim_cmd_is(&c, '/', 0));
}

static void test_motions_commands(void) {
    VimCmd c;
    g_assert_cmpint(parse("10j", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(c.kind, ==, VIM_CMD_MOTION);
    g_assert_cmpint(c.count, ==, 10);
    g_assert_cmpint(parse("0", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_true(vim_cmd_is(&c, '0', 0));
    g_assert_cmpint(parse("<Down>", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_true(vim_cmd_is(&c, 'j', 0));
    g_assert_cmpint(parse("<CR>", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_true(vim_cmd_is(&c, '+', 0));
    g_assert_cmpint(parse("'a", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(c.arg, ==, 'a');
    g_assert_cmpint(parse("]}", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(c.kind, ==, VIM_CMD_MOTION);

    g_assert_cmpint(parse("3x", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(c.kind, ==, VIM_CMD_SIMPLE);
    g_assert_cmpint(parse("rx", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(c.arg, ==, 'x');
    g_assert_cmpint(parse("qa", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(c.arg, ==, 'a');
    g_assert_cmpint(parse("@@", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(parse("gJ", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(c.kind, ==, VIM_CMD_SIMPLE);
    g_assert_cmpint(parse("zz", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(parse("ZZ", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(parse("<C-r>", FALSE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(parse("<C-v>", FALSE, &c), ==, VIM_PARSE_DONE);

    /* Recording: q alone stops */
    gunichar q = 'q';
    g_assert_cmpint(vim_parse(&q, 1, FALSE, TRUE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(c.arg, ==, 0);
}

static void test_visual(void) {
    VimCmd c;
    g_assert_cmpint(parse("iw", TRUE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(c.kind, ==, VIM_CMD_TEXTOBJ);
    g_assert_cmpint(parse("d", TRUE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(c.kind, ==, VIM_CMD_SIMPLE);
    g_assert_cmpint(parse("3>", TRUE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(c.count, ==, 3);
    g_assert_cmpint(parse("o", TRUE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(parse("gu", TRUE, &c), ==, VIM_PARSE_DONE);
    g_assert_true(vim_cmd_is(&c, 'g', 'u'));
    g_assert_cmpint(parse("rx", TRUE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(parse("$", TRUE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(c.kind, ==, VIM_CMD_MOTION);
    g_assert_cmpint(parse("\"ay", TRUE, &c), ==, VIM_PARSE_DONE);
    g_assert_cmpint(c.reg, ==, 'a');
}

static void test_notation(void) {
    gchar* k = vim_keys_from_notation("a<Esc><C-r><lt>b<CR>");
    gchar* shown = vim_keys_to_display(k);
    g_assert_cmpstr(shown, ==, "a^[^R<b^M");
    g_free(shown);
    g_free(k);
    g_assert_cmpuint(vim_key_normalize(0x1b), ==, VIM_KEY_ESC);
    g_assert_cmpuint(vim_key_normalize(VIM_KEY_CTRL('[')), ==, VIM_KEY_ESC);
    g_assert_cmpuint(vim_key_normalize(0x01), ==, VIM_KEY_CTRL('a'));
    g_assert_cmpuint(vim_key_from_gdk(GDK_KEY_a, GDK_CONTROL_MASK), ==, VIM_KEY_CTRL('a'));
    g_assert_cmpuint(vim_key_from_gdk(GDK_KEY_A, GDK_SHIFT_MASK), ==, 'A');
    g_assert_cmpuint(vim_key_from_gdk(GDK_KEY_x, GDK_ALT_MASK), ==, 0);
    g_assert_cmpuint(vim_key_from_gdk(GDK_KEY_V, GDK_CONTROL_MASK | GDK_SHIFT_MASK), ==, 0);
    g_assert_cmpuint(vim_key_from_gdk(GDK_KEY_Shift_L, 0), ==, 0);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vim_parse/incomplete_invalid", test_incomplete_invalid);
    g_test_add_func("/vim_parse/operators", test_operators);
    g_test_add_func("/vim_parse/motions_commands", test_motions_commands);
    g_test_add_func("/vim_parse/visual", test_visual);
    g_test_add_func("/vim_parse/notation", test_notation);
    return g_test_run();
}
