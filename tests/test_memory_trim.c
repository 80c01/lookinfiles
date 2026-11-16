/* Unit tests of the trim of free memory (src/core/memory_trim.c) */
#include "core/memory_trim.h"

static gboolean quit_loop(gpointer data) {
    g_main_loop_quit(data);
    return G_SOURCE_REMOVE;
}

/* Run the main loop for ms milliseconds */
static void run_for(guint ms) {
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    g_timeout_add(ms, quit_loop, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
}

static void test_debounced(void) {
#if defined(__GLIBC__)
    memory_trim_set_delay_ms(50);
    guint before = memory_trim_count();
    /* Free scattered blocks, as a preview does */
    GPtrArray* blocks = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < 2000; i++) g_ptr_array_add(blocks, g_malloc(16 * 1024));
    g_ptr_array_unref(blocks);

    /* Calls closer than the delay: one trim, after the last one */
    for (guint i = 0; i < 5; i++) {
        memory_trim_soon();
        run_for(20);
    }
    g_assert_cmpuint(memory_trim_count(), ==, before);
    run_for(300);
    g_assert_cmpuint(memory_trim_count(), ==, before + 1);

    /* Nothing more without a new call */
    run_for(150);
    g_assert_cmpuint(memory_trim_count(), ==, before + 1);
    memory_trim_soon();
    run_for(300);
    g_assert_cmpuint(memory_trim_count(), ==, before + 2);
#else
    g_test_skip("malloc_trim is glibc only");
#endif
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/memory_trim/debounced", test_debounced);
    return g_test_run();
}
