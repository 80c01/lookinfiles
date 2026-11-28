/* Unit tests for src/code/code_scan_tex.c: LaTeX and BibTeX symbols and
 * outlines. */
#include "code/code_scan_tex.h"
#include "code/code_scan.h"

#include <string.h>

/* Symbols as "kind:name@line:column" (1-based line), separated by spaces */
static gchar* symbols(const gchar* text, const gchar* only) {
    GPtrArray* syms = code_scan_tex(text, strlen(text), only);
    GString* s = g_string_new(NULL);
    for (guint i = 0; i < syms->len; i++) {
        CodeSymbol* sym = g_ptr_array_index(syms, i);
        if (s->len > 0) g_string_append_c(s, ' ');
        const gchar* kind = sym->kind == CODE_SYM_LABEL       ? "label"
                            : sym->kind == CODE_SYM_REFERENCE ? "bib"
                            : sym->kind == CODE_SYM_MACRO     ? "macro"
                            : sym->kind == CODE_SYM_TYPE      ? "env"
                                                              : "other";
        g_string_append_printf(s, "%s:%s@%d:%d", kind, sym->name, sym->line + 1, sym->column);
    }
    g_ptr_array_unref(syms);
    return g_string_free(s, FALSE);
}

/* Outline entries as "level:name@line" (level 0 for a symbol) */
static gchar* outline(const gchar* text) {
    GPtrArray* entries = code_outline_tex(text, strlen(text));
    GString* s = g_string_new(NULL);
    for (guint i = 0; i < entries->len; i++) {
        CodeOutlineEntry* e = g_ptr_array_index(entries, i);
        if (s->len > 0) g_string_append_c(s, ' ');
        g_string_append_printf(s, "%d:%s@%d", e->level, e->name, e->line + 1);
    }
    g_ptr_array_unref(entries);
    return g_string_free(s, FALSE);
}

#define ASSERT_SYMBOLS(text, only, expected)        \
    do {                                            \
        gchar* got_ = symbols(text, only);          \
        g_assert_cmpstr(got_, ==, (expected));      \
        g_free(got_);                               \
    } while (0)

#define ASSERT_OUTLINE(text, expected)              \
    do {                                            \
        gchar* got_ = outline(text);                \
        g_assert_cmpstr(got_, ==, (expected));      \
        g_free(got_);                               \
    } while (0)

static void test_labels(void) {
    ASSERT_SYMBOLS("\\section{Intro}\\label{sec:intro}\nSee \\ref{sec:intro}.\n", NULL,
                   "label:sec:intro@1:22");
    ASSERT_SYMBOLS("  \\label{ fig:a-b }\n", NULL, "label:fig:a-b@1:10");
    /* Comments, escaped percent signs, a parameter of a definition */
    ASSERT_SYMBOLS("% \\label{old}\n100\\% \\label{new} % \\label{no}\n", NULL, "label:new@2:13");
    ASSERT_SYMBOLS("\\newcommand{\\fig}[1]{\\label{fig:#1}}\n", NULL, "macro:fig@1:13");
    /* Only one name */
    ASSERT_SYMBOLS("\\label{a}\\label{b}\\label{a}\n", "a", "label:a@1:7 label:a@1:25");
}

static void test_definitions(void) {
    ASSERT_SYMBOLS("\\newcommand{\\R}{\\mathbb{R}}\n\\renewcommand*\\vec[1]{#1}\n"
                   "\\def\\foo#1{x}\n\\DeclareMathOperator{\\Tr}{Tr}\n\\NewDocumentCommand{\\pair}{mm}{}\n",
                   NULL, "macro:R@1:13 macro:vec@2:15 macro:foo@3:5 macro:Tr@4:22 macro:pair@5:21");
    ASSERT_SYMBOLS("\\newenvironment{proofsketch}{}{}\n\\newtheorem{lemma}{Lemma}\n\\newtheorem*{remark}{Remark}\n",
                   NULL, "env:proofsketch@1:16 env:lemma@2:12 env:remark@3:13");
    ASSERT_SYMBOLS("\\begin{thebibliography}{9}\n\\bibitem[Knuth]{knuth84} D. Knuth\n", NULL, "bib:knuth84@2:16");
}

static void test_verbatim(void) {
    ASSERT_SYMBOLS("\\begin{verbatim}\n\\label{no}\n\\end{verbatim}\n\\label{yes}\n", NULL, "label:yes@4:7");
    ASSERT_SYMBOLS("\\begin{lstlisting}[language=C]\n\\label{no}\n\\end{lstlisting}\\label{yes}\n", NULL,
                   "label:yes@3:23");
    ASSERT_SYMBOLS("\\verb|\\label{no}| \\verb*+\\label{no2}+ \\label{yes}\n", NULL, "label:yes@1:45");
}

static void test_bibtex(void) {
    const gchar* bib = "@string{tug = \"TUGboat\"}\n"
                       "@Article{knuth:1984,\n  title = {Literate {P}rogramming},\n}\n"
                       "  @book { lamport94 ,\n}\n"
                       "@comment{ignored}\n"
                       "@misc(web-page, url = {x})\n";
    ASSERT_SYMBOLS(bib, NULL, "bib:knuth:1984@2:9 bib:lamport94@5:10 bib:web-page@8:6");
    /* An address in the text is no entry */
    ASSERT_SYMBOLS("mail me at name@example.org{x,\n", NULL, "");
}

