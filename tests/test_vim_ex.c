/* Unit tests for src/vim/vim_ex.c: Ex command line parsing */
#include "vim/vim_ex.h"

#include <string.h>

static VimExCmd parse_ok(const gchar* line) {
    VimExCmd c;
    gchar* err = NULL;
    gboolean ok = vim_ex_parse(line, &c, &err);
    if (!ok) g_printerr("parse failed: %s (%s)\n", line, err);
    g_assert_true(ok);
    g_assert_null(err);
    return c;
}

static void test_names(void) {
    const struct {
        const gchar* line;
        const gchar* name;
    } cases[] = {
        {"w", "write"}, {"wq", "wq"}, {"x", "xit"}, {"q!", "quit"}, {"s/a/b/", "substitute"},
        {"%s#a#b#g", "substitute"}, {"se nu", "set"}, {"set", "set"}, {"sor", "sort"},
        {"noh", "nohlsearch"}, {"norm! x", "normal"}, {"g/x/d", "global"}, {"v/x/d", "vglobal"},
        {"d", "delete"}, {"y a", "yank"}, {"pu", "put"}, {"m0", "move"}, {"t.", "copy"},
        {"co$", "copy"}, {"ma a", "mark"}, {"marks", "marks"}, {"ka", "mark"}, {"j!", "join"},
        {"reg", "registers"}, {"di", "registers"}, {"red", "redo"}, {"u", "undo"},
        {"r", "read"}, {"r! ls", "read"}, {"read", "read"}, {"read! echo 1", "read"},
        {"ce 40", "center"}, {"ri", "right"}, {"le", "left"}, {"ret", "retab"}, {"&&", "&&"},
        {">", ">"}, {"<<", "<"}, {"sg", "substitute"}, {"tabn", "tabnext"}, {"bp", "tabprevious"},
        {"sav x", "saveas"}, {"e!", "edit"}, {"", ""}, {"12", ""},
        {"sp", "split"}, {"vs", "vsplit"}, {"vsplit", "vsplit"}, {"on", "only"}, {"clo", "close"},
    };
    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        VimExCmd c = parse_ok(cases[i].line);
        g_assert_cmpstr(c.name, ==, cases[i].name);
        vim_ex_cmd_clear(&c);
    }
    VimExCmd c;
    gchar* err = NULL;
    g_assert_false(vim_ex_parse("frobnicate", &c, &err));
    g_assert_nonnull(strstr(err, "E492"));
    g_free(err);
}

static void test_bang_args(void) {
    VimExCmd c = parse_ok("q!");
    g_assert_true(c.bang);
    g_assert_cmpstr(c.args, ==, "");
    vim_ex_cmd_clear(&c);
    c = parse_ok("w  foo.txt ");
    g_assert_false(c.bang);
    g_assert_cmpstr(c.args, ==, "foo.txt");
    vim_ex_cmd_clear(&c);
    c = parse_ok("norm Ax ");
    g_assert_cmpstr(c.args, ==, "Ax ");
    vim_ex_cmd_clear(&c);
    c = parse_ok("ka");
    g_assert_cmpstr(c.args, ==, "a");
    vim_ex_cmd_clear(&c);
    c = parse_ok("r! ls -la");
    g_assert_true(c.bang);
    g_assert_cmpstr(c.args, ==, "ls -la");
    vim_ex_cmd_clear(&c);
    c = parse_ok("read! echo hello");
    g_assert_true(c.bang);
    g_assert_cmpstr(c.args, ==, "echo hello");
    vim_ex_cmd_clear(&c);
}

