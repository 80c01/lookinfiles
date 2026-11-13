/* Unit tests for src/core/countdown.c: the repeating countdown */
#include <glib.h>
#include "core/countdown.h"

#define S COUNTDOWN_SECOND
#define T0 (G_GINT64_CONSTANT(1700000000) * S)

static void test_count_and_restart(void) {
    Countdown c;
    countdown_start(&c, 60 * 60 * S, T0);
    g_assert_cmpint(countdown_left(&c, T0), ==, 3600 * S);
    g_assert_cmpint(countdown_left(&c, T0 + 10 * S), ==, 3590 * S);
    g_assert_cmpuint(c.cycle, ==, 1);
    /* Expired: restarts at once with the full duration */
    g_assert_cmpint(countdown_left(&c, T0 + 3600 * S), ==, 3600 * S);
    g_assert_cmpuint(c.cycle, ==, 2);
    /* Several cycles missed (suspend): skipped whole */
    g_assert_cmpint(countdown_left(&c, T0 + 4 * 3600 * S + 5 * S), ==, 3595 * S);
    g_assert_cmpuint(c.cycle, ==, 5);
}

static void test_clock_back(void) {
    Countdown c;
    countdown_start(&c, 60 * S, T0);
    g_assert_cmpint(countdown_left(&c, T0 - 3600 * S), ==, 60 * S);
    g_assert_cmpint(countdown_left(&c, T0 - 3600 * S + 30 * S), ==, 30 * S);
}

static void test_pause(void) {
    Countdown c;
    countdown_start(&c, 60 * S, T0);
    countdown_pause(&c, T0 + 20 * S);
    g_assert_cmpint(countdown_left(&c, T0 + 500 * S), ==, 40 * S);
    countdown_resume(&c, T0 + 500 * S);
    g_assert_cmpint(countdown_left(&c, T0 + 510 * S), ==, 30 * S);
    countdown_resume(&c, T0 + 520 * S); /* already running: no effect */
    g_assert_cmpint(countdown_left(&c, T0 + 520 * S), ==, 20 * S);
    g_assert_cmpuint(c.cycle, ==, 1);
}

static void test_minimum_duration(void) {
    Countdown c;
    countdown_start(&c, 0, T0);
    g_assert_cmpint(c.duration, ==, S);
}

static void test_seconds_and_format(void) {
    g_assert_cmpint(countdown_seconds(3600 * S), ==, 3600);
    g_assert_cmpint(countdown_seconds(3599 * S + 1), ==, 3600);
    g_assert_cmpint(countdown_seconds(1), ==, 1);
    g_assert_cmpint(countdown_seconds(0), ==, 0);

    gchar* text = countdown_format(3600 * S, 3600 * S);
    g_assert_cmpstr(text, ==, "60:00");
    g_free(text);
    text = countdown_format(65 * S - 1, 3600 * S);
    g_assert_cmpstr(text, ==, "1:05");
    g_free(text);
    text = countdown_format(3600 * S, 120 * 60 * S);
    g_assert_cmpstr(text, ==, "1:00:00");
    g_free(text);
    text = countdown_format(59 * S, 120 * 60 * S);
    g_assert_cmpstr(text, ==, "0:00:59");
    g_free(text);
}

static void test_inverted(void) {
    g_assert_false(countdown_inverted(61 * S, TRUE));
    g_assert_true(countdown_inverted(60 * S, TRUE));
    g_assert_true(countdown_inverted(11 * S - S / 4, TRUE)); /* steady before the last 10 s */
    /* Last 10 s: reverse in the first half of each second shown */
    g_assert_true(countdown_inverted(10 * S, TRUE));
    g_assert_true(countdown_inverted(9 * S + S / 2 + 1, TRUE));
    g_assert_false(countdown_inverted(9 * S + S / 2, TRUE));
    g_assert_false(countdown_inverted(9 * S + 1, TRUE));
    g_assert_true(countdown_inverted(9 * S, TRUE));
    /* No blinking: steady */
    g_assert_true(countdown_inverted(9 * S + 1, FALSE));
}

static void test_next_change(void) {
    g_assert_cmpint(countdown_next_change(3600 * S, TRUE), ==, S);
    g_assert_cmpint(countdown_next_change(30 * S + S / 4, TRUE), ==, S / 4);
    g_assert_cmpint(countdown_next_change(10 * S, TRUE), ==, S / 2);
    g_assert_cmpint(countdown_next_change(9 * S + S / 2, TRUE), ==, S / 2);
    g_assert_cmpint(countdown_next_change(10 * S, FALSE), ==, S);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/countdown/count_and_restart", test_count_and_restart);
    g_test_add_func("/countdown/clock_back", test_clock_back);
    g_test_add_func("/countdown/pause", test_pause);
    g_test_add_func("/countdown/minimum_duration", test_minimum_duration);
    g_test_add_func("/countdown/seconds_and_format", test_seconds_and_format);
    g_test_add_func("/countdown/inverted", test_inverted);
    g_test_add_func("/countdown/next_change", test_next_change);
    return g_test_run();
}
