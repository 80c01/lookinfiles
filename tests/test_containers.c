#include "core/containers.h"

#include <string.h>

static Container* at(GPtrArray* a, guint i) {
    return g_ptr_array_index(a, i);
}

static void test_parse(void) {
    const gchar* out =
        "a1b2c3d4e5f6|old-test|alpine:3.20|exited|Exited (0) 2 days ago|2024-05-01 10:00:00 +0200 CEST|\n"
        "0123456789ab|web|nginx:1.27|running|Up 3 hours (healthy)|2024-05-03 09:00:00 +0200 CEST|0.0.0.0:8080->80/tcp, [::]:8080->80/tcp\n"
        "ffffffffffff|db|postgres:16|running|Up 3 hours|2024-05-03 09:00:01 +0200 CEST|5432/tcp\n"
        "garbage line\n"
        "\n"
        "eeeeeeeeeeee|legacy|busybox||Up 5 minutes|2024-05-03|\n";
    GPtrArray* c = containers_parse(out);
    g_assert_cmpuint(c->len, ==, 4);
    g_assert_cmpstr(at(c, 0)->name, ==, "db");
    g_assert_true(at(c, 0)->running);
    g_assert_cmpstr(at(c, 1)->name, ==, "legacy"); /* empty .State: "Up ..." */
    g_assert_true(at(c, 1)->running);
    g_assert_cmpstr(at(c, 2)->name, ==, "web");
    g_assert_cmpstr(at(c, 2)->ports, ==, "0.0.0.0:8080->80/tcp, [::]:8080->80/tcp");
    g_assert_cmpstr(at(c, 2)->status, ==, "Up 3 hours (healthy)");
    g_assert_cmpstr(at(c, 3)->name, ==, "old-test");
    g_assert_false(at(c, 3)->running);
    g_assert_cmpstr(at(c, 3)->ports, ==, "");
    g_assert_cmpstr(containers_find(c, "0123456789ab")->name, ==, "web");
    g_assert_null(containers_find(c, "nope"));
    g_ptr_array_unref(c);
    c = containers_parse(NULL);
    g_assert_cmpuint(c->len, ==, 0);
    g_ptr_array_unref(c);
    /* An error message is not a list */
    c = containers_parse("permission denied while trying to connect to the Docker daemon socket\n");
    g_assert_cmpuint(c->len, ==, 0);
    g_ptr_array_unref(c);
}

static void test_commands(void) {
    gchar** argv = containers_list_argv(CONTAINER_ENGINE_PODMAN);
    g_assert_cmpstr(argv[0], ==, "podman");
    g_assert_cmpstr(argv[4], ==, CONTAINERS_FORMAT);
    g_strfreev(argv);
    gchar* line = containers_list_command(CONTAINER_ENGINE_DOCKER);
    g_assert_true(g_str_has_prefix(line, "command -v docker >/dev/null 2>&1 || exit 127; docker ps -a --format '"));
    g_free(line);
    argv = container_action_argv(CONTAINER_ENGINE_DOCKER, CONTAINER_STOP, "web");
    g_assert_cmpstr(argv[1], ==, "stop");
    g_assert_cmpstr(argv[2], ==, "web");
    g_strfreev(argv);
    line = container_action_command(CONTAINER_ENGINE_PODMAN, CONTAINER_RESTART, "a b");
    g_assert_cmpstr(line, ==, "podman restart 'a b' 2>&1");
    g_free(line);
    argv = container_shell_argv(CONTAINER_ENGINE_DOCKER, "web");
    g_assert_cmpstr(argv[1], ==, "exec");
    g_assert_cmpstr(argv[2], ==, "-it");
    g_assert_cmpstr(argv[3], ==, "web");
    g_strfreev(argv);
    line = container_shell_command(CONTAINER_ENGINE_DOCKER, "web");
    g_assert_true(g_str_has_prefix(line, "docker exec -it 'web' sh -c '"));
    g_free(line);
    g_assert_cmpint(container_engine_from_program("podman"), ==, CONTAINER_ENGINE_PODMAN);
    g_assert_cmpint(container_engine_from_program("lxc"), ==, -1);
    g_assert_cmpstr(container_engine_name(CONTAINER_ENGINE_DOCKER), ==, "Docker");
}

static void test_error_text(void) {
    gchar* t = containers_error_text("\n  Cannot connect to the Docker daemon. Is it running?\nmore\n");
    g_assert_cmpstr(t, ==, "Cannot connect to the Docker daemon. Is it running?");
    g_free(t);
    t = containers_error_text(NULL);
    g_assert_cmpstr(t, ==, "");
    g_free(t);
    gchar* longer = g_strnfill(300, 'x');
    t = containers_error_text(longer);
    g_assert_cmpint(g_utf8_strlen(t, -1), ==, 201);
    g_free(t);
    g_free(longer);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/containers/parse", test_parse);
    g_test_add_func("/containers/commands", test_commands);
    g_test_add_func("/containers/error_text", test_error_text);
    return g_test_run();
}
