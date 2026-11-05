/* Unit tests for src/vt_pty.c: programs run on a pseudo-terminal. */
#include "vt_pty.h"

#include <string.h>

typedef struct {
    GString* out;
    GMainLoop* loop;
    gboolean exited;
} Run;

static void on_output(const gchar* data, gsize len, gpointer user_data) {
    Run* run = user_data;
    g_string_append_len(run->out, data, (gssize)len);
}

static void test_missing_program(void) {
    gchar* argv[] = {"no-such-program-lookin", NULL};
    GError* error = NULL;
    GPid pid = 0;
    VtPty* pty = vt_pty_spawn(argv, NULL, NULL, TRUE, 80, 24, on_output, NULL, &pid, &error);
    g_assert_null(pty);
    g_assert_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT);
    g_error_free(error);
}

#ifndef G_OS_WIN32
static void on_child_exit(GPid pid, gint status, gpointer user_data) {
    (void)status;
    Run* run = user_data;
    g_spawn_close_pid(pid);
    run->exited = TRUE;
    g_main_loop_quit(run->loop);
}

static gboolean on_timeout(gpointer user_data) {
    g_main_loop_quit(((Run*)user_data)->loop);
    return G_SOURCE_REMOVE;
}

/* Waits for text in the output (or the timeout) */
static gboolean wait_for(Run* run, const gchar* text) {
    gint64 end = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
    while (strstr(run->out->str, text) == NULL && g_get_monotonic_time() < end) {
        g_main_context_iteration(NULL, TRUE);
    }
    return strstr(run->out->str, text) != NULL;
}

static void test_output_input_and_size(void) {
    Run run = {g_string_new(NULL), g_main_loop_new(NULL, FALSE), FALSE};
    gchar* argv[] = {"sh", "-c", "stty size; echo \"term=$TERM\"; read line; echo \"got:$line\"", NULL};
    GError* error = NULL;
    GPid pid = 0;
    VtPty* pty = vt_pty_spawn(argv, "/", NULL, TRUE, 100, 30, on_output, &run, &pid, &error);
    g_assert_no_error(error);
    g_assert_nonnull(pty);
    g_child_watch_add(pid, on_child_exit, &run);
    g_assert_true(wait_for(&run, "term=xterm-256color"));
    g_assert_nonnull(strstr(run.out->str, "30 100"));
    vt_pty_write(pty, "hello\r", 6);
    g_assert_true(wait_for(&run, "got:hello"));
    guint timeout = g_timeout_add_seconds(10, on_timeout, &run);
    if (!run.exited) {
        g_main_loop_run(run.loop);
    }
    g_source_remove(timeout);
    g_assert_true(run.exited);
    vt_pty_free(pty);
    g_string_free(run.out, TRUE);
    g_main_loop_unref(run.loop);
}

static void test_free_hangs_up(void) {
    Run run = {g_string_new(NULL), g_main_loop_new(NULL, FALSE), FALSE};
    gchar* argv[] = {"sh", "-c", "echo ready; sleep 30", NULL};
    GPid pid = 0;
    VtPty* pty = vt_pty_spawn(argv, NULL, NULL, TRUE, 80, 24, on_output, &run, &pid, NULL);
    g_assert_nonnull(pty);
    g_child_watch_add(pid, on_child_exit, &run);
    g_assert_true(wait_for(&run, "ready"));
    vt_pty_free(pty);
    guint timeout = g_timeout_add_seconds(10, on_timeout, &run);
    g_main_loop_run(run.loop);
    g_source_remove(timeout);
    g_assert_true(run.exited);
    g_string_free(run.out, TRUE);
    g_main_loop_unref(run.loop);
}
#endif

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vt_pty/missing_program", test_missing_program);
#ifndef G_OS_WIN32
    g_test_add_func("/vt_pty/output_input_and_size", test_output_input_and_size);
    g_test_add_func("/vt_pty/free_hangs_up", test_free_hangs_up);
#endif
    return g_test_run();
}
