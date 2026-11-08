/* Unit tests for src/org/org_time.c: Org-mode timestamps. */
#include "org/org_time.h"

#include <string.h>

static GDateTime* fixed_now(void) {
    GTimeZone* tz = g_time_zone_new_utc();
    GDateTime* dt = g_date_time_new(tz, 2026, 10, 29, 9, 30, 0); /* a Thursday */
    g_time_zone_unref(tz);
    return dt;
}

static OrgTimestamp parse(const gchar* s) {
    OrgTimestamp ts;
    g_assert_true(org_timestamp_parse(s, strlen(s), &ts));
    return ts;
}

#define ASSERT_FORMAT(ts, expected)              \
    do {                                         \
        gchar* got_ = org_timestamp_format(ts);  \
        g_assert_cmpstr(got_, ==, expected);     \
        g_free(got_);                            \
    } while (0)

static void test_parse_format(void) {
    OrgTimestamp ts = parse("<2026-10-29 Thu 10:00-11:30 +1w -2d>");
    g_assert_true(ts.active);
    g_assert_cmpint(ts.year, ==, 2026);
    g_assert_cmpint(ts.month, ==, 10);
    g_assert_cmpint(ts.day, ==, 29);
    g_assert_true(ts.has_time && ts.has_end);
    g_assert_cmpint(ts.end_hour, ==, 11);
    g_assert_cmpstr(ts.repeat_kind, ==, "+");
    g_assert_cmpint(ts.repeat_n, ==, 1);
    g_assert_cmpint(ts.repeat_unit, ==, 'w');
    g_assert_cmpstr(ts.warn_kind, ==, "-");
    ASSERT_FORMAT(&ts, "<2026-10-29 Thu 10:00-11:30 +1w -2d>");

    /* Wrong day name is corrected; inactive */
    ts = parse("[2026-10-29 Mon]");
    g_assert_false(ts.active);
    ASSERT_FORMAT(&ts, "[2026-10-29 Thu]");
    /* No day name, .+ repeater */
    ts = parse("<2026-02-28 .+2d>");
    ASSERT_FORMAT(&ts, "<2026-02-28 Sat .+2d>");

    OrgTimestamp bad;
    g_assert_false(org_timestamp_parse("<2026-13-01>", 12, &bad));
    g_assert_false(org_timestamp_parse("<2026-10-29]", 12, &bad));
    g_assert_false(org_timestamp_parse("[[link]]", 8, &bad));
}

static void test_find_and_fields(void) {
    const gchar* line = "SCHEDULED: <2026-10-29 Thu 10:15 +1d> and [2026-01-02]";
    gint s, e;
    OrgTimestamp ts;
    g_assert_true(org_timestamp_find(line, 13, &s, &e, &ts));
    g_assert_cmpint(s, ==, 11);
    g_assert_cmpint(e, ==, 37);
    g_assert_cmpint(org_timestamp_field_at(&ts, 13 - s), ==, ORG_TS_FIELD_YEAR);
    g_assert_cmpint(org_timestamp_field_at(&ts, 17 - s), ==, ORG_TS_FIELD_MONTH);
    g_assert_cmpint(org_timestamp_field_at(&ts, 20 - s), ==, ORG_TS_FIELD_DAY);
    g_assert_cmpint(org_timestamp_field_at(&ts, 24 - s), ==, ORG_TS_FIELD_DAY); /* day name */
    g_assert_cmpint(org_timestamp_field_at(&ts, 27 - s), ==, ORG_TS_FIELD_HOUR);
    g_assert_cmpint(org_timestamp_field_at(&ts, 30 - s), ==, ORG_TS_FIELD_MINUTE);
    g_assert_cmpint(org_timestamp_field_at(&ts, 34 - s), ==, ORG_TS_FIELD_REPEAT);
    g_assert_true(org_timestamp_find(line, 50, &s, &e, &ts));
    g_assert_cmpint(s, ==, 42);
    g_assert_false(org_timestamp_find(line, 39, &s, &e, &ts));
}

