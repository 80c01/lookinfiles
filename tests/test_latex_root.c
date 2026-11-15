/* Unit tests for src/latex/latex_root.c: the main document of a LaTeX file. */
#include "latex/latex_root.h"

static void test_magic(void) {
    gchar* r = latex_root_magic("% !TEX root = ../thesis.tex\n\\section{Intro}\n");
    g_assert_cmpstr(r, ==, "../thesis.tex");
    g_free(r);
    r = latex_root_magic("  %!TeX root=main.tex\n");
    g_assert_cmpstr(r, ==, "main.tex");
    g_free(r);
    g_assert_null(latex_root_magic("\\section{Intro}\n% a comment\n"));
    g_assert_null(latex_root_magic("% !TEX root =\n"));
    g_assert_null(latex_root_magic(NULL));
}

static void test_is_main(void) {
    g_assert_true(latex_root_is_main("\\documentclass[a4paper]{article}\n\\begin{document}\n"));
    g_assert_true(latex_root_is_main("% comment\n  \\documentclass{report}"));
    g_assert_false(latex_root_is_main("% \\documentclass{article}\n\\section{A}\n"));
    g_assert_false(latex_root_is_main("100\\% \\documentclassx\n"));
    g_assert_true(latex_root_is_main("100\\% done\n\\documentclass{article}\n"));
    g_assert_false(latex_root_is_main(NULL));
}

static void test_engine(void) {
    g_assert_cmpint(latex_root_engine("\\documentclass{article}\n"), ==, LATEX_ENGINE_PDFLATEX);
    g_assert_cmpint(latex_root_engine("% !TEX program = xelatex\n\\documentclass{article}\n"), ==,
                    LATEX_ENGINE_XELATEX);
    g_assert_cmpint(latex_root_engine("% !TEX TS-program = LuaLaTeX\n"), ==, LATEX_ENGINE_LUALATEX);
    g_assert_cmpint(latex_root_engine("\\usepackage[no-math]{fontspec}\n"), ==, LATEX_ENGINE_XELATEX);
    g_assert_cmpint(latex_root_engine("\\usepackage{amsmath, unicode-math}\n"), ==, LATEX_ENGINE_XELATEX);
    g_assert_cmpint(latex_root_engine("\\usepackage{luacode}\n"), ==, LATEX_ENGINE_LUALATEX);
    g_assert_cmpint(latex_root_engine("% \\usepackage{fontspec}\n\\usepackage{fontspecx}\n"), ==,
                    LATEX_ENGINE_PDFLATEX);
    g_assert_cmpstr(latex_root_engine_name(LATEX_ENGINE_XELATEX), ==, "xelatex");
    g_assert_cmpstr(latex_root_engine_name(LATEX_ENGINE_PDFLATEX), ==, "pdflatex");
}

static void test_choose(void) {
    const gchar* names[] = {"article.tex", "main.tex", "slides.tex"};
    const gchar* texts[] = {
        "\\documentclass{article}\n\\input{other}\n",
        "\\documentclass{article}\n",
        "\\documentclass{beamer}\n\\include{intro}\n",
    };
    const gchar* stems[] = {"intro", "intro", "intro"};
    /* The one that includes the chapter */
    g_assert_cmpint(latex_root_choose(names, texts, stems, 3), ==, 2);
    /* Else main.tex */
    const gchar* other[] = {"conclusion", "conclusion", "conclusion"};
    g_assert_cmpint(latex_root_choose(names, texts, other, 3), ==, 1);
    /* The only one */
    g_assert_cmpint(latex_root_choose(names, texts, other, 1), ==, 0);
    /* None fits */
    const gchar* two[] = {"a.tex", "b.tex"};
    g_assert_cmpint(latex_root_choose(two, texts, other, 2), ==, -1);
    g_assert_cmpint(latex_root_choose(NULL, NULL, NULL, 0), ==, -1);

    /* \input forms: with ./ and .tex, without braces; not in a comment */
    const gchar* up[] = {"../thesis.tex", "../main.tex"};
    const gchar* up_texts[] = {
        "\\documentclass{book}\n\\input{./chapters/one.tex}\n",
        "\\documentclass{book}\n% \\input{chapters/two}\n",
    };
    const gchar* one[] = {"chapters/one", "chapters/one"};
    g_assert_cmpint(latex_root_choose(up, up_texts, one, 2), ==, 0);
    const gchar* two_stem[] = {"chapters/two", "chapters/two"};
    g_assert_cmpint(latex_root_choose(up, up_texts, two_stem, 2), ==, 1); /* main.tex */
    const gchar* plain[] = {"\\documentclass{book}\n\\input chapters/two\n", "\\documentclass{book}\n"};
    g_assert_cmpint(latex_root_choose(up, plain, two_stem, 2), ==, 0);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/latex_root/magic", test_magic);
    g_test_add_func("/latex_root/is_main", test_is_main);
    g_test_add_func("/latex_root/engine", test_engine);
    g_test_add_func("/latex_root/choose", test_choose);
    return g_test_run();
}
