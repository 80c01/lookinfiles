/* Unit tests for src/ole_props.c and src/office97.c (and the Office
 * 97-2003 readers): compound files built in memory (tests/ole_builder.h)
 * and the LibreOffice fixtures of tests/fixtures/preview. */
#include "ole_builder.h"
#include "office97.h"
#include "ole_props.h"
#include "preview_formats.h"

#include <glib/gstdio.h>
#include <string.h>

/* --- Property sets --- */

static void put_u32(GByteArray* a, guint32 v) {
    guint8 b[4] = {(guint8)v, (guint8)(v >> 8), (guint8)(v >> 16), (guint8)(v >> 24)};
    g_byte_array_append(a, b, 4);
}

typedef struct {
    guint32 id;
    guint32 type;
    const gchar* text;  /* VT_LPSTR */
    guint32 number;     /* VT_I4 / VT_I2 */
    guint64 filetime;   /* VT_FILETIME */
} Prop;

/* A SummaryInformation (doc_summary FALSE) property set stream */
static GBytes* property_set(gboolean doc_summary, const Prop* props, guint n) {
    static const guint8 SUMMARY[16] = {0xE0, 0x85, 0x9F, 0xF2, 0xF9, 0x4F, 0x68, 0x10,
                                       0xAB, 0x91, 0x08, 0x00, 0x2B, 0x27, 0xB3, 0xD9};
    static const guint8 DOC[16] = {0x02, 0xD5, 0xCD, 0xD5, 0x9C, 0x2E, 0x1B, 0x10,
                                   0x93, 0x97, 0x08, 0x00, 0x2B, 0x2C, 0xF9, 0xAE};
    GByteArray* values = g_byte_array_new();
    GArray* offsets = g_array_new(FALSE, FALSE, sizeof(guint32));
    guint32 table = 8 + n * 8;
    for (guint i = 0; i < n; i++) {
        guint32 off = table + values->len;
        g_array_append_val(offsets, off);
        put_u32(values, props[i].type);
        if (props[i].type == 30) {
            guint32 len = (guint32)strlen(props[i].text) + 1;
            put_u32(values, len);
            g_byte_array_append(values, (const guint8*)props[i].text, len);
            while (values->len % 4 != 0) g_byte_array_append(values, (const guint8*)"", 1);
        } else if (props[i].type == 64) {
            put_u32(values, (guint32)props[i].filetime);
            put_u32(values, (guint32)(props[i].filetime >> 32));
        } else {
            put_u32(values, props[i].number);
        }
    }
    GByteArray* out = g_byte_array_new();
    guint8 head[28] = {0xFE, 0xFF, 0, 0, 5, 1, 2, 0};
    g_byte_array_append(out, head, 24);
    put_u32(out, 1);
    g_byte_array_append(out, doc_summary ? DOC : SUMMARY, 16);
    put_u32(out, 48);
    put_u32(out, table + values->len);
    put_u32(out, n);
    for (guint i = 0; i < n; i++) {
        put_u32(out, props[i].id);
        put_u32(out, g_array_index(offsets, guint32, i));
    }
    g_byte_array_append(out, values->data, values->len);
    g_byte_array_free(values, TRUE);
    g_array_free(offsets, TRUE);
    return g_byte_array_free_to_bytes(out);
}

/* 2026-10-30 12:00:00 UTC as a FILETIME */
#define FT_2026 ((guint64)(1793361600LL + 11644473600LL) * 10000000ULL)

static GBytes* summary_stream(void) {
    Prop p[] = {
        {1, 2, NULL, 1252, 0},
        {2, 30, "Caf\xe9 budget", 0, 0}, /* Windows-1252 é */
        {4, 30, "Ann Smith", 0, 0},
        {8, 30, "Bob", 0, 0},
        {13, 64, NULL, 0, FT_2026},
        {14, 3, NULL, 3, 0},
        {15, 3, NULL, 512, 0},
        {18, 30, "Microsoft Office Word", 0, 0},
    };
    return property_set(FALSE, p, G_N_ELEMENTS(p));
}

