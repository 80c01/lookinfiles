/* Unit tests for src/search/deep_query.c: the criteria of a deep search. */
#include "search/deep_query.h"

static void test_size(void) {
    gint64 v = 0;
    g_assert_true(deep_query_parse_size("", &v));
    g_assert_cmpint(v, ==, -1);
    g_assert_true(deep_query_parse_size("  ", &v));
    g_assert_cmpint(v, ==, -1);
    g_assert_true(deep_query_parse_size("42", &v));
    g_assert_cmpint(v, ==, 42);
    g_assert_true(deep_query_parse_size("500k", &v));
    g_assert_cmpint(v, ==, 500 * 1024);
    g_assert_true(deep_query_parse_size("1.5 MB", &v));
    g_assert_cmpint(v, ==, 1536 * 1024);
    g_assert_true(deep_query_parse_size("2G", &v));
    g_assert_cmpint(v, ==, G_GINT64_CONSTANT(2147483648));
    g_assert_true(deep_query_parse_size("3 KiB", &v));
    g_assert_cmpint(v, ==, 3072);
    g_assert_true(deep_query_parse_size("10b", &v));
    g_assert_cmpint(v, ==, 10);
    g_assert_false(deep_query_parse_size("abc", &v));
    g_assert_false(deep_query_parse_size("10 x", &v));
    g_assert_false(deep_query_parse_size("-5", &v));
}

static void test_date(void) {
    GTimeZone* utc = g_time_zone_new_utc();
    GDateTime* now = g_date_time_new(utc, 2026, 11, 19, 15, 30, 0.0);
    gint64 base = g_date_time_to_unix(now);
    gint64 v = 1;
    g_assert_true(deep_query_parse_date("", now, FALSE, &v));
    g_assert_cmpint(v, ==, 0);
    g_assert_true(deep_query_parse_date("7d", now, FALSE, &v));
    g_assert_cmpint(v, ==, base - 7 * 86400);
    g_assert_true(deep_query_parse_date("3 h", now, FALSE, &v));
    g_assert_cmpint(v, ==, base - 3 * 3600);
    g_assert_true(deep_query_parse_date("30min", now, FALSE, &v));
    g_assert_cmpint(v, ==, base - 1800);
    g_assert_true(deep_query_parse_date("2w", now, FALSE, &v));
    g_assert_cmpint(v, ==, base - 14 * 86400);
    /* Months and years by the calendar */
    GDateTime* t = g_date_time_new(utc, 2026, 5, 19, 15, 30, 0.0);
    g_assert_true(deep_query_parse_date("6m", now, FALSE, &v));
    g_assert_cmpint(v, ==, g_date_time_to_unix(t));
    g_date_time_unref(t);
    t = g_date_time_new(utc, 2025, 11, 19, 15, 30, 0.0);
    g_assert_true(deep_query_parse_date("1 year", now, FALSE, &v));
    g_assert_cmpint(v, ==, g_date_time_to_unix(t));
    g_date_time_unref(t);
    /* Days: their start, or the start of the next one */
    GDateTime* day = g_date_time_new(utc, 2026, 11, 1, 0, 0, 0.0);
    gint64 nov1 = g_date_time_to_unix(day);
    g_date_time_unref(day);
    g_assert_true(deep_query_parse_date("2026-11-01", now, FALSE, &v));
    g_assert_cmpint(v, ==, nov1);
    g_assert_true(deep_query_parse_date("2026-11-01", now, TRUE, &v));
    g_assert_cmpint(v, ==, nov1 + 86400);
    g_assert_true(deep_query_parse_date("2026-11-01 14:30", now, TRUE, &v));
    g_assert_cmpint(v, ==, nov1 + 14 * 3600 + 1800);
    gint64 today = base - (15 * 3600 + 1800);
    g_assert_true(deep_query_parse_date("Today", now, FALSE, &v));
    g_assert_cmpint(v, ==, today);
    g_assert_true(deep_query_parse_date("today", now, TRUE, &v));
    g_assert_cmpint(v, ==, today + 86400);
    g_assert_true(deep_query_parse_date("yesterday", now, FALSE, &v));
    g_assert_cmpint(v, ==, today - 86400);
    g_assert_true(deep_query_parse_date("yesterday", now, TRUE, &v));
    g_assert_cmpint(v, ==, today);
    g_assert_false(deep_query_parse_date("2026-13-01", now, FALSE, &v));
    g_assert_false(deep_query_parse_date("7 lightyears", now, FALSE, &v));
    g_assert_false(deep_query_parse_date("soon", now, FALSE, &v));
    g_assert_false(deep_query_parse_date("2026-11-01 25:00", now, FALSE, &v));
    g_date_time_unref(now);
    g_time_zone_unref(utc);
}

