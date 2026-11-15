/* Unit tests for src/latex/latex_log.c: messages of a TeX log. */
#include "latex/latex_log.h"

#include <string.h>

static const LatexMessage* msg(GPtrArray* msgs, guint i) {
    g_assert_cmpuint(i, <, msgs->len);
    return g_ptr_array_index(msgs, i);
}

static void test_file_line_error(void) {
    const gchar* log =
        "This is pdfTeX, Version 3.141592653-2.6-1.40.25 (TeX Live 2023) (preloaded format=pdflatex)\n"
        "(./main.tex\n"
        "LaTeX2e <2022-11-01> patch level 1\n"
        "(/usr/share/texlive/texmf-dist/tex/latex/base/article.cls\n"
        "Document Class: article 2022/07/02 v1.4n Standard LaTeX document class\n"
        "(/usr/share/texlive/texmf-dist/tex/latex/base/size10.clo))\n"
        "(./main.aux)\n"
        "./main.tex:5: Undefined control sequence.\n"
        "l.5 Hello \\foo\n"
        "              {} world\n"
        "\n"
        "./main.tex:7: LaTeX Error: Environment itemz undefined.\n"
        "\n"
        "See the LaTeX manual or LaTeX Companion for explanation.\n"
        "Type  H <return>  for immediate help.\n"
        " ...                                              \n"
        "\n"
        "l.7 \\begin{itemz}\n"
        "                \n"
        "[1] (./main.aux) )\n";
    GPtrArray* msgs = latex_log_parse(log, -1);
    g_assert_cmpuint(msgs->len, ==, 2);
    g_assert_cmpint(msg(msgs, 0)->kind, ==, LATEX_MSG_ERROR);
    g_assert_cmpstr(msg(msgs, 0)->file, ==, "./main.tex");
    g_assert_cmpint(msg(msgs, 0)->line, ==, 5);
    g_assert_cmpstr(msg(msgs, 0)->text, ==, "Undefined control sequence.");
    g_assert_cmpstr(msg(msgs, 0)->context, ==, "Hello \\foo {} world");
    g_assert_cmpstr(msg(msgs, 1)->text, ==, "LaTeX Error: Environment itemz undefined.");
    g_assert_cmpint(msg(msgs, 1)->line, ==, 7);
    g_assert_cmpstr(msg(msgs, 1)->context, ==, "\\begin{itemz}");
    g_assert_cmpuint(latex_log_count(msgs, LATEX_MSG_ERROR), ==, 2);
    g_assert_cmpuint(latex_log_count(msgs, LATEX_MSG_WARNING), ==, 0);
    g_ptr_array_unref(msgs);
}

/* Without -file-line-error: "! message", the file from the nesting */
static void test_bang_error(void) {
    const gchar* log =
        "(./main.tex (./chapters/intro.tex\n"
        "! Missing $ inserted.\n"
        "<inserted text> \n"
        "                $\n"
        "l.3 x^\n"
        "      2\n"
        ") (./chapters/end.tex)\n"
        "! Emergency stop.\n"
        "<*> main.tex\n";
    GPtrArray* msgs = latex_log_parse(log, -1);
    g_assert_cmpuint(msgs->len, ==, 2);
    g_assert_cmpstr(msg(msgs, 0)->file, ==, "./chapters/intro.tex");
    g_assert_cmpint(msg(msgs, 0)->line, ==, 3);
    g_assert_cmpstr(msg(msgs, 0)->text, ==, "Missing $ inserted.");
    g_assert_cmpstr(msg(msgs, 0)->context, ==, "x^ 2");
    /* intro.tex and end.tex are closed: back in main.tex */
    g_assert_cmpstr(msg(msgs, 1)->file, ==, "./main.tex");
    g_assert_cmpint(msg(msgs, 1)->line, ==, 0);
    g_ptr_array_unref(msgs);
}