static void test_properties(void) {
    GBytes* b = summary_stream();
    gsize len = 0;
    const guint8* data = g_bytes_get_data(b, &len);
    OleProperties* props = ole_properties_new();
    g_assert_true(ole_properties_parse(data, len, props));
    g_assert_cmpstr(props->title, ==, "Café budget");
    g_assert_cmpstr(props->author, ==, "Ann Smith");
    g_assert_cmpstr(props->last_author, ==, "Bob");
    g_assert_cmpstr(props->app_name, ==, "Microsoft Office Word");
    g_assert_cmpint(props->modified, ==, 1793361600);
    g_assert_cmpint(props->pages, ==, 3);
    g_assert_cmpint(props->words, ==, 512);
    g_assert_cmpint(props->created, ==, -1);
    gchar* head = ole_properties_html_head(props, "Word 97-2003 document");
    g_assert_true(g_str_has_prefix(head, "<head><title>Café budget</title><meta name=\"author\" content=\"Ann Smith\">"));
    g_assert_nonnull(strstr(head, "Word 97-2003 document · 3 pages · 512 words · modified 2026-10-30"));
    g_free(head);
    PreviewDoc* doc = preview_doc_new();
    ole_properties_card(doc, props);
    g_assert_nonnull(strstr(doc->text->str, "Café budget"));
    g_assert_nonnull(strstr(doc->text->str, "3 pages · 512 words"));
    preview_doc_free(doc);
    /* Document summary: slides, company */
    Prop d[] = {{7, 3, NULL, 12, 0}, {15, 30, "Initech", 0, 0}};
    GBytes* db = property_set(TRUE, d, 2);
    data = g_bytes_get_data(db, &len);
    g_assert_true(ole_properties_parse(data, len, props));
    g_assert_cmpint(props->slides, ==, 12);
    g_assert_cmpstr(props->company, ==, "Initech");
    g_bytes_unref(db);
    ole_properties_free(props);
    /* Garbage and truncated sets */
    OleProperties* empty = ole_properties_new();
    g_assert_false(ole_properties_parse((const guint8*)"not a property set at all, really not", 38, empty));
    data = g_bytes_get_data(b, &len);
    for (gsize cut = 0; cut < len; cut += 7) ole_properties_parse(data, cut, empty);
    ole_properties_free(empty);
    g_bytes_unref(b);
}

/* --- Previews --- */

/* Write data to a temporary file; caller unlinks and frees */
static gchar* temp_file(GBytes* data, const gchar* name) {
    gchar* dir = g_dir_make_tmp("office97_XXXXXX", NULL);
    gchar* path = g_build_filename(dir, name, NULL);
    gsize len = 0;
    const gchar* bytes = g_bytes_get_data(data, &len);
    g_assert_true(g_file_set_contents(path, bytes, (gssize)len, NULL));
    g_free(dir);
    return path;
}

static void remove_temp(gchar* path) {
    gchar* dir = g_path_get_dirname(path);
    g_unlink(path);
    g_rmdir(dir);
    g_free(dir);
    g_free(path);
}

static void test_unknown_compound_file(void) {
    OleBuilder* ob = ole_builder_new();
    GBytes* summary = summary_stream();
    ole_builder_add(ob, "\005SummaryInformation", g_bytes_get_data(summary, NULL), g_bytes_get_size(summary));
    ole_builder_add(ob, "Contents", "x", 1);
    ole_builder_add(ob, "Storage/Inner", "yz", 2);
    GBytes* file = ole_builder_finish(ob);
    gchar* path = temp_file(file, "drawing.vsd");
    GError* err = NULL;
    PreviewDoc* doc = office97_preview(path, "drawing.vsd", (gint64)g_bytes_get_size(file), 65536, &err);
    g_assert_no_error(err);
    const gchar* t = doc->text->str;
    g_assert_nonnull(strstr(t, "Compound file"));
    g_assert_nonnull(strstr(t, "Café budget"));
    g_assert_nonnull(strstr(t, "Streams"));
    g_assert_nonnull(strstr(t, "\\005SummaryInformation"));
    g_assert_nonnull(strstr(t, "  Inner"));
    preview_doc_free(doc);
    remove_temp(path);
    g_bytes_unref(file);
    g_bytes_unref(summary);
}

static void test_encrypted(void) {
    OleBuilder* ob = ole_builder_new();
    ole_builder_add(ob, "EncryptionInfo", "....", 4);
    ole_builder_add(ob, "EncryptedPackage", "....", 4);
    GBytes* file = ole_builder_finish(ob);
    gchar* path = temp_file(file, "secret.doc");
    PreviewDoc* doc = office97_preview(path, "secret.doc", 1024, 65536, NULL);
    g_assert_nonnull(strstr(doc->text->str, "Encrypted"));
    preview_doc_free(doc);
    remove_temp(path);
    g_bytes_unref(file);
}

static void test_not_compound(void) {
    GBytes* junk = g_bytes_new_static("\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1 but truncated", 23);
    gchar* path = temp_file(junk, "broken.doc");
    GError* err = NULL;
    g_assert_null(office97_preview(path, "broken.doc", 23, 65536, &err));
    g_assert_nonnull(err);
    g_error_free(err);
    remove_temp(path);
    g_bytes_unref(junk);
}