static gboolean match(const DeepQuery* q, const gchar* name, gboolean is_dir, guint64 size, gint64 mtime) {
    DeepFilter* f = deep_filter_new(q);
    g_assert_nonnull(f);
    gboolean hit = deep_filter_match_entry(f, name, is_dir, size, mtime);
    deep_filter_free(f);
    return hit;
}

static void test_name(void) {
    DeepQuery q;
    deep_query_init(&q);
    g_assert_true(match(&q, "anything", FALSE, 0, 0));
    g_assert_true(match(&q, "folder", TRUE, 0, 0));
    /* A part of the name, any case */
    q.name = g_strdup("Report");
    g_assert_true(match(&q, "2026-annual-report.pdf", FALSE, 0, 0));
    g_assert_true(match(&q, "REPORTS", TRUE, 0, 0));
    g_assert_false(match(&q, "notes.txt", FALSE, 0, 0));
    g_free(q.name);
    /* A pattern of the whole name */
    q.name = g_strdup("img_????.JPG");
    g_assert_true(match(&q, "IMG_0042.jpg", FALSE, 0, 0));
    g_assert_false(match(&q, "IMG_42.jpg", FALSE, 0, 0));
    g_assert_false(match(&q, "old-IMG_0042.jpg", FALSE, 0, 0));
    g_free(q.name);
    q.name = g_strdup("*.tar.gz");
    g_assert_true(match(&q, "backup.tar.gz", FALSE, 0, 0));
    g_assert_false(match(&q, "backup.tgz", FALSE, 0, 0));
    deep_query_clear(&q);
    /* Accents compare folded */
    q.name = g_strdup("ÉTÉ");
    g_assert_true(match(&q, "photos-été.png", FALSE, 0, 0));
    deep_query_clear(&q);
}

static void test_extensions(void) {
    DeepQuery q;
    deep_query_init(&q);
    q.extensions = g_strdup(" c, .H ; *.md tar.gz");
    g_assert_true(match(&q, "main.c", FALSE, 0, 0));
    g_assert_true(match(&q, "MAIN.C", FALSE, 0, 0));
    g_assert_true(match(&q, "x.h", FALSE, 0, 0));
    g_assert_true(match(&q, "README.md", FALSE, 0, 0));
    g_assert_true(match(&q, "a.tar.gz", FALSE, 0, 0));
    g_assert_false(match(&q, "a.gz", FALSE, 0, 0));
    g_assert_false(match(&q, "main.cpp", FALSE, 0, 0));
    g_assert_false(match(&q, ".c", FALSE, 0, 0)); /* a hidden name, no extension */
    g_assert_false(match(&q, "src.c", TRUE, 0, 0)); /* folders have none */
    deep_query_clear(&q);
    q.extensions = g_strdup(" , ");
    g_assert_true(match(&q, "folder", TRUE, 0, 0)); /* nothing given */
    deep_query_clear(&q);
}

static void test_family(void) {
    DeepQuery q;
    deep_query_init(&q);
    g_assert_cmpint(q.family, ==, -1);
    q.family = FILE_FAMILY_IMAGES;
    g_assert_true(match(&q, "IMG_0042.JPG", FALSE, 0, 0));
    g_assert_true(match(&q, "drawing.svg", FALSE, 0, 0));
    g_assert_false(match(&q, "clip.mp4", FALSE, 0, 0));
    g_assert_false(match(&q, "pictures", TRUE, 0, 0)); /* folders have none */
    /* With the name: both */
    q.name = g_strdup("holiday");
    g_assert_true(match(&q, "holiday-1.png", FALSE, 0, 0));
    g_assert_false(match(&q, "holiday.mp4", FALSE, 0, 0));
    g_assert_false(match(&q, "work.png", FALSE, 0, 0));
    g_clear_pointer(&q.name, g_free);
    /* Never with extensions */
    q.extensions = g_strdup("xcf");
    gchar* why = deep_query_check(&q);
    g_assert_nonnull(why);
    g_free(why);
    g_assert_null(deep_filter_new(&q));
    deep_query_clear(&q);
    q.family = FILE_FAMILY_VIDEOS;
    g_assert_true(match(&q, "clip.mkv", FALSE, 0, 0));
    deep_query_clear(&q);
}

