/* Unit tests for src/tasks/task_latex.c: the LaTeX tasks of a .tex file. */
#include "tasks/task_latex.h"

#include <glib/gstdio.h>
#include <string.h>

static gchar* tmp;

static gchar* put(const gchar* rel, const gchar* text) {
    gchar* path = g_build_filename(tmp, rel, NULL);
    gchar* dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_assert_true(g_file_set_contents(path, text, -1, NULL));
    g_free(dir);
    return path;
}

static void test_root(void) {
    gchar* main_tex = put("thesis/main.tex", "\\documentclass{book}\n\\begin{document}\n\\include{chapters/one}\n\\end{document}\n");
    gchar* one = put("thesis/chapters/one.tex", "\\chapter{One}\n");
    gchar* magic = put("thesis/appendix.tex", "% !TEX root = main.tex\n\\section{A}\n");
    gchar* lonely = put("other/notes.tex", "just text\n");

    gchar* root = task_latex_root(main_tex);
    g_assert_cmpstr(root, ==, main_tex);
    g_free(root);
    /* A chapter: the document of the folder above that includes it */
    root = task_latex_root(one);
    g_assert_cmpstr(root, ==, main_tex);
    g_free(root);
    root = task_latex_root(magic);
    g_assert_cmpstr(root, ==, main_tex);
    g_free(root);
    g_assert_null(task_latex_root(lonely));
    g_assert_null(task_latex_root("/no/such/file.tex"));
    g_assert_null(task_latex_root(tmp)); /* not a .tex file */

    gchar* pdf = task_latex_output(main_tex, ".pdf");
    gchar* want = g_build_filename(tmp, "thesis", "main.pdf", NULL);
    g_assert_cmpstr(pdf, ==, want);
    g_free(want);
    g_free(pdf);

    g_free(lonely);
    g_free(magic);
    g_free(one);
    g_free(main_tex);
}

static void test_tasks(void) {
    gchar* main_tex = put("x/main.tex", "\\documentclass{article}\n\\usepackage{fontspec}\n");
    GPtrArray* tasks = task_latex_tasks(main_tex);
    gchar* latexmk = g_find_program_in_path("latexmk");
    if (latexmk != NULL) {
        g_assert_cmpuint(tasks->len, ==, 2);
        const TaskDef* build = g_ptr_array_index(tasks, 0);
        g_assert_cmpstr(build->label, ==, "latexmk main.tex");
        g_assert_cmpstr(build->argv[0], ==, "latexmk");
        g_assert_cmpstr(build->argv[1], ==, "-pdfxe"); /* fontspec needs XeLaTeX */
        g_assert_true(g_strv_contains((const gchar* const*)build->argv, "-synctex=1"));
        g_assert_cmpstr(build->tex_root, ==, main_tex);
        gchar* dir = g_path_get_dirname(main_tex);
        g_assert_cmpstr(build->dir, ==, dir);
        g_free(dir);
        const TaskDef* clean = g_ptr_array_index(tasks, 1);
        g_assert_cmpstr(clean->label, ==, "latexmk -C main.tex");
        g_assert_null(clean->tex_root);
    } else {
        /* Tectonic or an engine, or nothing installed */
        g_assert_cmpuint(tasks->len, <=, 1);
        if (tasks->len == 1) g_assert_cmpstr(((TaskDef*)g_ptr_array_index(tasks, 0))->tex_root, ==, main_tex);
    }
    g_free(latexmk);
    g_ptr_array_unref(tasks);
    tasks = task_latex_tasks(NULL);
    g_assert_cmpuint(tasks->len, ==, 0);
    g_ptr_array_unref(tasks);
    g_free(main_tex);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    tmp = g_dir_make_tmp("task-latex-XXXXXX", NULL);
    g_test_add_func("/task_latex/root", test_root);
    g_test_add_func("/task_latex/tasks", test_tasks);
    int status = g_test_run();
    const gchar* rm_argv[] = {"rm", "-rf", tmp, NULL};
    g_spawn_sync(NULL, (gchar**)rm_argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, NULL);
    g_free(tmp);
    return status;
}
