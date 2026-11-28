/* Unit tests for src/latex/latex_edit.c: LaTeX environments for the editor. */
#include "latex/latex_edit.h"

#include <string.h>

/* Offset of the n-th (0-based) occurrence of needle in text */
static gsize at(const gchar* text, const gchar* needle, gint n) {
    const gchar* p = text;
    for (gint i = 0;; i++) {
        p = strstr(p, needle);
        g_assert_nonnull(p);
        if (i == n) return (gsize)(p - text);
        p++;
    }
}

static gchar* name_of(const gchar* text, const LatexEnvToken* t) {
    return g_strndup(text + t->name_start, t->name_end - t->name_start);
}

static void test_token_at(void) {
    const gchar* text = "a \\begin{itemize} b \\end {itemize}\n% \\begin{x}\n\\beginx{y} \\endgraf{z}\n";
    gsize len = strlen(text);
    LatexEnvToken t;
    /* In the name, at both ends */
    g_assert_true(latex_edit_token_at(text, len, at(text, "itemize", 0), TRUE, &t));
    g_assert_true(t.begin);
    g_assert_cmpuint(t.start, ==, 2);
    g_assert_cmpuint(t.end, ==, at(text, " b ", 0));
    gchar* n = name_of(text, &t);
    g_assert_cmpstr(n, ==, "itemize");
    g_free(n);
    g_assert_true(latex_edit_token_at(text, len, at(text, "} b", 0), TRUE, &t));
    /* On the command word: only when not name_only */
    g_assert_false(latex_edit_token_at(text, len, 4, TRUE, &t));
    g_assert_true(latex_edit_token_at(text, len, 4, FALSE, &t));
    /* \end with a space before its brace */
    g_assert_true(latex_edit_token_at(text, len, at(text, "itemize", 1) + 2, TRUE, &t));
    g_assert_false(t.begin);
    /* Comments, other commands, plain text */
    g_assert_false(latex_edit_token_at(text, len, at(text, "{x}", 0) + 1, FALSE, &t));
    g_assert_false(latex_edit_token_at(text, len, at(text, "{y}", 0) + 1, FALSE, &t));
    g_assert_false(latex_edit_token_at(text, len, at(text, "{z}", 0) + 1, FALSE, &t));
    g_assert_false(latex_edit_token_at(text, len, 0, FALSE, &t));
}

static void test_partner(void) {
    const gchar* text = "\\begin{a}\n \\begin{b}\n  \\begin{a}\n  \\end{a}\n \\end{b}\n% \\end{a}\n\\end{a}\n\\end{c}\n";
    gsize len = strlen(text);
    LatexEnvToken t, p;
    /* The outer a: past the inner a and the commented \end */
    g_assert_true(latex_edit_token_at(text, len, 1, FALSE, &t));
    g_assert_true(latex_edit_partner(text, len, &t, &p));
    g_assert_false(p.begin);
    g_assert_cmpuint(p.start, ==, at(text, "\\end{a}", 2));
    /* And back */
    g_assert_true(latex_edit_partner(text, len, &p, &t));
    g_assert_cmpuint(t.start, ==, 0);
    /* The inner a */
    g_assert_true(latex_edit_token_at(text, len, at(text, "\\begin{a}", 1), FALSE, &t));
    g_assert_true(latex_edit_partner(text, len, &t, &p));
    g_assert_cmpuint(p.start, ==, at(text, "\\end{a}", 0));
    /* An \end without its \begin */
    g_assert_true(latex_edit_token_at(text, len, at(text, "\\end{c}", 0), FALSE, &t));
    g_assert_false(latex_edit_partner(text, len, &t, &p));
}

static void test_partner_unbalanced(void) {
    /* b is never closed: the \end{a} still closes a */
    const gchar* text = "\\begin{a}\n\\begin{b}\n\\end{a}\n";
    gsize len = strlen(text);
    LatexEnvToken t, p;
    g_assert_true(latex_edit_token_at(text, len, 0, FALSE, &t));
    g_assert_true(latex_edit_partner(text, len, &t, &p));
    g_assert_cmpuint(p.start, ==, at(text, "\\end{a}", 0));
    g_assert_true(latex_edit_token_at(text, len, at(text, "\\begin{b}", 0), FALSE, &t));
    g_assert_false(latex_edit_partner(text, len, &t, &p));
}

static void test_verbatim(void) {
    const gchar* text = "\\begin{a}\n\\begin{verbatim}\n\\end{a}\n\\end{verbatim}\n\\end{a}\n";
    gsize len = strlen(text);
    LatexEnvToken t, p;
    g_assert_true(latex_edit_token_at(text, len, 0, FALSE, &t));
    g_assert_true(latex_edit_partner(text, len, &t, &p));
    g_assert_cmpuint(p.start, ==, at(text, "\\end{a}", 1));
    g_assert_true(latex_edit_token_at(text, len, at(text, "\\begin{verbatim}", 0), FALSE, &t));
    g_assert_true(latex_edit_partner(text, len, &t, &p));
    g_assert_cmpuint(p.start, ==, at(text, "\\end{verbatim}", 0));
}

