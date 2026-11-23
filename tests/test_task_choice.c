/* Unit tests for src/tasks/task_choice.c: the task chosen in each project. */
#include "tasks/task_choice.h"

#include <glib/gstdio.h>

static void test_round_trip(void) {
    gchar* tmp = g_dir_make_tmp("task-choice-XXXXXX", NULL);
    gchar* path = g_build_filename(tmp, "sub", "tasks.ini", NULL);

    GHashTable* chosen = task_choice_load(path); /* missing: empty */
    g_assert_cmpuint(g_hash_table_size(chosen), ==, 0);
    const gchar* const make_argv[] = {"make", "-j8", "test; with = signs", NULL};
    g_hash_table_replace(chosen, g_strdup("/home/u/proj"), task_def_new("make -j8 test", make_argv, "/home/u/proj"));
    const gchar* const sh_argv[] = {"sh", "-c", "./run.sh --fast", NULL};
    TaskDef* own = task_def_new("fast tests", sh_argv, "/home/u/web/tests");
    const gchar* const env[] = {"CC=clang", "V=1", NULL};
    own->env = g_strdupv((gchar**)env);
    g_hash_table_replace(chosen, g_strdup("/home/u/web"), own);
    const gchar* const bad_argv[] = {"x", NULL};
    g_hash_table_replace(chosen, g_strdup("/odd[name]"), task_def_new("x", bad_argv, "/odd"));
    g_assert_true(task_choice_save(path, chosen, NULL));

    GHashTable* back = task_choice_load(path);
    g_assert_cmpuint(g_hash_table_size(back), ==, 2);
    g_assert_true(task_def_equal(g_hash_table_lookup(back, "/home/u/proj"), g_hash_table_lookup(chosen, "/home/u/proj")));
    const TaskDef* web = g_hash_table_lookup(back, "/home/u/web");
    g_assert_true(task_def_equal(web, own));
    g_assert_cmpstr(web->label, ==, "fast tests");
    g_assert_cmpstr(web->env[1], ==, "V=1");
    g_hash_table_unref(back);
    g_hash_table_unref(chosen);

    /* A group that cannot be read is skipped */
    g_assert_true(g_file_set_contents(path, "[/a]\nlabel=x\n[/b]\nlabel=y\ndir=/b\nargv=go;build;\n", -1, NULL));
    back = task_choice_load(path);
    g_assert_cmpuint(g_hash_table_size(back), ==, 1);
    g_assert_cmpstr(((TaskDef*)g_hash_table_lookup(back, "/b"))->argv[1], ==, "build");
    g_hash_table_unref(back);

    g_unlink(path);
    gchar* sub = g_path_get_dirname(path);
    g_rmdir(sub);
    g_rmdir(tmp);
    g_free(sub);
    g_free(path);
    g_free(tmp);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/task_choice/round_trip", test_round_trip);
    return g_test_run();
}