static void test_ranges(void) {
    VimExCmd c = parse_ok("%d");
    g_assert_true(c.whole);
    g_assert_cmpint(c.naddr, ==, 2);
    vim_ex_cmd_clear(&c);

    c = parse_ok("3,5d");
    g_assert_cmpint(c.naddr, ==, 2);
    g_assert_cmpint(c.addr[0].kind, ==, VIM_ADDR_NUMBER);
    g_assert_cmpint(c.addr[0].number, ==, 3);
    g_assert_cmpint(c.addr[1].number, ==, 5);
    vim_ex_cmd_clear(&c);

    c = parse_ok(".,$s/a/b/");
    g_assert_cmpint(c.addr[0].kind, ==, VIM_ADDR_CURRENT);
    g_assert_cmpint(c.addr[1].kind, ==, VIM_ADDR_LAST);
    g_assert_cmpstr(c.args, ==, "/a/b/");
    vim_ex_cmd_clear(&c);

    c = parse_ok("'<,'>s/x/y/");
    g_assert_cmpint(c.addr[0].kind, ==, VIM_ADDR_MARK);
    g_assert_cmpint(c.addr[0].mark, ==, '<');
    g_assert_cmpint(c.addr[1].mark, ==, '>');
    vim_ex_cmd_clear(&c);

    c = parse_ok(".+2,$-1d");
    g_assert_cmpint(c.addr[0].offset, ==, 2);
    g_assert_cmpint(c.addr[1].offset, ==, -1);
    vim_ex_cmd_clear(&c);

    c = parse_ok("+,++d");
    g_assert_cmpint(c.addr[0].offset, ==, 1);
    g_assert_cmpint(c.addr[1].offset, ==, 2);
    vim_ex_cmd_clear(&c);

    c = parse_ok("/foo/,/bar\\/x/d");
    g_assert_cmpint(c.addr[0].kind, ==, VIM_ADDR_SEARCH_FWD);
    g_assert_cmpstr(c.addr[0].pattern, ==, "foo");
    g_assert_cmpstr(c.addr[1].pattern, ==, "bar/x");
    vim_ex_cmd_clear(&c);

    c = parse_ok("?x?;+3p");
    g_assert_cmpint(c.addr[0].kind, ==, VIM_ADDR_SEARCH_BWD);
    g_assert_true(c.semicolon);
    vim_ex_cmd_clear(&c);

    c = parse_ok("*d");
    g_assert_cmpint(c.addr[0].mark, ==, '<');
    vim_ex_cmd_clear(&c);

    c = parse_ok("12");
    g_assert_cmpint(c.naddr, ==, 1);
    g_assert_cmpint(c.addr[0].number, ==, 12);
    vim_ex_cmd_clear(&c);
}

static void test_split_pattern(void) {
    gchar *pat, *rep, *rest;
    g_assert_true(vim_ex_split_pattern("/a\\/b/c\\1/g 3", &pat, &rep, &rest, TRUE));
    g_assert_cmpstr(pat, ==, "a/b");
    g_assert_cmpstr(rep, ==, "c\\1");
    g_assert_cmpstr(rest, ==, "g 3");
    g_free(pat);
    g_free(rep);
    g_free(rest);
    g_assert_true(vim_ex_split_pattern("#x#y", &pat, &rep, &rest, TRUE));
    g_assert_cmpstr(pat, ==, "x");
    g_assert_cmpstr(rep, ==, "y");
    g_assert_cmpstr(rest, ==, "");
    g_free(pat);
    g_free(rep);
    g_free(rest);
    g_assert_true(vim_ex_split_pattern("/x/normal Ay", &pat, NULL, &rest, FALSE));
    g_assert_cmpstr(pat, ==, "x");
    g_assert_cmpstr(rest, ==, "normal Ay");
    g_free(pat);
    g_free(rest);
    g_assert_false(vim_ex_split_pattern("g", &pat, &rep, &rest, TRUE));
    g_free(rest);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vim_ex/names", test_names);
    g_test_add_func("/vim_ex/bang_args", test_bang_args);
    g_test_add_func("/vim_ex/ranges", test_ranges);
    g_test_add_func("/vim_ex/split_pattern", test_split_pattern);
    return g_test_run();
}