static void test_add(void) {
    OrgTimestamp ts = parse("<2026-01-31 Sat>");
    org_timestamp_add(&ts, ORG_TS_FIELD_MONTH, 1);
    ASSERT_FORMAT(&ts, "<2026-02-28 Sat>");
    org_timestamp_add(&ts, ORG_TS_FIELD_DAY, 1);
    ASSERT_FORMAT(&ts, "<2026-03-01 Sun>");
    org_timestamp_add(&ts, ORG_TS_FIELD_YEAR, -1);
    ASSERT_FORMAT(&ts, "<2025-03-01 Sat>");
    ts = parse("<2026-12-31 Thu 23:50-23:55>");
    org_timestamp_add(&ts, ORG_TS_FIELD_MINUTE, 15);
    ASSERT_FORMAT(&ts, "<2027-01-01 Fri 00:05-00:10>");
    org_timestamp_add(&ts, ORG_TS_FIELD_HOUR, -1);
    ASSERT_FORMAT(&ts, "<2026-12-31 Thu 23:05-23:10>");
    ts = parse("<2026-10-29 Thu +1w>");
    org_timestamp_add(&ts, ORG_TS_FIELD_REPEAT, 2);
    ASSERT_FORMAT(&ts, "<2026-10-29 Thu +3w>");
}

static void test_repeater(void) {
    GDateTime* now = fixed_now();
    OrgTimestamp ts = parse("<2026-10-01 Thu +1m>");
    g_assert_true(org_timestamp_apply_repeater(&ts, now));
    ASSERT_FORMAT(&ts, "<2026-11-01 Sun +1m>");
    ts = parse("<2026-10-01 Thu ++1w>");
    g_assert_true(org_timestamp_apply_repeater(&ts, now));
    ASSERT_FORMAT(&ts, "<2026-11-05 Thu ++1w>"); /* first one after today */
    ts = parse("<2026-10-01 Thu 08:00 .+2d>");
    g_assert_true(org_timestamp_apply_repeater(&ts, now));
    ASSERT_FORMAT(&ts, "<2026-10-31 Sat 08:00 .+2d>");
    ts = parse("<2026-10-01 Thu>");
    g_assert_false(org_timestamp_apply_repeater(&ts, now));
    g_date_time_unref(now);
}

static void check_read(const gchar* input, const gchar* expected) {
    GDateTime* now = fixed_now();
    OrgTimestamp ts = {0};
    ts.active = TRUE;
    gboolean ok = org_timestamp_read(input, now, &ts);
    if (expected == NULL) {
        g_assert_false(ok);
    } else {
        g_assert_true(ok);
        ASSERT_FORMAT(&ts, expected);
    }
    g_date_time_unref(now);
}

static void test_read(void) {
    check_read("", "<2026-10-29 Thu>");
    check_read("+3", "<2026-11-01 Sun>");
    check_read("+2w", "<2026-11-12 Thu>");
    check_read("-1m", "<2026-09-29 Tue>");
    check_read("now", "<2026-10-29 Thu 09:30>");
    check_read("tomorrow", "<2026-10-30 Fri>");
    check_read("mon", "<2026-11-02 Mon>");
    check_read("thu", "<2026-11-05 Thu>");
    check_read("2027-01-05", "<2027-01-05 Tue>");
    check_read("12-25 18:00", "<2026-12-25 Fri 18:00>");
    check_read("14:30", "<2026-10-29 Thu 14:30>");
    check_read("9:00-10:30", "<2026-10-29 Thu 09:00-10:30>");
    check_read("2026-02-30", NULL);
    check_read("whenever", NULL);
}

static void test_minutes_between(void) {
    OrgTimestamp a = parse("[2026-10-29 Thu 23:30]");
    OrgTimestamp b = parse("[2026-10-30 Fri 01:05]");
    g_assert_cmpint(org_timestamp_minutes_between(&a, &b), ==, 95);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/org_time/parse_format", test_parse_format);
    g_test_add_func("/org_time/find_and_fields", test_find_and_fields);
    g_test_add_func("/org_time/add", test_add);
    g_test_add_func("/org_time/repeater", test_repeater);
    g_test_add_func("/org_time/read", test_read);
    g_test_add_func("/org_time/minutes_between", test_minutes_between);
    return g_test_run();
}
