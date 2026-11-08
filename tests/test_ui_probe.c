/* Unit tests for src/core/ui_probe.c: SIGUSR2 writes a counter once idle. */
#include "core/ui_probe.h"

#include <glib/gstdio.h>
#ifdef G_OS_UNIX
#include <signal.h>
#endif

static gchar* read_probe(const gchar* path) {
    gchar* text = NULL;
    g_file_get_contents(path, &text, NULL, NULL);
    return text;
}

static gboolean on_busy(gpointer data) {
    gint* runs = data;
    (*runs)++;
    return *runs < 3 ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

static void test_signal_counts(void) {
#ifdef G_OS_UNIX
    gchar* dir = g_dir_make_tmp("ui_probe_XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "probe", NULL);
    ui_probe_install_file(path);

    for (guint n = 1; n <= 2; n++) {
        /* Higher-priority work pending: the counter is written after it */
        gint runs = 0;
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, on_busy, &runs, NULL);
        raise(SIGUSR2);
        gchar* text = NULL;
        while ((text = read_probe(path)) == NULL || (guint)g_ascii_strtoull(text, NULL, 10) < n) {
            g_free(text);
            g_main_context_iteration(NULL, TRUE);
        }
        g_assert_cmpuint(g_ascii_strtoull(text, NULL, 10), ==, n);
        g_assert_cmpint(runs, ==, 3);
        g_free(text);
    }
    g_unlink(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
#endif
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ui_probe/signal_counts", test_signal_counts);
    return g_test_run();
}
