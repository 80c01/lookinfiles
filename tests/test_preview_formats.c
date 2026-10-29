/* Unit tests for src/preview_formats.c (registry, ELF, documents, hex dump). */
#include "preview_formats.h"

#include <glib/gstdio.h>
#include <string.h>

static void test_registry(void) {
    g_assert_true(preview_format_has_builder(FILE_KIND_EXECUTABLE));
    g_assert_true(preview_format_has_builder(FILE_KIND_DOCUMENT));
    g_assert_false(preview_format_has_builder(FILE_KIND_TEXT));
    g_assert_false(preview_format_has_builder(FILE_KIND_IMAGE));
    PreviewInput in = { .path = "/nonexistent", .name = "x", .kind = FILE_KIND_TEXT };
    GError* error = NULL;
    g_assert_null(preview_format_build(&in, NULL, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
    g_error_free(error);
}

static void test_hex_dump(void) {
    guchar data[20];
    for (guint i = 0; i < sizeof(data); i++) {
        data[i] = (guchar)('A' + i);
    }
    g_autoptr(PreviewDoc) doc = preview_format_hex_dump("blob", FILE_KIND_BINARY, 100, data,
                                                        sizeof(data), NULL);
    g_assert_true(doc->monospace);
    g_assert_true(g_str_has_prefix(doc->text->str,
                                   "[ Binary file ] blob (100 bytes)\nShowing the first 20 bytes.\n\n"
                                   "00000000  41 42 43"));
    g_autoptr(PreviewDoc) doc2 = preview_format_hex_dump("a.zip", FILE_KIND_ARCHIVE, 20, data,
                                                         sizeof(data), "damaged");
    g_assert_true(g_str_has_prefix(doc2->text->str,
                                   "[ Archive file ] a.zip (20 bytes)\nCannot show the content: damaged.\n\n"));
    g_autoptr(PreviewDoc) empty = preview_format_hex_dump("e", FILE_KIND_BINARY, 0, NULL, 0, NULL);
    g_assert_cmpstr(empty->text->str, ==, "[ Binary file ] e (0 bytes)\n\n");
}

static void test_elf(void) {
    if (!g_file_test("/bin/true", G_FILE_TEST_EXISTS)) {
        g_test_skip("no /bin/true");
        return;
    }
    FileKind kind = FILE_KIND_TEXT;
    GError* error = NULL;
    g_autoptr(PreviewDoc) doc = preview_format_build_file("/bin/true", "true", 0, NULL, &kind, &error);
    g_assert_no_error(error);
    g_assert_cmpint(kind, ==, FILE_KIND_EXECUTABLE);
    g_assert_true(g_str_has_prefix(doc->text->str, "true\n"));
    g_assert_nonnull(strstr(doc->text->str, "Architecture"));
}

static void test_word_document(void) {
    const gchar* path = "tests/ui/fixtures/report.docx";
    FileKind kind = FILE_KIND_TEXT;
    GError* error = NULL;
    g_autoptr(PreviewDoc) doc = preview_format_build_file(path, "report.docx", 0, NULL, &kind, &error);
    g_assert_no_error(error);
    g_assert_cmpint(kind, ==, FILE_KIND_DOCUMENT);
    g_assert_cmpint(doc->format, ==, PREVIEW_DOC_HTML);
    g_assert_nonnull(strstr(doc->text->str, "<h1"));
}

/* Built preview of a fixture of tests/fixtures/preview */
static PreviewDoc* build_fixture(const gchar* name, FileKind expected) {
    gchar* path = g_build_filename("tests", "fixtures", "preview", name, NULL);
    FileKind kind = FILE_KIND_TEXT;
    GError* error = NULL;
    PreviewDoc* doc = preview_format_build_file(path, name, 512 * 1024, NULL, &kind, &error);
    g_assert_no_error(error);
    g_assert_cmpint(kind, ==, expected);
    g_assert_nonnull(doc);
    g_free(path);
    return doc;
}

static void test_csv(void) {
    g_autoptr(PreviewDoc) doc = build_fixture("sample.csv", FILE_KIND_CSV);
    g_assert_true(g_str_has_prefix(doc->text->str, "8 rows × 7 columns · comma-separated\n"));
    g_assert_nonnull(strstr(doc->text->str, "id │ product"));
    g_assert_nonnull(strstr(doc->text->str, "Model \"Pro\""));
    g_assert_true(doc->monospace);
}

static void test_json(void) {
    g_autoptr(PreviewDoc) doc = build_fixture("sample.json", FILE_KIND_JSON);
    g_assert_true(g_str_has_prefix(doc->text->str, "Object · 10 keys\n\n{\n  \"name\": \"lookin-demo\",\n"));
    g_autoptr(PreviewDoc) lines = build_fixture("events.jsonl", FILE_KIND_JSON);
    g_assert_true(g_str_has_prefix(lines->text->str, "JSON Lines · 3 records\n"));
}

static void test_diff(void) {
    g_autoptr(PreviewDoc) doc = build_fixture("fix-parser.patch", FILE_KIND_DIFF);
    g_assert_nonnull(strstr(doc->text->str, "2 files changed, +4 −2"));
}

static void test_archives(void) {
    g_autoptr(PreviewDoc) zip = build_fixture("sample.zip", FILE_KIND_ARCHIVE);
    g_assert_true(g_str_has_prefix(zip->text->str, "sample.zip\nZIP archive · "));
    g_assert_nonnull(strstr(zip->text->str, "4 files, 3 folders"));
    g_assert_nonnull(strstr(zip->text->str, "demo/src/main.c"));
    g_assert_nonnull(strstr(zip->text->str, "2026-10-29 10:30"));
    g_autoptr(PreviewDoc) tgz = build_fixture("sample.tar.gz", FILE_KIND_ARCHIVE);
    g_assert_nonnull(strstr(tgz->text->str, "Gzip-compressed tar archive"));
    g_assert_nonnull(strstr(tgz->text->str, "demo/latest → README.md"));
    g_autoptr(PreviewDoc) gz = build_fixture("server.log.gz", FILE_KIND_ARCHIVE);
    g_assert_nonnull(strstr(gz->text->str, "Original name  server.log"));
    g_assert_nonnull(strstr(gz->text->str, "INFO request 49 served"));
}

static void test_certificates(void) {
    g_autoptr(PreviewDoc) pem = build_fixture("server.crt", FILE_KIND_CERTIFICATE);
    g_assert_true(g_str_has_prefix(pem->text->str, "server.crt\nPEM · 782 bytes\n"));
    g_autoptr(PreviewDoc) der = build_fixture("server.der", FILE_KIND_CERTIFICATE);
    g_assert_true(g_str_has_prefix(der->text->str, "server.der\nDER · 537 bytes\n"));
    g_assert_nonnull(strstr(der->text->str, "EC P-256"));
}

static void test_cards(void) {
    g_autoptr(PreviewDoc) ics = build_fixture("meetings.ics", FILE_KIND_CALENDAR);
    g_assert_nonnull(strstr(ics->text->str, "Weekly sync"));
    g_autoptr(PreviewDoc) vcf = build_fixture("contacts.vcf", FILE_KIND_CONTACT);
    g_assert_nonnull(strstr(vcf->text->str, "Ada Lovelace"));
    g_autoptr(PreviewDoc) desktop = build_fixture("editor.desktop", FILE_KIND_DESKTOP);
    g_assert_nonnull(strstr(desktop->text->str, "lookin %F"));
}

static void test_log(void) {
    g_autoptr(PreviewDoc) doc = build_fixture("app.log", FILE_KIND_LOG);
    g_assert_true(g_str_has_prefix(doc->text->str, "9 lines · 2 errors · 1 warning\n\n2026-10-29"));
    /* A log larger than what the viewer reads: its end is shown */
    gchar* dir = g_dir_make_tmp("pf-XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "big.log", NULL);
    GString* text = g_string_new(NULL);
    for (guint i = 1; i <= 20000; i++) {
        g_string_append_printf(text, "2026-10-29 10:00:00 INFO line %u\n", i);
    }
    g_assert_true(g_file_set_contents(path, text->str, (gssize)text->len, NULL));
    g_autoptr(PreviewDoc) big = preview_format_build_file(path, "big.log", 64 * 1024, NULL, NULL, NULL);
    g_assert_nonnull(big);
    g_assert_true(g_str_has_prefix(big->text->str, "last 5,000 lines\nShowing the end of the file"));
    g_assert_true(g_str_has_suffix(big->text->str, "INFO line 20000\n"));
    g_string_free(text, TRUE);
    g_remove(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

static void test_manpage(void) {
    g_autoptr(PreviewDoc) doc = build_fixture("lookin.1", FILE_KIND_MANPAGE);
    g_assert_true(g_str_has_prefix(doc->text->str, "LOOKIN(1)"));
}

static void test_notebook(void) {
    g_autoptr(PreviewDoc) doc = build_fixture("analysis.ipynb", FILE_KIND_NOTEBOOK);
    g_assert_cmpint(doc->format, ==, PREVIEW_DOC_MARKDOWN);
    g_assert_true(g_str_has_prefix(doc->text->str, "*Jupyter notebook · 5 cells (4 code) · Python 3*"));
}

static void test_spreadsheets(void) {
    g_autoptr(PreviewDoc) xlsx = build_fixture("budget.xlsx", FILE_KIND_SPREADSHEET);
    g_assert_true(g_str_has_prefix(xlsx->text->str, "budget.xlsx\nExcel workbook · 2 sheets · "));
    g_assert_nonnull(strstr(xlsx->text->str, "Budget 2026"));
    g_assert_nonnull(strstr(xlsx->text->str, "$12,500.00"));
    g_autoptr(PreviewDoc) ods = build_fixture("stock.ods", FILE_KIND_SPREADSHEET);
    g_assert_nonnull(strstr(ods->text->str, "Bolt M6"));
    g_assert_nonnull(strstr(ods->text->str, "(empty)"));
}

static void test_damaged_document(void) {
    gchar* dir = g_dir_make_tmp("pf-XXXXXX", NULL);
    gchar* path = g_build_filename(dir, "broken.docx", NULL);
    g_assert_true(g_file_set_contents(path, "PK\x03\x04garbage", 11, NULL));
    GError* error = NULL;
    PreviewDoc* doc = preview_format_build_file(path, "broken.docx", 0, NULL, NULL, &error);
    g_assert_null(doc);
    g_assert_nonnull(error);
    g_error_free(error);
    g_remove(path);
    g_rmdir(dir);
    g_free(path);
    g_free(dir);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/preview_formats/registry", test_registry);
    g_test_add_func("/preview_formats/hex_dump", test_hex_dump);
    g_test_add_func("/preview_formats/elf", test_elf);
    g_test_add_func("/preview_formats/word_document", test_word_document);
    g_test_add_func("/preview_formats/damaged_document", test_damaged_document);
    g_test_add_func("/preview_formats/csv", test_csv);
    g_test_add_func("/preview_formats/json", test_json);
    g_test_add_func("/preview_formats/diff", test_diff);
    g_test_add_func("/preview_formats/archives", test_archives);
    g_test_add_func("/preview_formats/certificates", test_certificates);
    g_test_add_func("/preview_formats/cards", test_cards);
    g_test_add_func("/preview_formats/log", test_log);
    g_test_add_func("/preview_formats/manpage", test_manpage);
    g_test_add_func("/preview_formats/notebook", test_notebook);
    g_test_add_func("/preview_formats/spreadsheets", test_spreadsheets);
    return g_test_run();
}
