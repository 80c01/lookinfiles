/* Unit tests for src/tasks/task_config.c: the tasks of .lookinfiles/tasks.toml. */
#include "tasks/task_config.h"

#include <string.h>

static const TaskDef* task(GPtrArray* tasks, guint i) {
    g_assert_cmpuint(i, <, tasks->len);
    return g_ptr_array_index(tasks, i);
}

static void test_tasks(void) {
    const gchar* text =
        "# The project's tasks\n"
        "[[task]]\n"
        "name = \"fast tests\"\n"
        "command = \"./run_tests.sh --fast && echo \\\"done\\\"\"  # through the shell\n"
        "cwd = \"tests\"\n"
        "env = { CC = \"clang\", \"V\" = '1' }\n"
        "\n"
        "[[task]]\n"
        "name = 'install'\n"
        "args = [\n"
        "    \"make\",  # the program\n"
        "    \"-C\", 'build',\n"
        "    \"install\",\n"
        "]\n"
        "matcher = '^(?<file>[^:]+):(?<line>\\d+): (?<message>.*)$'\n"
        "default = true\n";
    GError* error = NULL;
    GPtrArray* tasks = task_config_parse(text, "/proj", &error);
    g_assert_no_error(error);
    g_assert_cmpuint(tasks->len, ==, 2);
    /* The default one first */
    const TaskDef* install = task(tasks, 0);
    g_assert_cmpstr(install->label, ==, "install");
    g_assert_cmpstr(install->argv[0], ==, "make");
    g_assert_cmpstr(install->argv[2], ==, "build");
    g_assert_cmpstr(install->argv[3], ==, "install");
    g_assert_null(install->argv[4]);
    g_assert_cmpstr(install->detail, ==, "make -C build install");
    g_assert_cmpstr(install->dir, ==, "/proj");
    g_assert_cmpstr(install->matcher, ==, "^(?<file>[^:]+):(?<line>\\d+): (?<message>.*)$");
    g_assert_null(install->env);
    const TaskDef* fast = task(tasks, 1);
    g_assert_cmpstr(fast->label, ==, "fast tests");
#ifndef G_OS_WIN32
    g_assert_cmpstr(fast->argv[0], ==, "sh");
    g_assert_cmpstr(fast->argv[1], ==, "-c");
    g_assert_cmpstr(fast->argv[2], ==, "./run_tests.sh --fast && echo \"done\"");
#endif
    g_assert_cmpstr(fast->detail, ==, "./run_tests.sh --fast && echo \"done\"");
    g_assert_cmpstr(fast->dir, ==, "/proj/tests");
    g_assert_cmpstr(fast->env[0], ==, "CC=clang");
    g_assert_cmpstr(fast->env[1], ==, "V=1");
    g_assert_null(fast->env[2]);
    g_ptr_array_unref(tasks);

    tasks = task_config_parse("", "/proj", &error);
    g_assert_no_error(error);
    g_assert_cmpuint(tasks->len, ==, 0);
    g_ptr_array_unref(tasks);
}

/* text is refused, the error naming line */
static void refused(const gchar* text, gint line, const gchar* part) {
    GError* error = NULL;
    GPtrArray* tasks = task_config_parse(text, "/proj", &error);
    g_assert_null(tasks);
    g_assert_nonnull(error);
    gchar* where = g_strdup_printf("%d", line);
    if (strstr(error->message, where) == NULL || strstr(error->message, part) == NULL) {
        g_printerr("unexpected error for %s: %s\n", text, error->message);
        g_assert_not_reached();
    }
    g_free(where);
    g_error_free(error);
}

static void test_errors(void) {
    refused("name = \"x\"\n", 1, "[[task]]");
    refused("[[task]]\nname = \"x\"\n", 1, "command");
    refused("[[task]]\nname = \"x\"\ncommand = \"a\"\nargs = [\"b\"]\n", 1, "command");
    refused("[[task]]\ncommand = \"a\"\n", 1, "name");
    refused("[[task]]\nname = \"x\"\ncommand = \"a\nb\"\n", 3, "closed");
    refused("[[task]]\nname = \"x\"\ncomand = \"a\"\n", 3, "comand");
    refused("[[task]]\nname = \"x\"\nname = \"y\"\n", 3, "twice");
    refused("[[task]]\nname = \"x\"\nargs = \"make\"\n", 3, "array");
    refused("[[task]]\nname = \"x\"\ndefault = \"yes\"\n", 3, "true");
    refused("[task]\n", 1, "[[task]]");
    refused("[[task]]\nname = \"x\" extra\n", 2, "after");
    refused("[[task]]\nname = \"x\"\ncommand = \"a\"\nmatcher = \"(\"\n", 1, "regular expression");
    refused("[[task]]\nname = \"x\"\ncommand = \"a\"\nmatcher = \"^(?<file>.*)$\"\n", 1, "line");
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/task_config/tasks", test_tasks);
    g_test_add_func("/task_config/errors", test_errors);
    return g_test_run();
}
