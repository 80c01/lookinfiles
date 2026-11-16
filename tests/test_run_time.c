/* Unit tests for src/core/run_time.c: the time a cell run took */
#include <glib.h>
#include "core/run_time.h"

#define MS G_GINT64_CONSTANT(1000)
#define S G_GINT64_CONSTANT(1000000)

static void check(gint64 usec, const gchar* expected) {
    gchar* text = run_time_format(usec);
    g_assert_cmpstr(text, ==, expected);
    g_free(text);
}

static void test_seconds(void) {
    check(0, "0.0s");
    check(-5 * S, "0.0s");
    check(99 * MS, "0.0s");
    check(450 * MS, "0.4s");
    check(12 * S + 399 * MS, "12.3s");
    check(59 * S + 999 * MS, "59.9s");
}

static void test_minutes_and_hours(void) {
    check(60 * S, "1m 00s");
    check(65 * S + 900 * MS, "1m 05s");
    check(3599 * S, "59m 59s");
    check(3600 * S, "1h 00m");
    check(2 * 3600 * S + 3 * 60 * S + 59 * S, "2h 03m");
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/run_time/seconds", test_seconds);
    g_test_add_func("/run_time/minutes_and_hours", test_minutes_and_hours);
    return g_test_run();
}
