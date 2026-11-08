/* Unit tests for src/formats/log_view.c (timestamps, levels, tail of logs). */
#include "formats/log_view.h"

#include <string.h>

#define TS(s) log_view_timestamp_length((s), strlen(s))

static void test_timestamps(void) {
    g_assert_cmpuint(TS("2026-10-29 10:00:00.120 INFO x"), ==, 23);
    g_assert_cmpuint(TS("2026-10-29T10:00:02Z ERROR"), ==, 20);
    g_assert_cmpuint(TS("2026-10-29T10:00:02+02:00 x"), ==, 25);
    g_assert_cmpuint(TS("2026/10/29 10:00 x"), ==, 16);
    g_assert_cmpuint(TS("Oct 29 10:00:03 host"), ==, 15);
    g_assert_cmpuint(TS("Oct  9 10:00:03 host"), ==, 15);
    g_assert_cmpuint(TS("[10:00:04.5] notice"), ==, 12);
    g_assert_cmpuint(TS("[2026-10-29 10:00:00] x"), ==, 21);
    g_assert_cmpuint(TS("1761732005 fatal"), ==, 10);
    g_assert_cmpuint(TS("1761732005.25 x"), ==, 13);
    g_assert_cmpuint(TS("hello 2026-10-29"), ==, 0);
    g_assert_cmpuint(TS("12345678901 x"), ==, 0);
    g_assert_cmpuint(TS("[unclosed 10:00"), ==, 0);
}

static LogLevel level(const gchar* line, gsize* start) {
    gsize len = 0;
    return log_view_line_level(line, strlen(line), start, &len);
}

static void test_levels(void) {
    gsize at = 0;
    g_assert_cmpint(level("2026-10-29 10:00:00 ERROR boom", &at), ==, LOG_LEVEL_ERROR);
    g_assert_cmpuint(at, ==, 20);
    g_assert_cmpint(level("[warn] disk", &at), ==, LOG_LEVEL_WARNING);
    g_assert_cmpint(level("x notice: y", &at), ==, LOG_LEVEL_INFO);
    g_assert_cmpint(level("I: debug: z", &at), ==, LOG_LEVEL_DEBUG);
    /* Ordinary words are not levels */
    g_assert_cmpint(level("an error happened", &at), ==, LOG_LEVEL_NONE);
    g_assert_cmpint(level("information", &at), ==, LOG_LEVEL_NONE);
    g_assert_cmpint(level("ERRORS=0", &at), ==, LOG_LEVEL_NONE);
}

static void test_append(void) {
    gchar* path = g_build_filename("tests", "fixtures", "preview", "app.log", NULL);
    gchar* text = NULL;
    gsize len = 0;
    g_assert_true(g_file_get_contents(path, &text, &len, NULL));
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    LogStats stats;
    log_view_append(doc, text, len, &stats);
    g_assert_cmpuint(stats.lines, ==, 9);
    g_assert_cmpuint(stats.errors, ==, 2);
    g_assert_cmpuint(stats.warnings, ==, 1);
    g_assert_cmpstr(doc->text->str, ==, text);
    g_free(text);
    g_free(path);
}

static void test_last_lines(void) {
    GString* text = g_string_new(NULL);
    for (guint i = 1; i <= LOG_VIEW_MAX_LINES + 10; i++) {
        g_string_append_printf(text, "line %u\n", i);
    }
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    LogStats stats;
    log_view_append(doc, text->str, text->len, &stats);
    g_assert_cmpuint(stats.lines, ==, LOG_VIEW_MAX_LINES);
    g_assert_true(g_str_has_prefix(doc->text->str, "line 11\n"));
    g_string_free(text, TRUE);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/log_view/timestamps", test_timestamps);
    g_test_add_func("/log_view/levels", test_levels);
    g_test_add_func("/log_view/append", test_append);
    g_test_add_func("/log_view/last_lines", test_last_lines);
    return g_test_run();
}