static void test_kind_size_date(void) {
    DeepQuery q;
    deep_query_init(&q);
    q.kind = DEEP_KIND_FOLDERS;
    g_assert_true(match(&q, "src", TRUE, 0, 0));
    g_assert_false(match(&q, "a.c", FALSE, 0, 0));
    q.kind = DEEP_KIND_FILES;
    g_assert_false(match(&q, "src", TRUE, 0, 0));
    g_assert_true(match(&q, "a.c", FALSE, 0, 0));
    q.kind = DEEP_KIND_ANY;
    /* Sizes: inclusive, files only */
    q.min_size = 100;
    q.max_size = 200;
    g_assert_false(match(&q, "a", FALSE, 99, 0));
    g_assert_true(match(&q, "a", FALSE, 100, 0));
    g_assert_true(match(&q, "a", FALSE, 200, 0));
    g_assert_false(match(&q, "a", FALSE, 201, 0));
    g_assert_false(match(&q, "d", TRUE, 150, 0));
    q.min_size = q.max_size = -1;
    /* Dates: from after (included) to before (excluded), folders too */
    q.after = 1000;
    q.before = 2000;
    g_assert_false(match(&q, "a", FALSE, 0, 999));
    g_assert_true(match(&q, "a", FALSE, 0, 1000));
    g_assert_true(match(&q, "d", TRUE, 0, 1999));
    g_assert_false(match(&q, "a", FALSE, 0, 2000));
    q.after = q.before = 0;
    /* A content: files only */
    q.content = g_strdup("needle");
    DeepFilter* f = deep_filter_new(&q);
    g_assert_true(deep_filter_has_content(f));
    g_assert_false(deep_filter_match_entry(f, "d", TRUE, 0, 0));
    g_assert_true(deep_filter_match_entry(f, "a", FALSE, 0, 0));
    deep_filter_free(f);
    deep_query_clear(&q);
    f = deep_filter_new(&q);
    g_assert_false(deep_filter_has_content(f));
    deep_filter_free(f);
}

static void test_exclude(void) {
    DeepQuery q;
    deep_query_init(&q);
    DeepFilter* f = deep_filter_new(&q);
    g_assert_false(deep_filter_excluded(f, "node_modules", TRUE));
    deep_filter_free(f);
    q.exclude = g_strdup("node_modules, build/, *.o");
    f = deep_filter_new(&q);
    g_assert_true(deep_filter_excluded(f, "node_modules", TRUE));
    g_assert_true(deep_filter_excluded(f, "web/node_modules", TRUE));
    g_assert_true(deep_filter_excluded(f, "build", TRUE)); /* a folder by its trailing slash */
    g_assert_false(deep_filter_excluded(f, "build", FALSE));
    g_assert_true(deep_filter_excluded(f, "src/a.o", FALSE));
    g_assert_false(deep_filter_excluded(f, "src/a.c", FALSE));
    deep_filter_free(f);
    deep_query_clear(&q);
}

static void test_check(void) {
    DeepQuery q;
    deep_query_init(&q);
    g_assert_null(deep_query_check(&q));
    q.min_size = 10;
    q.max_size = 5;
    gchar* why = deep_query_check(&q);
    g_assert_nonnull(why);
    g_assert_null(deep_filter_new(&q));
    g_free(why);
    q.min_size = q.max_size = -1;
    q.after = 20;
    q.before = 10;
    why = deep_query_check(&q);
    g_assert_nonnull(why);
    g_free(why);
    q.after = q.before = 0;
    q.content = g_strdup("(");
    g_assert_null(deep_query_check(&q)); /* literal */
    q.content_opts.regex = TRUE;
    why = deep_query_check(&q);
    g_assert_nonnull(why);
    g_free(why);
    deep_query_clear(&q);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/deep_query/size", test_size);
    g_test_add_func("/deep_query/date", test_date);
    g_test_add_func("/deep_query/name", test_name);
    g_test_add_func("/deep_query/extensions", test_extensions);
    g_test_add_func("/deep_query/family", test_family);
    g_test_add_func("/deep_query/kind_size_date", test_kind_size_date);
    g_test_add_func("/deep_query/exclude", test_exclude);
    g_test_add_func("/deep_query/check", test_check);
    return g_test_run();
}