static void test_outline(void) {
    ASSERT_OUTLINE("\\documentclass{book}\n\\newcommand{\\R}{}\n\\begin{document}\n"
                   "\\part{One}\n\\chapter[Short]{A long\n  title}\\label{ch:a}\n"
                   "\\section*{Intro to \\LaTeX}\n\\subsection{Sub}\n\\paragraph{Para}\n"
                   "% \\section{Commented}\n\\end{document}\n",
                   "0:R@2 1:One@4 2:A long title@5 0:ch:a@6 3:Intro to \\LaTeX@7 4:Sub@8 6:Para@9");
    /* Beamer frames: the title on the line of \begin{frame}, else \frametitle */
    ASSERT_OUTLINE("\\section{S}\n\\begin{frame}<1->[fragile]{First}\n\\end{frame}\n"
                   "\\begin{frame}\n\\frametitle{Second}\n\\end{frame}\n"
                   "\\begin{frame}\n\\end{frame}\n\\frametitle{Outside}\n",
                   "3:S@1 8:First@2 8:Second@5");
    /* A heading inside a definition is not one */
    ASSERT_OUTLINE("\\newcommand{\\chap}[1]{\\chapter{#1}}\n", "0:chap@1");
}

static void test_tree(void) {
    const gchar* text = "\\newcommand{\\R}{}\n\\section{A}\n\\label{a}\n\\subsection{B}\n\\label{b}\n\\section{C}\n";
    GPtrArray* entries = code_outline_tex(text, strlen(text));
    GPtrArray* roots = code_outline_tree(entries, CODE_OUTLINE_BY_KIND);
    /* Sections A and C, then the Macros group */
    g_assert_cmpuint(roots->len, ==, 3);
    CodeOutlineNode* a = g_ptr_array_index(roots, 0);
    g_assert_cmpuint(a->children->len, ==, 2); /* label a, subsection B */
    CodeOutlineNode* b = g_ptr_array_index(a->children, 1);
    g_assert_cmpuint(b->children->len, ==, 1); /* label b */
    CodeOutlineNode* group = g_ptr_array_index(roots, 2);
    g_assert_cmpint(group->entry, ==, -1);
    g_assert_cmpint(group->group, ==, CODE_SYM_MACRO);
    g_ptr_array_unref(roots);
    g_ptr_array_unref(entries);
}

static void test_robustness(void) {
    ASSERT_SYMBOLS("\\label{unclosed\n\\section{x", NULL, "");
    ASSERT_SYMBOLS("\\", NULL, "");
    ASSERT_SYMBOLS("\\label", NULL, "");
    ASSERT_SYMBOLS("\\newcommand{", NULL, "");
    ASSERT_SYMBOLS("@", NULL, "");
    ASSERT_SYMBOLS("\\begin{verbatim}\n\\label{x}", NULL, "");
    ASSERT_OUTLINE("\\section", "");
    ASSERT_OUTLINE("\\section{\xff\xfe bad}", "3:\xef\xbf\xbd\xef\xbf\xbd bad@1");
}

static void test_through_code_scan(void) {
    const gchar* text = "\\label{sec:x}\n";
    g_assert_true(code_scan_supported(CODE_LANG_LATEX));
    g_assert_true(code_scan_preferred(CODE_LANG_LATEX));
    g_assert_false(code_scan_preferred(CODE_LANG_C));
    GPtrArray* syms = code_scan_text(text, strlen(text), CODE_LANG_LATEX, "sec:x");
    g_assert_cmpuint(syms->len, ==, 1);
    g_ptr_array_unref(syms);
    /* \ref{sec:x}: the label is the word under the cursor */
    gint s = 0, e = 0;
    g_assert_true(code_word_at("see \\ref{sec:x-y}.", -1, 11, CODE_LANG_LATEX, &s, &e));
    g_assert_cmpint(s, ==, 9);
    g_assert_cmpint(e, ==, 16);
    /* The comment above a definition */
    const gchar* doc = "% The real numbers\n%% (blackboard bold)\n\\newcommand{\\R}{\\mathbb{R}}\n";
    gchar* comment = code_scan_doc_comment(doc, strlen(doc), CODE_LANG_LATEX, 2);
    g_assert_nonnull(comment);
    g_assert_nonnull(strstr(comment, "The real numbers"));
    g_assert_null(strchr(comment, '%'));
    g_free(comment);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/code_scan_tex/labels", test_labels);
    g_test_add_func("/code_scan_tex/definitions", test_definitions);
    g_test_add_func("/code_scan_tex/verbatim", test_verbatim);
    g_test_add_func("/code_scan_tex/bibtex", test_bibtex);
    g_test_add_func("/code_scan_tex/outline", test_outline);
    g_test_add_func("/code_scan_tex/tree", test_tree);
    g_test_add_func("/code_scan_tex/robustness", test_robustness);
    g_test_add_func("/code_scan_tex/through_code_scan", test_through_code_scan);
    return g_test_run();
}
