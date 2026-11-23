/* Unit tests for src/tasks/task_detect.c: the tasks of a project. */
#include "tasks/task_detect.h"

#include <glib/gstdio.h>
#include <string.h>

static gchar* join(GPtrArray* names) {
    g_ptr_array_add(names, NULL);
    gchar* s = g_strjoinv(",", (gchar**)names->pdata);
    g_ptr_array_unref(names);
    return s;
}

static void test_make_targets(void) {
    const gchar* mk =
        "CC ?= gcc\n"
        "SRCS := $(wildcard src/*.c)\n"
        "BUILD_DIR = build\n"
        "all: lookin\n"
        "\n"
        "lookin: $(OBJS)\n"
        "\t$(CC) -o $@ $^\n"
        "$(BUILD_DIR)/%.o: src/%.c\n"
        "build/x.o: x.c\n"
        "test check: all\n"
        "\t./run\n"
        ".PHONY: all test check clean  # comment\n"
        "clean:\n"
        "\trm -rf build\n"
        "install:: all\n";
    gchar* s = join(task_detect_make_targets(mk));
    g_assert_cmpstr(s, ==, "all,test,check,clean");
    g_free(s);
    /* Without .PHONY: every plain rule */
    s = join(task_detect_make_targets("all: app\napp: main.o\nmain.o: main.c\ndocs:\n"));
    g_assert_cmpstr(s, ==, "all,app,docs");
    g_free(s);
    s = join(task_detect_make_targets(NULL));
    g_assert_cmpstr(s, ==, "");
    g_free(s);
}

static void test_npm_scripts(void) {
    const gchar* pkg =
        "{\n"
        "  \"name\": \"demo\",\n"
        "  \"scripts\": {\n"
        "    \"dev\": \"vite\",\n"
        "    \"build\": \"tsc && vite build\",\n"
        "    \"lint\": \"eslint \\\"src/**\\\"\"\n"
        "  },\n"
        "  \"dependencies\": {}\n"
        "}\n";
    gchar* s = join(task_detect_npm_scripts(pkg));
    g_assert_cmpstr(s, ==, "dev,build,lint");
    g_free(s);
    s = join(task_detect_npm_scripts("{\"scripts\": {}}"));
    g_assert_cmpstr(s, ==, "");
    g_free(s);
    s = join(task_detect_npm_scripts("{\"name\": \"x\"}"));
    g_assert_cmpstr(s, ==, "");
    g_free(s);
}

static void write_file(const gchar* dir, const gchar* name, const gchar* text) {
    gchar* path = g_build_filename(dir, name, NULL);
    gchar* parent = g_path_get_dirname(path);
    g_mkdir_with_parents(parent, 0700);
    g_assert_true(g_file_set_contents(path, text, -1, NULL));
    g_free(parent);
    g_free(path);
}

static gchar* labels(GPtrArray* tasks) {
    GPtrArray* names = g_ptr_array_new();
    for (guint i = 0; i < tasks->len; i++) g_ptr_array_add(names, ((TaskDef*)g_ptr_array_index(tasks, i))->label);
    g_ptr_array_add(names, NULL);
    gchar* s = g_strjoinv(",", (gchar**)names->pdata);
    g_ptr_array_unref(names);
    g_ptr_array_unref(tasks);
    return s;
}

static void rm_rf(const gchar* path) {
    if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
        GDir* d = g_dir_open(path, 0, NULL);
        const gchar* e;
        while (d != NULL && (e = g_dir_read_name(d)) != NULL) {
            gchar* sub = g_build_filename(path, e, NULL);
            rm_rf(sub);
            g_free(sub);
        }
        if (d != NULL) g_dir_close(d);
        g_rmdir(path);
    } else {
        g_unlink(path);
    }
}