/* Preview of a fixture through the whole pipeline */
static PreviewDoc* fixture(const gchar* name) {
    gchar* path = g_build_filename("tests", "fixtures", "preview", name, NULL);
    GError* err = NULL;
    FileKind kind = FILE_KIND_TEXT;
    PreviewDoc* doc = preview_format_build_file(path, name, 65536, NULL, &kind, &err);
    g_assert_no_error(err);
    g_assert_cmpint(kind, ==, FILE_KIND_OLE);
    g_free(path);
    return doc;
}

static void test_fixture_properties(void) {
    PreviewDoc* doc = fixture("budget.xls");
    g_assert_nonnull(doc);
    preview_doc_free(doc);
}

/* --- Word 97-2003 --- */

/* A Word 97 WordDocument stream: FIB, then the text at 2048 as two pieces
 * (8-bit, then UTF-16); the piece table goes in table (the 1Table stream) */
static GBytes* word97_stream(const gchar* ansi, const gchar* utf8, guint16 flags, GByteArray* table) {
    GByteArray* w = g_byte_array_new();
    g_byte_array_set_size(w, 4096);
    memset(w->data, 0, w->len);
    guint8* f = w->data;
    ob_put16(f, 0xA5EC);
    ob_put16(f + 2, 0xC1);
    ob_put16(f + 0x0A, flags | 0x0200); /* 1Table */
    ob_put16(f + 32, 14);               /* csw */
    ob_put16(f + 62, 22);               /* cslw */
    glong n16 = 0;
    gunichar2* w16 = g_utf8_to_utf16(utf8, -1, NULL, &n16, NULL);
    guint32 n1 = (guint32)strlen(ansi);
    ob_put32(f + 64 + 12, n1 + (guint32)n16); /* ccpText */
    gsize fclcb = 64 + 22 * 4;
    ob_put16(f + fclcb, 93);
    gsize rg = fclcb + 2;
    memcpy(f + 2048, ansi, n1);
    for (glong i = 0; i < n16; i++) ob_put16(f + 3072 + i * 2, w16[i]);
    g_free(w16);
    /* Clx: one Pcdt with two pieces */
    guint8 clx[64] = {0x02};
    ob_put32(clx + 1, 4 * 3 + 8 * 2);
    guint8* plc = clx + 5;
    ob_put32(plc, 0);
    ob_put32(plc + 4, n1);
    ob_put32(plc + 8, n1 + (guint32)n16);
    ob_put32(plc + 12 + 2, (2048 * 2) | 0x40000000u);
    ob_put32(plc + 20 + 2, 3072);
    gsize clx_len = 5 + 4 * 3 + 8 * 2;
    ob_put32(f + rg + 66 * 4, table->len);
    ob_put32(f + rg + 67 * 4, (guint32)clx_len);
    g_byte_array_append(table, clx, (guint)clx_len);
    return g_byte_array_free_to_bytes(w);
}

static PreviewDoc* preview_of(GBytes* file, const gchar* name) {
    gchar* path = temp_file(file, name);
    GError* err = NULL;
    PreviewDoc* doc = office97_preview(path, name, (gint64)g_bytes_get_size(file), 65536, &err);
    g_assert_no_error(err);
    remove_temp(path);
    return doc;
}

static void test_word97_synthetic(void) {
    GByteArray* table = g_byte_array_new();
    GBytes* word = word97_stream("Caf\xe9 \x93menu\x94\r", "Fields \x13 HYPERLINK \"https://x.org/a?b&c\" \x14link\x15 and \xc3\xa9t\xc3\xa9 \xf0\x9f\x98\x80\r<b>\r", 0, table);
    OleBuilder* ob = ole_builder_new();
    ole_builder_add(ob, "WordDocument", g_bytes_get_data(word, NULL), g_bytes_get_size(word));
    ole_builder_add(ob, "1Table", table->data, table->len);
    GBytes* file = ole_builder_finish(ob);
    PreviewDoc* doc = preview_of(file, "menu.doc");
    g_assert_cmpint(doc->format, ==, PREVIEW_DOC_HTML);
    const gchar* html = doc->text->str;
    g_assert_nonnull(strstr(html, "<p>Café “menu”</p>"));
    g_assert_nonnull(strstr(html, "<a href=\"https://x.org/a?b&amp;c\">link</a> and été 😀"));
    g_assert_nonnull(strstr(html, "<p>&lt;b&gt;</p>"));
    g_assert_null(strstr(html, "HYPERLINK"));
    preview_doc_free(doc);
    g_bytes_unref(file);
    g_bytes_unref(word);
    g_byte_array_free(table, TRUE);
}

