/* Unit tests for src/formats/card_view.c (iCalendar, vCard, desktop entries). */
#include "formats/card_view.h"

#include <string.h>

static gchar* fixture(const gchar* name, gsize* len) {
    gchar* path = g_build_filename("tests", "fixtures", "preview", name, NULL);
    gchar* data = NULL;
    g_assert_true(g_file_get_contents(path, &data, len, NULL));
    g_free(path);
    return data;
}

static void test_content_lines(void) {
    const gchar* text = "BEGIN:VCARD\r\nitem1.EMAIL;TYPE=\"work,pref\":a@b\r\nNOTE:one\\, two\\n thr\r\n ee\r\nX:\r\n";
    GArray* lines = vformat_parse(text, strlen(text));
    g_assert_cmpuint(lines->len, ==, 4);
    VLine* email = &g_array_index(lines, VLine, 1);
    g_assert_cmpstr(email->name, ==, "EMAIL");
    gchar* type = vformat_param(email->params, "TYPE");
    g_assert_cmpstr(type, ==, "work,pref");
    g_free(type);
    VLine* note = &g_array_index(lines, VLine, 2);
    gchar* value = vformat_unescape(note->value);
    g_assert_cmpstr(value, ==, "one, two\n three");
    g_free(value);
    g_assert_null(vformat_param("CHARSET=UTF-8", "TYPE"));
    gchar* bare = vformat_param("CELL;WORK", "TYPE");
    g_assert_cmpstr(bare, ==, "CELL,WORK");
    g_free(bare);
    g_array_free(lines, TRUE);
}

static void test_times(void) {
    gboolean all_day = FALSE;
    g_assert_cmpint(vformat_parse_time("20261029T100000Z", &all_day), ==, 1793268000);
    g_assert_false(all_day);
    g_assert_cmpint(vformat_parse_time("20261029", &all_day), ==, 1793232000);
    g_assert_true(all_day);
    g_assert_cmpint(vformat_parse_time("bad", NULL), ==, -1);
    g_assert_cmpint(vformat_parse_time("20261029X", NULL), ==, -1);
}

static void test_calendar(void) {
    gsize len;
    g_autofree gchar* text = fixture("meetings.ics", &len);
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_true(card_view_append_calendar(doc, text, len));
    const gchar* out = doc->text->str;
    g_assert_true(g_str_has_prefix(out, "Team calendar\n2 events · 1 to-do\n"));
    /* Sorted by start: the all-day event first, the to-do last */
    const gchar* release = strstr(out, "Release day");
    const gchar* sync = strstr(out, "Weekly sync");
    const gchar* todo = strstr(out, "Write the release notes for the next version of the application");
    g_assert_true(release != NULL && sync != NULL && todo != NULL);
    g_assert_true(release < sync && sync < todo);
    g_assert_nonnull(strstr(out, "2026-11-02 Mon (all day)"));
    g_assert_nonnull(strstr(out, "2026-11-05 Thu 09:00 – 09:30"));
    g_assert_nonnull(strstr(out, "Room 4, second floor"));
    g_assert_nonnull(strstr(out, "every week on TH, 10 times"));
    g_assert_nonnull(strstr(out, "Ada Lovelace <ada@example.org>"));
    g_assert_nonnull(strstr(out, "Agenda:\n"));
    g_autoptr(PreviewDoc) empty = preview_doc_new();
    g_assert_false(card_view_append_calendar(empty, "BEGIN:VCALENDAR\nEND:VCALENDAR\n", 30));
}

static void test_contacts(void) {
    gsize len;
    g_autofree gchar* text = fixture("contacts.vcf", &len);
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_true(card_view_append_contacts(doc, text, len));
    const gchar* out = doc->text->str;
    g_assert_cmpuint(doc->images->len, ==, 1);
    g_assert_nonnull(strstr(out, "Ada Lovelace\n"));
    g_assert_nonnull(strstr(out, "Analytical Engines, Research"));
    g_assert_nonnull(strstr(out, "ada@example.org (work)\n"));
    g_assert_nonnull(strstr(out, "+44 20 7946 0000 (cell)"));
    g_assert_nonnull(strstr(out, "12 St James's Square, London, SW1Y 4JH, United Kingdom"));
    /* vCard 2.1 without FN: the name comes from N, bare TEL types */
    g_assert_nonnull(strstr(out, "Torvalds Linus\n"));
    g_assert_nonnull(strstr(out, "+358 40 000 0000 (cell)"));
}

static void test_desktop(void) {
    gsize len;
    g_autofree gchar* text = fixture("editor.desktop", &len);
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    g_assert_true(card_view_append_desktop(doc, text, len));
    const gchar* out = doc->text->str;
    g_assert_true(g_str_has_prefix(out, "Lookin\nFile Explorer — Look in your files\n"));
    g_assert_nonnull(strstr(out, "Command        lookin %F"));
    g_assert_nonnull(strstr(out, "Categories     Utility, FileManager"));
    g_assert_nonnull(strstr(out, "New Window     lookin --new-window"));
    g_autoptr(PreviewDoc) bad = preview_doc_new();
    g_assert_false(card_view_append_desktop(bad, "[Other]\nx=1\n", 12));
}

int main(int argc, char** argv) {
    g_setenv("TZ", "UTC", TRUE);
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/card_view/content_lines", test_content_lines);
    g_test_add_func("/card_view/times", test_times);
    g_test_add_func("/card_view/calendar", test_calendar);
    g_test_add_func("/card_view/contacts", test_contacts);
    g_test_add_func("/card_view/desktop", test_desktop);
    return g_test_run();
}
