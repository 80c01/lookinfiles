/* Unit tests for src/core/date_bins.c: the unit chosen for a span, the
 * groups of days, months and years, their bounds and labels. */
#include "core/date_bins.h"

static void test_day_of(void) {
    GDateTime* dt = g_date_time_new_local(2025, 3, 14, 15, 9, 26);
    gint64 t = g_date_time_to_unix(dt);
    g_assert_cmpint(date_bins_day_of(t), ==, 20250314);
    gint64 start, end;
    g_assert_cmpint(date_bins_day_span(t, &start, &end), ==, 20250314);
    g_assert_cmpint(start, <=, t);
    g_assert_cmpint(end, >, t);
    g_assert_cmpint(date_bins_day_of(start), ==, 20250314);
    g_assert_cmpint(date_bins_day_of(end - 1), ==, 20250314);
    g_assert_cmpint(date_bins_day_of(end), ==, 20250315);
    g_date_time_unref(dt);
}

static void test_span(void) {
    g_assert_cmpuint(date_bins_span(20250101, 20250101, DATE_BIN_DAY), ==, 1);
    g_assert_cmpuint(date_bins_span(20250228, 20250301, DATE_BIN_DAY), ==, 2);
    g_assert_cmpuint(date_bins_span(20241231, 20250101, DATE_BIN_DAY), ==, 2);
    g_assert_cmpuint(date_bins_span(202411, 202502, DATE_BIN_MONTH), ==, 4);
    g_assert_cmpuint(date_bins_span(2000, 2025, DATE_BIN_YEAR), ==, 26);
    g_assert_cmpuint(date_bins_span(2025, 2000, DATE_BIN_YEAR), ==, 0);
}

static void test_unit(void) {
    g_assert_cmpint(date_bins_unit(20251101, 20251119, 31), ==, DATE_BIN_DAY);
    g_assert_cmpint(date_bins_unit(20251001, 20251119, 31), ==, DATE_BIN_MONTH);
    g_assert_cmpint(date_bins_unit(20240101, 20251119, 24), ==, DATE_BIN_MONTH);
    g_assert_cmpint(date_bins_unit(20231101, 20251119, 24), ==, DATE_BIN_YEAR);
    g_assert_cmpint(date_bins_unit(19700101, 20251119, 24), ==, DATE_BIN_YEAR);
}

static void test_next(void) {
    g_assert_cmpint(date_bins_next(20250228, DATE_BIN_DAY), ==, 20250301);
    g_assert_cmpint(date_bins_next(20240228, DATE_BIN_DAY), ==, 20240229);
    g_assert_cmpint(date_bins_next(20251231, DATE_BIN_DAY), ==, 20260101);
    g_assert_cmpint(date_bins_next(202511, DATE_BIN_MONTH), ==, 202512);
    g_assert_cmpint(date_bins_next(202512, DATE_BIN_MONTH), ==, 202601);
    g_assert_cmpint(date_bins_next(2025, DATE_BIN_YEAR), ==, 2026);
    g_assert_cmpint(date_bins_key(20251119, DATE_BIN_MONTH), ==, 202511);
    g_assert_cmpint(date_bins_key(20251119, DATE_BIN_YEAR), ==, 2025);
}

static void test_days(void) {
    gint f, l;
    date_bins_days(20251119, DATE_BIN_DAY, &f, &l);
    g_assert_cmpint(f, ==, 20251119);
    g_assert_cmpint(l, ==, 20251119);
    date_bins_days(202402, DATE_BIN_MONTH, &f, &l);
    g_assert_cmpint(f, ==, 20240201);
    g_assert_cmpint(l, ==, 20240229);
    date_bins_days(202504, DATE_BIN_MONTH, &f, &l);
    g_assert_cmpint(l, ==, 20250430);
    date_bins_days(2025, DATE_BIN_YEAR, &f, &l);
    g_assert_cmpint(f, ==, 20250101);
    g_assert_cmpint(l, ==, 20251231);
}

static void test_labels(void) {
    gchar* s = date_bins_label(20251104, DATE_BIN_DAY);
    g_assert_cmpstr(s, ==, "11-04");
    g_free(s);
    s = date_bins_name(20251104, DATE_BIN_DAY);
    g_assert_cmpstr(s, ==, "2025-11-04");
    g_free(s);
    s = date_bins_label(202503, DATE_BIN_MONTH);
    g_assert_cmpstr(s, ==, "2025-03");
    g_free(s);
    s = date_bins_label(2025, DATE_BIN_YEAR);
    g_assert_cmpstr(s, ==, "2025");
    g_free(s);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/date_bins/day_of", test_day_of);
    g_test_add_func("/date_bins/span", test_span);
    g_test_add_func("/date_bins/unit", test_unit);
    g_test_add_func("/date_bins/next", test_next);
    g_test_add_func("/date_bins/days", test_days);
    g_test_add_func("/date_bins/labels", test_labels);
    return g_test_run();
}