static void test_warnings(void) {
    const gchar* log =
        "(./main.tex (./sec.tex\n"
        "LaTeX Warning: Reference `fig:x' on page 1 undefined on input line 12.\n"
        "\n"
        "Package hyperref Warning: Token not allowed in a PDF string (Unicode):\n"
        "(hyperref)                removing `math shift' on input line 20.\n"
        "\n"
        "Package hyperref Warning: Token not allowed in a PDF string (Unicode):\n"
        "(hyperref)                removing `math shift' on input line 20.\n"
        "\n"
        ")\n"
        "LaTeX Font Warning: Font shape `OT1/cmr/bx/sc' undefined\n"
        "(Font)              using `OT1/cmr/bx/n' instead on input line 30.\n"
        "\n"
        "LaTeX Warning: There were undefined references.\n"
        ")\n";
    GPtrArray* msgs = latex_log_parse(log, -1);
    g_assert_cmpuint(msgs->len, ==, 4);
    g_assert_cmpint(msg(msgs, 0)->kind, ==, LATEX_MSG_WARNING);
    g_assert_cmpstr(msg(msgs, 0)->text, ==, "Reference `fig:x' on page 1 undefined");
    g_assert_cmpstr(msg(msgs, 0)->file, ==, "./sec.tex");
    g_assert_cmpint(msg(msgs, 0)->line, ==, 12);
    g_assert_cmpstr(msg(msgs, 1)->text, ==,
                    "Token not allowed in a PDF string (Unicode): removing `math shift'");
    g_assert_cmpint(msg(msgs, 1)->line, ==, 20);
    g_assert_cmpstr(msg(msgs, 2)->text, ==, "Font shape `OT1/cmr/bx/sc' undefined using `OT1/cmr/bx/n' instead");
    g_assert_cmpstr(msg(msgs, 2)->file, ==, "./main.tex");
    g_assert_cmpint(msg(msgs, 2)->line, ==, 30);
    g_assert_cmpstr(msg(msgs, 3)->text, ==, "There were undefined references.");
    g_assert_cmpint(msg(msgs, 3)->line, ==, 0);
    g_ptr_array_unref(msgs);
}

/* The box content shown after a bad box has parentheses that are not files */
static void test_badboxes(void) {
    const gchar* log =
        "(./main.tex\n"
        "Overfull \\hbox (15.2pt too wide) in paragraph at lines 8--10\n"
        "[]\\OT1/cmr/m/n/10 A (very long) line (with paren\n"
        "\n"
        "Underfull \\vbox (badness 10000) has occurred while \\output is active []\n"
        "\n"
        "Underfull \\hbox (badness 10000) detected at line 14\n"
        "[]\n"
        "\n"
        "./main.tex:20: Undefined control sequence.\n"
        "l.20 \\x\n"
        "\n";
    GPtrArray* msgs = latex_log_parse(log, -1);
    g_assert_cmpuint(msgs->len, ==, 4);
    g_assert_cmpint(msg(msgs, 0)->kind, ==, LATEX_MSG_BADBOX);
    g_assert_cmpstr(msg(msgs, 0)->text, ==, "Overfull \\hbox (15.2pt too wide)");
    g_assert_cmpint(msg(msgs, 0)->line, ==, 8);
    g_assert_cmpstr(msg(msgs, 1)->text, ==, "Underfull \\vbox (badness 10000)");
    g_assert_cmpint(msg(msgs, 1)->line, ==, 0);
    g_assert_cmpstr(msg(msgs, 2)->file, ==, "./main.tex");
    g_assert_cmpint(msg(msgs, 2)->line, ==, 14);
    g_assert_cmpuint(latex_log_count(msgs, LATEX_MSG_BADBOX), ==, 3);
    g_assert_cmpint(msg(msgs, 3)->kind, ==, LATEX_MSG_ERROR);
    g_ptr_array_unref(msgs);
}

/* Lines of 79 characters continue on the next line */
static void test_wrapped_lines(void) {
    GString* log = g_string_new("(");
    gchar* dir = g_strnfill(79 - 1 - strlen("/a/"), 'd');
    g_string_append_printf(log, "/a/%s\n", dir);
    g_string_append(log, "/chapter.tex\n");
    const gchar* warning = "LaTeX Warning: Citation `k' on page 2 undefined ";
    gchar* text = g_strnfill(79 - strlen(warning) - 1, 'w');
    g_string_append_printf(log, "%s%s \n", warning, text);
    g_string_append(log, "on input line 4.\n");
    GPtrArray* msgs = latex_log_parse(log->str, (gssize)log->len);
    g_assert_cmpuint(msgs->len, ==, 1);
    gchar* file = g_strdup_printf("/a/%s/chapter.tex", dir);
    g_assert_cmpstr(msg(msgs, 0)->file, ==, file);
    g_assert_cmpint(msg(msgs, 0)->line, ==, 4);
    g_free(file);
    g_free(text);
    g_free(dir);
    g_ptr_array_unref(msgs);
    g_string_free(log, TRUE);
}

static void test_empty(void) {
    GPtrArray* msgs = latex_log_parse(NULL, 0);
    g_assert_cmpuint(msgs->len, ==, 0);
    g_ptr_array_unref(msgs);
    msgs = latex_log_parse("No pages of output.\n", -1);
    g_assert_cmpuint(msgs->len, ==, 0);
    g_assert_cmpuint(latex_log_count(msgs, LATEX_MSG_ERROR), ==, 0);
    g_ptr_array_unref(msgs);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/latex_log/file_line_error", test_file_line_error);
    g_test_add_func("/latex_log/bang_error", test_bang_error);
    g_test_add_func("/latex_log/warnings", test_warnings);
    g_test_add_func("/latex_log/badboxes", test_badboxes);
    g_test_add_func("/latex_log/wrapped_lines", test_wrapped_lines);
    g_test_add_func("/latex_log/empty", test_empty);
    return g_test_run();
}