static void test_word97_encrypted(void) {
    GByteArray* table = g_byte_array_new();
    GBytes* word = word97_stream("secret\r", "", 0x0100, table);
    OleBuilder* ob = ole_builder_new();
    ole_builder_add(ob, "WordDocument", g_bytes_get_data(word, NULL), g_bytes_get_size(word));
    ole_builder_add(ob, "1Table", table->data, table->len);
    GBytes* file = ole_builder_finish(ob);
    PreviewDoc* doc = preview_of(file, "secret.doc");
    g_assert_cmpint(doc->format, ==, PREVIEW_DOC_TEXT);
    g_assert_nonnull(strstr(doc->text->str, "Encrypted"));
    g_assert_null(strstr(doc->text->str, "secret\n"));
    preview_doc_free(doc);
    g_bytes_unref(file);
    g_bytes_unref(word);
    g_byte_array_free(table, TRUE);
}

static void test_word6(void) {
    /* Word 6 / 95: nFib 101, text from fcMin to fcMac (8-bit) */
    guint8 w[1024] = {0};
    ob_put16(w, 0xA5DC);
    ob_put16(w + 2, 101);
    const gchar* text = "Old document\rSecond \x96 paragraph\r";
    ob_put32(w + 0x18, 512);
    ob_put32(w + 0x1C, 512 + (guint32)strlen(text));
    memcpy(w + 512, text, strlen(text));
    OleBuilder* ob = ole_builder_new();
    ole_builder_add(ob, "WordDocument", w, sizeof w);
    GBytes* file = ole_builder_finish(ob);
    PreviewDoc* doc = preview_of(file, "old.doc");
    g_assert_nonnull(strstr(doc->text->str, "<p>Old document</p>\n<p>Second – paragraph</p>"));
    preview_doc_free(doc);
    g_bytes_unref(file);
}

static void test_word_fixture(void) {
    PreviewDoc* doc = fixture("report.doc");
    g_assert_cmpint(doc->format, ==, PREVIEW_DOC_HTML);
    const gchar* html = doc->text->str;
    g_assert_nonnull(strstr(html, "<title>Quarterly Report</title>"));
    g_assert_nonnull(strstr(html, "<meta name=\"author\" content=\"Lookin UI test\">"));
    g_assert_nonnull(strstr(html, "<h1>Quarterly Report</h1>"));
    g_assert_nonnull(strstr(html, "<b>bold</b>, <i>italic</i>, <u>underlined</u> or <s>struck</s>, with E = mc<sup>2</sup>"));
    g_assert_nonnull(strstr(html, "<li>Word documents in the preview pane</li>\n<ul>\n<li>Headings, lists, tables and images</li>"));
    g_assert_nonnull(strstr(html, "<h2>Next steps</h2>"));
    g_assert_nonnull(strstr(html, "<a href=\"https://example.org/lookin\">Project page</a>"));
    g_assert_nonnull(strstr(html, "<tr><td>Q1</td><td>1 204</td><td>3.2 GB</td></tr>"));
    g_assert_nonnull(strstr(html, "<p>End of the report.</p>"));
    preview_doc_free(doc);
}

/* Every truncation of the WordDocument stream fails cleanly */
static void test_word_truncated(void) {
    gchar* path = g_build_filename("tests", "fixtures", "preview", "report.doc", NULL);
    OleReader* ole = ole_reader_open(path, NULL);
    GBytes* word = ole_reader_read(ole, "WordDocument", 1 << 20, NULL);
    GBytes* table = ole_reader_read(ole, "1Table", 1 << 20, NULL);
    ole_reader_free(ole);
    gsize wlen = g_bytes_get_size(word);
    for (gsize cut = 0; cut < wlen; cut += wlen / 23 + 1) {
        OleBuilder* ob = ole_builder_new();
        ole_builder_add(ob, "WordDocument", g_bytes_get_data(word, NULL), cut);
        ole_builder_add(ob, "1Table", g_bytes_get_data(table, NULL), g_bytes_get_size(table) / 2);
        GBytes* file = ole_builder_finish(ob);
        PreviewDoc* doc = preview_of(file, "cut.doc");
        g_assert_nonnull(doc);
        preview_doc_free(doc);
        g_bytes_unref(file);
    }
    g_bytes_unref(word);
    g_bytes_unref(table);
    g_free(path);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/office97/properties", test_properties);
    g_test_add_func("/office97/unknown_compound_file", test_unknown_compound_file);
    g_test_add_func("/office97/encrypted", test_encrypted);
    g_test_add_func("/office97/not_compound", test_not_compound);
    g_test_add_func("/office97/fixture_properties", test_fixture_properties);
    g_test_add_func("/office97/word97_synthetic", test_word97_synthetic);
    g_test_add_func("/office97/word97_encrypted", test_word97_encrypted);
    g_test_add_func("/office97/word6", test_word6);
    g_test_add_func("/office97/word_fixture", test_word_fixture);
    g_test_add_func("/office97/word_truncated", test_word_truncated);
    return g_test_run();
}