static void test_enclosing(void) {
    const gchar* text = "x\n\\begin{a}\n p\n \\begin{b} q \\end{b}\n r\n\\end{a}\n\\begin{c}\n s";
    gsize len = strlen(text);
    LatexEnvToken b, e;
    gboolean closed = FALSE;
    g_assert_false(latex_edit_enclosing(text, len, 0, &b, &e, &closed));
    g_assert_true(latex_edit_enclosing(text, len, at(text, "p", 0), &b, &e, &closed));
    g_assert_true(closed);
    g_assert_cmpuint(b.start, ==, at(text, "\\begin{a}", 0));
    g_assert_cmpuint(e.start, ==, at(text, "\\end{a}", 0));
    g_assert_true(latex_edit_enclosing(text, len, at(text, "q", 0), &b, &e, &closed));
    g_assert_cmpuint(b.start, ==, at(text, "\\begin{b}", 0));
    g_assert_true(latex_edit_enclosing(text, len, at(text, "r", 0), &b, &e, &closed));
    g_assert_cmpuint(b.start, ==, at(text, "\\begin{a}", 0));
    /* Inside an \end: still in its environment */
    g_assert_true(latex_edit_enclosing(text, len, at(text, "\\end{a}", 0) + 3, &b, &e, &closed));
    g_assert_cmpuint(b.start, ==, at(text, "\\begin{a}", 0));
    /* An environment left open */
    g_assert_true(latex_edit_enclosing(text, len, at(text, "s", 0), &b, &e, &closed));
    g_assert_false(closed);
    g_assert_cmpuint(b.start, ==, at(text, "\\begin{c}", 0));
}

#define ASSERT_CLOSE(text, offset, expected)                                     \
    do {                                                                         \
        gchar* got_ = latex_edit_env_to_close((text), strlen(text), (offset));  \
        g_assert_cmpstr(got_, ==, (expected));                                   \
        g_free(got_);                                                            \
    } while (0)

static void test_env_to_close(void) {
    const gchar* t1 = "  \\begin{itemize}";
    ASSERT_CLOSE(t1, strlen(t1), "itemize");
    const gchar* t2 = "\\begin{tabular}{|l|c|} \nnext";
    ASSERT_CLOSE(t2, at(t2, "\n", 0), "tabular");
    const gchar* t3 = "\\begin{figure}[htbp]";
    ASSERT_CLOSE(t3, strlen(t3), "figure");
    /* Already closed, text after it, inside it, unfinished arguments */
    const gchar* t4 = "\\begin{itemize}\n\\item a\n\\end{itemize}\n";
    ASSERT_CLOSE(t4, at(t4, "\n", 0), NULL);
    const gchar* t5 = "\\begin{center} text";
    ASSERT_CLOSE(t5, strlen(t5), NULL);
    ASSERT_CLOSE(t5, 10, NULL);
    const gchar* t6 = "\\begin{tabular}{l";
    ASSERT_CLOSE(t6, strlen(t6), NULL);
    /* A new list before a closed one of the same name */
    const gchar* t7 = "\\begin{enumerate}\n\\begin{enumerate}\n\\end{enumerate}\n";
    ASSERT_CLOSE(t7, at(t7, "\n", 0), "enumerate");
    /* Commented out, an \end on the line */
    const gchar* t8 = "% \\begin{proof}";
    ASSERT_CLOSE(t8, strlen(t8), NULL);
    const gchar* t9 = "\\begin{a}\\end{a}";
    ASSERT_CLOSE(t9, strlen(t9), NULL);
}

static void test_valid_name(void) {
    g_assert_true(latex_edit_valid_name("align*", -1));
    g_assert_true(latex_edit_valid_name("my-env_2", -1));
    g_assert_false(latex_edit_valid_name("", -1));
    g_assert_false(latex_edit_valid_name("a b", -1));
    g_assert_false(latex_edit_valid_name("a}", -1));
    g_assert_false(latex_edit_valid_name(NULL, -1));
    g_assert_true(latex_edit_valid_name("abc}", 3));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/latex_edit/token_at", test_token_at);
    g_test_add_func("/latex_edit/partner", test_partner);
    g_test_add_func("/latex_edit/partner_unbalanced", test_partner_unbalanced);
    g_test_add_func("/latex_edit/verbatim", test_verbatim);
    g_test_add_func("/latex_edit/enclosing", test_enclosing);
    g_test_add_func("/latex_edit/env_to_close", test_env_to_close);
    g_test_add_func("/latex_edit/valid_name", test_valid_name);
    return g_test_run();
}