static void test_projects(void) {
    gchar* tmp = g_dir_make_tmp("task-detect-XXXXXX", NULL);
    g_assert_nonnull(tmp);

    write_file(tmp, "c/Makefile", ".PHONY: all test\nall:\n\tcc x.c\ntest: all\n");
    write_file(tmp, "c/src/deep/x.c", "int x;\n");
    write_file(tmp, "web/package.json", "{\"scripts\": {\"dev\": \"vite\", \"build\": \"vite build\"}}");
    write_file(tmp, "web/pnpm-lock.yaml", "");
    write_file(tmp, "rs/Cargo.toml", "[workspace]\nmembers = [\"crates/a\"]\n");
    write_file(tmp, "rs/crates/a/Cargo.toml", "[package]\nname = \"a\"\n");
    write_file(tmp, "cm/CMakeLists.txt", "project(x)\n");
    write_file(tmp, "cm/build/CMakeCache.txt", "");
    write_file(tmp, "empty/readme.txt", "");

    gchar* start = g_build_filename(tmp, "c", "src", "deep", NULL);
    gchar* dir = task_detect_project_dir(start);
    gchar* want = g_build_filename(tmp, "c", NULL);
    g_assert_cmpstr(dir, ==, want);
    gchar* s = labels(task_detect(dir));
    /* Every processor: make -j<n> (plain make on a single one) */
    guint n = g_get_num_processors();
    gchar* want_labels = n > 1 ? g_strdup_printf("make -j%1$u,make -j%1$u all,make -j%1$u test", n)
                               : g_strdup("make,make all,make test");
    g_assert_cmpstr(s, ==, want_labels);
    g_free(want_labels);
    g_free(s);
    g_free(want);
    g_free(dir);
    g_free(start);

    dir = g_build_filename(tmp, "web", NULL);
    GPtrArray* tasks = task_detect(dir);
    const TaskDef* first = g_ptr_array_index(tasks, 0);
    g_assert_cmpstr(first->argv[0], ==, "pnpm");
    g_assert_cmpstr(first->argv[2], ==, "build");
    g_assert_cmpstr(first->dir, ==, dir);
    s = labels(tasks);
    g_assert_cmpstr(s, ==, "pnpm run build,pnpm run dev");
    g_free(s);
    g_free(dir);

    /* A crate of a Cargo workspace builds from the workspace's folder */
    dir = g_build_filename(tmp, "rs", "crates", "a", NULL);
    tasks = task_detect(dir);
    g_assert_cmpuint(tasks->len, ==, 5);
    gchar* root = g_build_filename(tmp, "rs", NULL);
    g_assert_cmpstr(((TaskDef*)g_ptr_array_index(tasks, 0))->dir, ==, root);
    g_assert_cmpstr(((TaskDef*)g_ptr_array_index(tasks, 0))->label, ==, "cargo build");
    TaskDef* copy = task_def_copy(g_ptr_array_index(tasks, 0));
    g_assert_true(task_def_equal(copy, g_ptr_array_index(tasks, 0)));
    g_assert_false(task_def_equal(copy, g_ptr_array_index(tasks, 1)));
    task_def_free(copy);
    g_ptr_array_unref(tasks);
    g_free(root);
    g_free(dir);

    dir = g_build_filename(tmp, "cm", NULL);
    s = labels(task_detect(dir));
    gchar* want_cmake = g_strdup_printf("cmake --build build --parallel %u", g_get_num_processors());
    g_assert_cmpstr(s, ==, want_cmake);
    g_free(want_cmake);
    g_free(s);
    g_free(dir);

    dir = g_build_filename(tmp, "empty", NULL);
    tasks = task_detect(dir);
    g_assert_cmpuint(tasks->len, ==, 0);
    g_ptr_array_unref(tasks);
    g_free(dir);

    rm_rf(tmp);
    g_free(tmp);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/task_detect/make_targets", test_make_targets);
    g_test_add_func("/task_detect/npm_scripts", test_npm_scripts);
    g_test_add_func("/task_detect/projects", test_projects);
    return g_test_run();
}
