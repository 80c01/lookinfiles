/* Unit tests for src/ole_props.c and src/office97.c (and the Office
 * 97-2003 readers): compound files built in memory (tests/ole_builder.h)
 * and the LibreOffice fixtures of tests/fixtures/preview. */
#include "ole_builder.h"
#include "office97.h"
#include "ppt_reader.h"
#include "ole_props.h"
#include "preview/preview_formats.h"

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
    gchar* subtitle = ole_properties_subtitle(props, "Word 97-2003 document");
    g_assert_cmpstr(subtitle, ==, "Word 97-2003 document · Ann Smith · 3 pages · 512 words · modified 2026-10-30");
    g_free(subtitle);
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

/* First span of a style covering the text "what" (NULL if none) */
static const PreviewSpan* span_on(const PreviewDoc* doc, PreviewStyle style, const gchar* what) {
    const gchar* at = strstr(doc->text->str, what);
    g_assert_nonnull(at);
    gint start = (gint)g_utf8_pointer_to_offset(doc->text->str, at);
    gint end = start + (gint)g_utf8_strlen(what, -1);
    for (guint i = 0; i < doc->spans->len; i++) {
        const PreviewSpan* sp = &g_array_index(doc->spans, PreviewSpan, i);
        if (sp->style == style && sp->start <= start && sp->end >= end) {
            return sp;
        }
    }
    return NULL;
}

static void test_word97_synthetic(void) {
    GByteArray* table = g_byte_array_new();
    GBytes* word = word97_stream("Caf\xe9 \x93menu\x94\r", "Fields \x13 HYPERLINK \"https://x.org/a?b&c\" \x14link\x15 and \xc3\xa9t\xc3\xa9 \xf0\x9f\x98\x80\r<b>\r", 0, table);
    OleBuilder* ob = ole_builder_new();
    ole_builder_add(ob, "WordDocument", g_bytes_get_data(word, NULL), g_bytes_get_size(word));
    ole_builder_add(ob, "1Table", table->data, table->len);
    GBytes* file = ole_builder_finish(ob);
    PreviewDoc* doc = preview_of(file, "menu.doc");
    g_assert_cmpint(doc->format, ==, PREVIEW_DOC_TEXT);
    const gchar* t = doc->text->str;
    g_assert_true(g_str_has_prefix(t, "menu.doc\nWord 97-2003 document · "));
    g_assert_nonnull(strstr(t, "\n\nCafé “menu”\nFields link and été 😀\n<b>\n"));
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_LINK, "link"));
    g_assert_null(span_on(doc, PREVIEW_STYLE_LINK, " and"));
    g_assert_null(strstr(t, "HYPERLINK"));
    preview_doc_free(doc);
    g_bytes_unref(file);
    g_bytes_unref(word);
    g_byte_array_free(table, TRUE);
}

/* Word 97 file with formatting: a style sheet (Normal: 12 pt in the second
 * font; Heading 1: bold 16 pt, centred), a font table, character runs
 * (colour, highlight, capitals) and paragraph runs (direct alignment and
 * indent) in FKPs, and a header and footer story */
static GBytes* word97_formatted(void) {
    const gchar* text = "Title\rRed marked loud\r";        /* main text */
    const gchar* stories = "Header line\rFooter line\r\r";  /* header document */
    guint32 n_text = (guint32)strlen(text), n_hdd = (guint32)strlen(stories);
    GByteArray* wa = g_byte_array_new();
    g_byte_array_set_size(wa, 4096);
    memset(wa->data, 0, wa->len);
    guint8* f = wa->data;
    ob_put16(f, 0xA5EC);
    ob_put16(f + 2, 0xC1);
    ob_put16(f + 0x0A, 0x0200);
    ob_put16(f + 32, 14);
    ob_put16(f + 62, 22);
    ob_put32(f + 64 + 12, n_text); /* ccpText */
    ob_put32(f + 64 + 20, n_hdd);  /* ccpHdd */
    gsize rg = 64 + 22 * 4 + 2;
    ob_put16(f + rg - 2, 93);
    memcpy(f + 2048, text, n_text);
    memcpy(f + 2048 + n_text, stories, n_hdd);
    guint32 fc0 = 2048;

    /* CHPX FKP at page 6: "Title\r" plain, "Red" red, " marked" yellow, " loud\r" capitals */
    guint8* page = f + 6 * 512;
    guint32 cuts[] = { fc0, fc0 + 6, fc0 + 9, fc0 + 16, fc0 + n_text, fc0 + n_text + n_hdd };
    page[511] = 5;
    for (gint i = 0; i < 6; i++) ob_put32(page + 4 * i, cuts[i]);
    guint8 red[] = { 7, 0x70, 0x68, 0xFF, 0x00, 0x00, 0x00, 0x00 };    /* sprmCCv */
    guint8 yellow[] = { 3, 0x0C, 0x2A, 7 };                             /* sprmCHighlight */
    guint8 caps[] = { 3, 0x3B, 0x08, 1 };                               /* sprmCFCaps */
    memcpy(page + 200, red, sizeof red);
    memcpy(page + 220, yellow, sizeof yellow);
    memcpy(page + 240, caps, sizeof caps);
    guint8* rgb = page + 4 * 6;
    rgb[0] = 0;
    rgb[1] = 100;
    rgb[2] = 110;
    rgb[3] = 120;
    rgb[4] = 0;

    /* PAPX FKP at page 7: "Title\r" style 1, the rest style 0 right-aligned, indented */
    page = f + 7 * 512;
    page[511] = 2;
    ob_put32(page, fc0);
    ob_put32(page + 4, fc0 + 6);
    ob_put32(page + 8, fc0 + n_text + n_hdd);
    guint8* bx = page + 12;
    bx[0] = 150;
    bx[13] = 170;
    guint8 papx1[] = { 2, 0x01, 0x00 };                                         /* istd 1 */
    guint8 papx2[] = { 5, 0x00, 0x00, 0x61, 0x24, 2, 0x5E, 0x84, 0xD0, 0x02 };   /* istd 0, jc right, left 720 */
    memcpy(page + 300, papx1, sizeof papx1);
    memcpy(page + 340, papx2, sizeof papx2);

    GByteArray* t = g_byte_array_new();
    guint8 zero[4] = { 0 };
    g_byte_array_append(t, zero, 4);
    /* Clx: one 8-bit piece over the main text and the stories */
    guint8 clx[5 + 8 + 8] = { 0x02 };
    ob_put32(clx + 1, 16);
    ob_put32(clx + 5, 0);
    ob_put32(clx + 9, n_text + n_hdd);
    ob_put32(clx + 13 + 2, (fc0 * 2) | 0x40000000u);
    ob_put32(f + rg + 66 * 4, t->len);
    ob_put32(f + rg + 67 * 4, sizeof clx);
    g_byte_array_append(t, clx, sizeof clx);
    /* Bin tables: one FKP each */
    guint8 bte[12];
    ob_put32(bte, fc0);
    ob_put32(bte + 4, fc0 + n_text + n_hdd);
    ob_put32(bte + 8, 6);
    ob_put32(f + rg + 24 * 4, t->len);
    ob_put32(f + rg + 25 * 4, 12);
    g_byte_array_append(t, bte, 12);
    ob_put32(bte + 8, 7);
    ob_put32(f + rg + 26 * 4, t->len);
    ob_put32(f + rg + 27 * 4, 12);
    g_byte_array_append(t, bte, 12);
    /* Style sheet */
    GByteArray* st = g_byte_array_new();
    guint8 stshi[2 + 18] = { 18, 0, 2, 0, 10, 0 };
    ob_put16(stshi + 2 + 12, 0); /* ftcAsci: the first font */
    g_byte_array_append(st, stshi, sizeof stshi);
    guint8 normal[] = { 0, 0, 0x01, 0xFF, 0x02, 0x00, 0, 0, 0, 0,          /* sti 0, para, no base, 2 UPX */
                        1, 0, 'N', 0, 0, 0,                               /* name "N" */
                        2, 0, 0, 0,                                       /* UPX papx: istd 0 */
                        8, 0, 0x43, 0x4A, 24, 0, 0x4F, 0x4A, 1, 0 };      /* UPX chpx: 12 pt, font 1 */
    guint8 heading[] = { 1, 0, 0x01, 0x00, 0x02, 0x00, 0, 0, 0, 0,         /* sti 1, base Normal */
                         1, 0, 'H', 0, 0, 0,
                         5, 0, 1, 0, 0x61, 0x24, 1, 0,                    /* papx: istd 1, centred */
                         7, 0, 0x35, 0x08, 1, 0x43, 0x4A, 32, 0 };        /* chpx: bold, 16 pt */
    guint8 cb[2];
    ob_put16(cb, sizeof normal);
    g_byte_array_append(st, cb, 2);
    g_byte_array_append(st, normal, sizeof normal);
    ob_put16(cb, sizeof heading);
    g_byte_array_append(st, cb, 2);
    g_byte_array_append(st, heading, sizeof heading);
    ob_put32(f + rg + 2 * 4, t->len);
    ob_put32(f + rg + 3 * 4, st->len);
    g_byte_array_append(t, st->data, st->len);
    g_byte_array_free(st, TRUE);
    /* Font table: two FFN records */
    GByteArray* ft = g_byte_array_new();
    guint8 count[4] = { 2, 0, 0, 0 };
    g_byte_array_append(ft, count, 4);
    const gchar* names[] = { "Arial", "Liberation Serif" };
    for (gint i = 0; i < 2; i++) {
        guint8 ffn[80] = { 0 };
        gsize n = strlen(names[i]);
        for (gsize k = 0; k < n; k++) ob_put16(ffn + 40 + 2 * k, (guchar)names[i][k]);
        gsize size = 40 + 2 * (n + 1);
        ffn[0] = (guint8)(size - 1);
        g_byte_array_append(ft, ffn, (guint)size);
    }
    ob_put32(f + rg + 30 * 4, t->len);
    ob_put32(f + rg + 31 * 4, ft->len);
    g_byte_array_append(t, ft->data, ft->len);
    g_byte_array_free(ft, TRUE);
    /* Header stories: 6 separators, even / odd header, even / odd footer, first header / footer */
    guint32 hdd[13] = { 0, 0, 0, 0, 0, 0, 0, 0, 12, 12, 24, 24, 24 };
    guint8 plc[13 * 4];
    for (gint i = 0; i < 13; i++) ob_put32(plc + 4 * i, hdd[i]);
    ob_put32(f + rg + 22 * 4, t->len);
    ob_put32(f + rg + 23 * 4, sizeof plc);
    g_byte_array_append(t, plc, sizeof plc);

    OleBuilder* ob = ole_builder_new();
    ole_builder_add(ob, "WordDocument", wa->data, wa->len);
    ole_builder_add(ob, "1Table", t->data, t->len);
    g_byte_array_free(wa, TRUE);
    g_byte_array_free(t, TRUE);
    return ole_builder_finish(ob);
}

static void test_word97_formatting(void) {
    GBytes* file = word97_formatted();
    PreviewDoc* doc = preview_of(file, "styled.doc");
    const gchar* t = doc->text->str;
    g_assert_nonnull(strstr(t, "\n\nHeader line\n────────────────────────\nTitle\nRed marked LOUD\n"
                               "────────────────────────\nFooter line\n"));
    /* Style: bold, 16 pt over Normal's 12 pt, centred; Normal's font */
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_BOLD, "Title"));
    g_assert_cmpstr(span_on(doc, PREVIEW_STYLE_SCALE, "Title")->arg, ==, "1.333");
    g_assert_cmpstr(span_on(doc, PREVIEW_STYLE_FAMILY, "Title")->arg, ==, "Liberation Serif");
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_OUTLINE1, "Title")); /* Heading 1 style: the tagbar lists it */
    g_assert_null(span_on(doc, PREVIEW_STYLE_OUTLINE1, "Red"));
    PreviewParagraph layout;
    g_assert_true(preview_paragraph_parse(span_on(doc, PREVIEW_STYLE_PARAGRAPH, "Title")->arg, &layout));
    g_assert_cmpint(layout.align, ==, PREVIEW_ALIGN_CENTER);
    /* Direct formatting */
    g_assert_null(span_on(doc, PREVIEW_STYLE_BOLD, "Red"));
    g_assert_null(span_on(doc, PREVIEW_STYLE_SCALE, "Red"));
    g_assert_cmpstr(span_on(doc, PREVIEW_STYLE_COLOR, "Red")->arg, ==, "#ff0000");
    g_assert_cmpstr(span_on(doc, PREVIEW_STYLE_HIGHLIGHT, " marked")->arg, ==, "#ffff00");
    g_assert_true(preview_paragraph_parse(span_on(doc, PREVIEW_STYLE_PARAGRAPH, "Red marked")->arg, &layout));
    g_assert_cmpint(layout.align, ==, PREVIEW_ALIGN_RIGHT);
    g_assert_cmpint(layout.left, ==, 48);
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_DIM, "Header line"));
    preview_doc_free(doc);
    g_bytes_unref(file);
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
    g_assert_nonnull(strstr(doc->text->str, "\n\nOld document\nSecond – paragraph\n"));
    preview_doc_free(doc);
    g_bytes_unref(file);
}

static void test_word_fixture(void) {
    PreviewDoc* doc = fixture("report.doc");
    g_assert_cmpint(doc->format, ==, PREVIEW_DOC_TEXT);
    const gchar* t = doc->text->str;
    g_assert_true(g_str_has_prefix(t, "Quarterly Report\nWord 97-2003 document · Lookin UI test · "));
    g_assert_nonnull(strstr(t, "Text can be bold, italic, underlined or struck, with E = mc2."));
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_BOLD, "bold"));
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_ITALIC, "italic"));
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_UNDERLINE, "underlined"));
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_STRIKE, "struck"));
    g_assert_cmpstr(span_on(doc, PREVIEW_STYLE_FAMILY, "Prepared for")->arg, ==, "Calibri");
    /* Headings: the style's font and size (14 pt over 11 pt) */
    g_assert_cmpstr(span_on(doc, PREVIEW_STYLE_SCALE, "Next steps")->arg, ==, "1.273");
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_OUTLINE2, "Next steps")); /* Heading 2: in the tagbar */
    /* Nested list item indented further */
    PreviewParagraph outer, inner;
    g_assert_true(preview_paragraph_parse(span_on(doc, PREVIEW_STYLE_PARAGRAPH, "Faster previews")->arg, &outer));
    g_assert_true(preview_paragraph_parse(span_on(doc, PREVIEW_STYLE_PARAGRAPH, "Headings, lists")->arg, &inner));
    g_assert_cmpint(inner.left, >, outer.left);
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_LINK, "Project page"));
    g_assert_nonnull(strstr(t, "│ Q1      │ 1 204 │ 3.2 GB │"));
    g_assert_nonnull(strstr(t, "End of the report.\n"));
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

/* --- Excel 97-2003 --- */

static void rec(GByteArray* a, guint16 type, const void* data, gsize len) {
    guint8 h[4];
    ob_put16(h, type);
    ob_put16(h + 2, (guint)len);
    g_byte_array_append(a, h, 4);
    g_byte_array_append(a, data, (guint)len);
}

/* row, col, xf header of a cell record */
static gsize cell_head(guint8* p, guint row, guint col, guint xf) {
    ob_put16(p, row);
    ob_put16(p + 2, col);
    ob_put16(p + 4, xf);
    return 6;
}

static void put_f64(guint8* p, gdouble v) {
    guint64 bits;
    memcpy(&bits, &v, 8);
    ob_put32(p, (guint32)bits);
    ob_put32(p + 4, (guint32)(bits >> 32));
}

/* A BIFF8 workbook: globals (formats, XFs, one sheet, SST split by a
 * CONTINUE, optional FILEPASS / 1904 dates), then the sheet */
static GBytes* biff8_workbook(gboolean encrypted, gboolean date1904) {
    GByteArray* g = g_byte_array_new();
    guint8 bof[16] = {0};
    ob_put16(bof, 0x0600);
    ob_put16(bof + 2, 0x0005);
    rec(g, 0x0809, bof, 16);
    if (encrypted) rec(g, 0x002F, "\x01\x00\x01\x00", 4);
    if (date1904) rec(g, 0x0022, "\x01\x00", 2);
    /* FORMAT 164 "0.0%"; XF 0: General, XF 1: builtin 14 (date), XF 2: 164 */
    guint8 fmt[16];
    ob_put16(fmt, 164);
    ob_put16(fmt + 2, 4);
    fmt[4] = 0;
    memcpy(fmt + 5, "0.0%", 4);
    rec(g, 0x041E, fmt, 9);
    guint ifmts[3] = {0, 14, 164};
    for (gint i = 0; i < 3; i++) {
        guint8 xf[20] = {0};
        ob_put16(xf + 2, ifmts[i]);
        rec(g, 0x00E0, xf, 20);
    }
    /* BOUNDSHEET: position patched below */
    guint8 bs[16] = {0};
    bs[6] = 6;
    bs[7] = 0;
    memcpy(bs + 8, "Ventes", 6);
    gsize bs_at = g->len + 4;
    rec(g, 0x0085, bs, 14);
    /* SST: "Hello", then "Wörld" whose characters continue in a CONTINUE
     * record switching from 8-bit to UTF-16 */
    guint8 sst[64];
    gsize n = 0;
    ob_put32(sst, 3);
    ob_put32(sst + 4, 3);
    n = 8;
    ob_put16(sst + n, 5); sst[n + 2] = 0; memcpy(sst + n + 3, "Hello", 5); n += 8;
    ob_put16(sst + n, 5); sst[n + 2] = 0; memcpy(sst + n + 3, "W", 1); n += 4;
    rec(g, 0x00FC, sst, n);
    guint8 cont[32];
    gsize m = 0;
    cont[m++] = 1; /* the rest is UTF-16 */
    ob_put16(cont + m, 0x00F6); m += 2; /* ö */
    ob_put16(cont + m, 'r'); m += 2;
    ob_put16(cont + m, 'l'); m += 2;
    ob_put16(cont + m, 'd'); m += 2;
    ob_put16(cont + m, 3); cont[m + 2] = 0; memcpy(cont + m + 3, "end", 3); m += 6;
    rec(g, 0x003C, cont, m);
    rec(g, 0x000A, "", 0);
    /* The sheet */
    ob_put32(g->data + bs_at, g->len);
    guint8 sbof[16] = {0};
    ob_put16(sbof, 0x0600);
    ob_put16(sbof + 2, 0x0010);
    rec(g, 0x0809, sbof, 16);
    guint8 c[32];
    gsize k;
    k = cell_head(c, 0, 0, 0); ob_put32(c + k, 0); rec(g, 0x00FD, c, k + 4);    /* A1 LABELSST Hello */
    k = cell_head(c, 0, 1, 0); ob_put32(c + k, 1); rec(g, 0x00FD, c, k + 4);    /* B1 Wörld */
    k = cell_head(c, 0, 2, 0); ob_put32(c + k, 2); rec(g, 0x00FD, c, k + 4);    /* C1 end */
    k = cell_head(c, 1, 0, 0); ob_put32(c + k, (42u << 2) | 2); rec(g, 0x027E, c, k + 4);      /* A2 RK int 42 */
    k = cell_head(c, 1, 1, 0); ob_put32(c + k, (1234u << 2) | 3); rec(g, 0x027E, c, k + 4);    /* B2 RK 12.34 */
    k = cell_head(c, 1, 2, 2); put_f64(c + k, 0.125); rec(g, 0x0203, c, k + 8);                /* C2 12.5% */
    k = cell_head(c, 2, 0, 1); put_f64(c + k, 46000); rec(g, 0x0203, c, k + 8);                /* A3 date */
    /* MULRK D2:E2 */
    guint8 mul[32];
    ob_put16(mul, 1); ob_put16(mul + 2, 3);
    ob_put16(mul + 4, 0); ob_put32(mul + 6, (7u << 2) | 2);
    ob_put16(mul + 10, 0); ob_put32(mul + 12, (8u << 2) | 2);
    ob_put16(mul + 16, 4);
    rec(g, 0x00BD, mul, 18);
    k = cell_head(c, 2, 1, 0); c[k] = 1; c[k + 1] = 0; rec(g, 0x0205, c, k + 2);            /* B3 TRUE */
    k = cell_head(c, 2, 2, 0); c[k] = 0x07; c[k + 1] = 1; rec(g, 0x0205, c, k + 2);         /* C3 #DIV/0! */
    /* D3: a formula with a string result, then its STRING record */
    k = cell_head(c, 2, 3, 0);
    memset(c + k, 0, 8); c[k] = 0; ob_put16(c + k + 6, 0xFFFF);
    rec(g, 0x0006, c, k + 8 + 6);
    guint8 str[16];
    ob_put16(str, 4); str[2] = 0; memcpy(str + 3, "calc", 4);
    rec(g, 0x0207, str, 7);
    /* LABEL in row 500: beyond max_rows */
    k = cell_head(c, 500, 0, 0); ob_put16(c + k, 3); c[k + 2] = 0; memcpy(c + k + 3, "far", 3); rec(g, 0x0204, c, k + 6);
    rec(g, 0x000A, "", 0);
    return g_byte_array_free_to_bytes(g);
}

static PreviewDoc* workbook_preview(GBytes* stream, const gchar* stream_name) {
    OleBuilder* ob = ole_builder_new();
    ole_builder_add(ob, stream_name, g_bytes_get_data(stream, NULL), g_bytes_get_size(stream));
    GBytes* file = ole_builder_finish(ob);
    PreviewDoc* doc = preview_of(file, "book.xls");
    g_bytes_unref(file);
    return doc;
}

static void test_excel_synthetic(void) {
    GBytes* wb = biff8_workbook(FALSE, FALSE);
    PreviewDoc* doc = workbook_preview(wb, "Workbook");
    const gchar* t = doc->text->str;
    g_assert_nonnull(strstr(t, "Excel 97-2003 workbook · 1 sheet"));
    g_assert_nonnull(strstr(t, "Hello"));
    g_assert_nonnull(strstr(t, "Wörld"));
    g_assert_nonnull(strstr(t, "end"));
    g_assert_nonnull(strstr(t, "42"));
    g_assert_nonnull(strstr(t, "12.34"));
    g_assert_nonnull(strstr(t, "12.5%"));
    g_assert_nonnull(strstr(t, "2025-12-09")); /* serial 46000 */
    g_assert_nonnull(strstr(t, "TRUE"));
    g_assert_nonnull(strstr(t, "#DIV/0!"));
    g_assert_nonnull(strstr(t, "calc"));
    g_assert_null(strstr(t, "far"));
    g_assert_nonnull(strstr(t, "Showing the first 300 rows."));
    preview_doc_free(doc);
    g_bytes_unref(wb);
    /* The 1904 date system shifts dates only */
    wb = biff8_workbook(FALSE, TRUE);
    doc = workbook_preview(wb, "Workbook");
    g_assert_nonnull(strstr(doc->text->str, "2029-12-10"));
    g_assert_nonnull(strstr(doc->text->str, "12.34"));
    preview_doc_free(doc);
    g_bytes_unref(wb);
}

static void test_excel_encrypted(void) {
    GBytes* wb = biff8_workbook(TRUE, FALSE);
    PreviewDoc* doc = workbook_preview(wb, "Workbook");
    g_assert_nonnull(strstr(doc->text->str, "Encrypted"));
    g_assert_null(strstr(doc->text->str, "Hello"));
    preview_doc_free(doc);
    g_bytes_unref(wb);
}

static void test_excel5(void) {
    /* BIFF5 "Book": 8-bit strings in the code page (Windows-1252) */
    GByteArray* g = g_byte_array_new();
    guint8 bof[8] = {0};
    ob_put16(bof, 0x0500);
    ob_put16(bof + 2, 0x0005);
    rec(g, 0x0809, bof, 8);
    rec(g, 0x0042, "\xe4\x04", 2); /* code page 1252 */
    guint8 bs[16] = {0};
    bs[6] = 4;
    memcpy(bs + 7, "Feuil", 5); /* cch 4: "Feui" */
    gsize bs_at = g->len + 4;
    rec(g, 0x0085, bs, 11);
    rec(g, 0x000A, "", 0);
    ob_put32(g->data + bs_at, g->len);
    rec(g, 0x0809, bof, 8);
    guint8 c[32];
    gsize k = cell_head(c, 0, 0, 0);
    ob_put16(c + k, 6);
    memcpy(c + k + 2, "Caf\xe9 \x80", 6);
    rec(g, 0x0204, c, k + 8);
    rec(g, 0x000A, "", 0);
    GBytes* book = g_byte_array_free_to_bytes(g);
    PreviewDoc* doc = workbook_preview(book, "Book");
    g_assert_nonnull(strstr(doc->text->str, "Excel 5.0/95 workbook"));
    g_assert_nonnull(strstr(doc->text->str, "Café €"));
    preview_doc_free(doc);
    g_bytes_unref(book);
}

static void test_excel_fixture(void) {
    PreviewDoc* doc = fixture("budget.xls");
    const gchar* t = doc->text->str;
    g_assert_nonnull(strstr(t, "Excel 97-2003 workbook · 2 sheets"));
    g_assert_nonnull(strstr(t, "Household budget"));
    g_assert_nonnull(strstr(t, "Expenses · Income"));
    g_assert_nonnull(strstr(t, "2026-09-01"));
    g_assert_nonnull(strstr(t, "Groceries – café"));
    g_assert_nonnull(strstr(t, "1,250.00"));
    g_assert_nonnull(strstr(t, "62.5%"));
    g_assert_nonnull(strstr(t, "1,697.30"));
    g_assert_nonnull(strstr(t, "-3.25"));
    preview_doc_free(doc);
}

static void test_excel_truncated(void) {
    gchar* path = g_build_filename("tests", "fixtures", "preview", "budget.xls", NULL);
    OleReader* ole = ole_reader_open(path, NULL);
    GBytes* wb = ole_reader_read(ole, "Workbook", 1 << 20, NULL);
    ole_reader_free(ole);
    gsize len = g_bytes_get_size(wb);
    for (gsize cut = 0; cut < len; cut += len / 31 + 1) {
        GBytes* part = g_bytes_new_from_bytes(wb, 0, cut);
        PreviewDoc* doc = workbook_preview(part, "Workbook");
        g_assert_nonnull(doc);
        preview_doc_free(doc);
        g_bytes_unref(part);
    }
    g_bytes_unref(wb);
    g_free(path);
}

/* --- PowerPoint 97-2003 --- */

static void ppt_rec(GByteArray* a, guint16 verinst, guint16 type, const void* data, gsize len) {
    guint8 h[8];
    ob_put16(h, verinst);
    ob_put16(h + 2, type);
    ob_put32(h + 4, (guint32)len);
    g_byte_array_append(a, h, 8);
    if (len > 0) g_byte_array_append(a, data, (guint)len);
}

static void ppt_text(GByteArray* a, guint32 text_type, const gchar* utf8, gboolean bytes) {
    guint8 th[4];
    ob_put32(th, text_type);
    ppt_rec(a, 0, 0x0F9F, th, 4);
    if (bytes) {
        ppt_rec(a, 0, 0x0FA8, utf8, strlen(utf8)); /* 8-bit: already Windows-1252 */
        return;
    }
    glong n = 0;
    gunichar2* w = g_utf8_to_utf16(utf8, -1, NULL, &n, NULL);
    GByteArray* t = g_byte_array_new();
    for (glong i = 0; i < n; i++) {
        guint8 c[2];
        ob_put16(c, w[i]);
        g_byte_array_append(t, c, 2);
    }
    ppt_rec(a, 0, 0x0FA0, t->data, t->len);
    g_byte_array_free(t, TRUE);
    g_free(w);
}

/* A PowerPoint-style stream: the text in the document's SlideListWithText */
static GBytes* ppt_stream(gboolean encrypted) {
    GByteArray* list = g_byte_array_new();
    guint8 persist[20] = {0};
    ppt_rec(list, 0, 0x03F3, persist, 20);
    ppt_text(list, 0, "Budget 2027", FALSE);
    ppt_text(list, 1, "Rent\rFood \x80 drinks", TRUE);
    ppt_rec(list, 0, 0x03F3, persist, 20);
    ppt_text(list, 6, "Questions?", FALSE);
    GByteArray* doc = g_byte_array_new();
    ppt_rec(doc, 0x000F, 0x0FF0, list->data, list->len); /* instance 0: slides */
    if (encrypted) ppt_rec(doc, 0x000F, 0x2F14, "", 0);
    GByteArray* s = g_byte_array_new();
    ppt_rec(s, 0x000F, 0x03E8, doc->data, doc->len);
    g_byte_array_free(list, TRUE);
    g_byte_array_free(doc, TRUE);
    return g_byte_array_free_to_bytes(s);
}

static PreviewDoc* presentation_preview(GBytes* stream) {
    OleBuilder* ob = ole_builder_new();
    ole_builder_add(ob, "PowerPoint Document", g_bytes_get_data(stream, NULL), g_bytes_get_size(stream));
    GBytes* file = ole_builder_finish(ob);
    PreviewDoc* doc = preview_of(file, "deck.ppt");
    g_bytes_unref(file);
    return doc;
}

/* The slides of a PowerPoint Document stream */
static SlideDeck* ppt_deck(GBytes* stream) {
    OleBuilder* ob = ole_builder_new();
    ole_builder_add(ob, "PowerPoint Document", g_bytes_get_data(stream, NULL), g_bytes_get_size(stream));
    GBytes* file = ole_builder_finish(ob);
    gchar* path = temp_file(file, "deck.ppt");
    GError* err = NULL;
    OleReader* ole = ole_reader_open(path, &err);
    g_assert_no_error(err);
    SlideDeck* deck = ppt_reader_read(ole, NULL, &err);
    g_assert_no_error(err);
    ole_reader_free(ole);
    remove_temp(path);
    g_bytes_unref(file);
    return deck;
}

static const SlideParagraph* para_of(const SlideShape* s, guint i) {
    return g_ptr_array_index(s->paragraphs, i);
}

static const SlideRun* run_of(const SlideShape* s, guint para, guint run) {
    return g_ptr_array_index(para_of(s, para)->runs, run);
}

static void test_ppt_synthetic(void) {
    /* Text only in the slide list: the default title and body boxes */
    GBytes* st = ppt_stream(FALSE);
    PreviewDoc* doc = presentation_preview(st);
    const gchar* t = doc->text->str;
    g_assert_nonnull(strstr(t, "PowerPoint 97-2003 presentation · 2 slides"));
    g_assert_nonnull(strstr(t, "Slide 1 · Budget 2027\n" PREVIEW_DOC_IMAGE_CHAR));
    g_assert_nonnull(strstr(t, "Slide 2 · Questions?\n"));
    g_assert_cmpuint(doc->images->len, ==, 2);
    preview_doc_free(doc);
    SlideDeck* deck = ppt_deck(st);
    const Slide* s1 = g_ptr_array_index(deck->slides, 0);
    g_assert_cmpuint(s1->shapes->len, ==, 2);
    const SlideShape* title = g_ptr_array_index(s1->shapes, 0);
    g_assert_cmpfloat(run_of(title, 0, 0)->size, ==, 44);
    const SlideShape* body = g_ptr_array_index(s1->shapes, 1);
    g_assert_cmpuint(body->paragraphs->len, ==, 2);
    g_assert_cmpstr(para_of(body, 0)->bullet, ==, "•");
    g_assert_cmpstr(run_of(body, 1, 0)->text, ==, "Food € drinks");
    const Slide* s2 = g_ptr_array_index(deck->slides, 1);
    g_assert_cmpint(para_of(g_ptr_array_index(s2->shapes, 0), 0)->align, ==, SLIDE_ALIGN_CENTER);
    slide_deck_free(deck);
    g_bytes_unref(st);
    st = ppt_stream(TRUE);
    doc = presentation_preview(st);
    g_assert_nonnull(strstr(doc->text->str, "Encrypted"));
    g_assert_null(strstr(doc->text->str, "Budget"));
    preview_doc_free(doc);
    g_bytes_unref(st);
}

/* An OfficeArt shape: FSP, OPT (id, value pairs), anchor, then extra records */
static void art_shape(GByteArray* a, guint type, guint32 flags, const guint32* props, guint n_props,
                      guint16 anchor_type, const void* anchor, gsize anchor_len, GByteArray* extra) {
    GByteArray* sp = g_byte_array_new();
    guint8 fsp[8];
    ob_put32(fsp, 1024);
    ob_put32(fsp + 4, flags);
    ppt_rec(sp, (guint16)(0x0002 | (type << 4)), 0xF00A, fsp, 8);
    GByteArray* opt = g_byte_array_new();
    for (guint i = 0; i < n_props; i++) {
        guint8 e[6];
        ob_put16(e, props[2 * i]);
        ob_put32(e + 2, props[2 * i + 1]);
        g_byte_array_append(opt, e, 6);
    }
    ppt_rec(sp, (guint16)(0x0003 | (n_props << 4)), 0xF00B, opt->data, opt->len);
    g_byte_array_free(opt, TRUE);
    if (anchor != NULL) ppt_rec(sp, 0, anchor_type, anchor, anchor_len);
    if (extra != NULL) g_byte_array_append(sp, extra->data, extra->len);
    ppt_rec(a, 0x000F, 0xF004, sp->data, sp->len);
    g_byte_array_free(sp, TRUE);
}

/* A presentation with drawings: a filled rectangle with styled text, a
 * group with a child shape, the background, a colour scheme */
static GBytes* ppt_drawn(void) {
    GByteArray* tree = g_byte_array_new();
    guint8 fspgr[16] = { 0 };
    GByteArray* patriarch = g_byte_array_new();
    ppt_rec(patriarch, 0x0001, 0xF009, fspgr, 16);
    art_shape(tree, 0, 0x05, NULL, 0, 0, NULL, 0, patriarch);
    g_byte_array_free(patriarch, TRUE);
    /* Rectangle: fill scheme colour 5 (accent), no line; text "Hello" bold 40 pt in RGB green */
    guint32 rect_props[] = { 0x0181, 0x08000005, 0x01FF, 0x00080000, 0x0087, 1 };
    gint16 rect_anchor[4] = { 576, 576, 2880, 1728 }; /* top, left, right, bottom */
    guint8 ra[8];
    for (gint i = 0; i < 4; i++) ob_put16(ra + 2 * i, (guint16)rect_anchor[i]);
    GByteArray* box = g_byte_array_new();
    GByteArray* tb = g_byte_array_new();
    ppt_text(tb, 0, "Hello", FALSE);
    guint8 style[] = { 6, 0, 0, 0, 0, 0, 0x00, 0x08, 0x00, 0x00, 1, 0,           /* para: 6 chars, centred */
                       6, 0, 0, 0, 0x01, 0x00, 0x06, 0x00, 0x01, 0x00, 40, 0, 0x00, 0x80, 0x00, 0xFE };
    ppt_rec(tb, 0, 0x0FA1, style, sizeof style);
    ppt_rec(box, 0x000F, 0xF00D, tb->data, tb->len);
    g_byte_array_free(tb, TRUE);
    art_shape(tree, 1, 0x0A00, rect_props, 3, 0xF010, ra, 8, box);
    g_byte_array_free(box, TRUE);
    /* Group: box 1152..2304 x 2304..3456 over child space 0..100 */
    GByteArray* group = g_byte_array_new();
    GByteArray* gfirst = g_byte_array_new();
    guint8 child_space[16];
    ob_put32(child_space, 0);
    ob_put32(child_space + 4, 0);
    ob_put32(child_space + 8, 100);
    ob_put32(child_space + 12, 100);
    ppt_rec(gfirst, 0x0001, 0xF009, child_space, 16);
    guint8 ga[8];
    gint16 group_anchor[4] = { 2304, 1152, 2304, 3456 };
    for (gint i = 0; i < 4; i++) ob_put16(ga + 2 * i, (guint16)group_anchor[i]);
    ppt_rec(gfirst, 0, 0xF010, ga, 8);
    art_shape(group, 0, 0x0201, NULL, 0, 0, NULL, 0, gfirst);
    g_byte_array_free(gfirst, TRUE);
    guint32 ell_props[] = { 0x0181, 0x000000FF, 0x01C0, 0x00FF0000 };
    guint8 ca[16];
    ob_put32(ca, 50);
    ob_put32(ca + 4, 0);
    ob_put32(ca + 8, 100);
    ob_put32(ca + 12, 50);
    art_shape(group, 3, 0x0A82, ell_props, 2, 0xF00F, ca, 16, NULL);
    ppt_rec(tree, 0x000F, 0xF003, group->data, group->len);
    g_byte_array_free(group, TRUE);
    GByteArray* dg = g_byte_array_new();
    ppt_rec(dg, 0x000F, 0xF003, tree->data, tree->len);
    g_byte_array_free(tree, TRUE);
    /* Background: dark blue */
    guint32 bg_props[] = { 0x0181, 0x00402010 };
    art_shape(dg, 1, 0x0C00, bg_props, 1, 0, NULL, 0, NULL);
    GByteArray* drawing = g_byte_array_new();
    ppt_rec(drawing, 0x000F, 0xF002, dg->data, dg->len);
    g_byte_array_free(dg, TRUE);
    GByteArray* slide = g_byte_array_new();
    guint8 atom[24] = { 0 };
    ppt_rec(slide, 0x0002, 0x03EF, atom, 24);
    guint8 scheme[32] = { 0 };
    scheme[5 * 4 + 1] = 0xA0; /* accent: 00A000 */
    ppt_rec(slide, 0x0010, 0x07F0, scheme, 32);
    ppt_rec(slide, 0x000F, 0x040C, drawing->data, drawing->len);
    g_byte_array_free(drawing, TRUE);
    GByteArray* doc = g_byte_array_new();
    guint8 size[8];
    ob_put32(size, 5760);  /* 720 pt */
    ob_put32(size + 4, 3240);
    ppt_rec(doc, 0x0001, 0x03E9, size, 8);
    GByteArray* s = g_byte_array_new();
    ppt_rec(s, 0x000F, 0x03E8, doc->data, doc->len);
    ppt_rec(s, 0x000F, 0x03EE, slide->data, slide->len);
    g_byte_array_free(doc, TRUE);
    g_byte_array_free(slide, TRUE);
    return g_byte_array_free_to_bytes(s);
}

static void test_ppt_drawing(void) {
    GBytes* st = ppt_drawn();
    SlideDeck* deck = ppt_deck(st);
    g_assert_cmpfloat(deck->width, ==, 720);
    g_assert_cmpfloat(deck->height, ==, 405);
    g_assert_cmpuint(deck->slides->len, ==, 1);
    const Slide* slide = g_ptr_array_index(deck->slides, 0);
    g_assert_cmphex(slide->background, ==, 0x102040);
    g_assert_cmpstr(slide->title, ==, "Hello");
    g_assert_cmpuint(slide->shapes->len, ==, 2);
    const SlideShape* rect = g_ptr_array_index(slide->shapes, 0);
    g_assert_cmpfloat(rect->x, ==, 72);
    g_assert_cmpfloat(rect->y, ==, 72);
    g_assert_cmpfloat(rect->w, ==, 288);
    g_assert_cmpfloat(rect->h, ==, 144);
    g_assert_cmphex(rect->fill, ==, 0x00A000);
    g_assert_cmphex(rect->line, ==, SLIDE_NO_COLOR);
    g_assert_cmpint(rect->anchor, ==, SLIDE_ANCHOR_MIDDLE);
    g_assert_cmpint(para_of(rect, 0)->align, ==, SLIDE_ALIGN_CENTER);
    const SlideRun* r = run_of(rect, 0, 0);
    g_assert_cmpstr(r->text, ==, "Hello");
    g_assert_true(r->bold);
    g_assert_cmpfloat(r->size, ==, 40);
    g_assert_cmphex(r->color, ==, 0x008000);
    /* The group child, mapped from its child space; flipped */
    const SlideShape* ell = g_ptr_array_index(slide->shapes, 1);
    g_assert_cmpint(ell->geometry, ==, SLIDE_GEOM_ELLIPSE);
    g_assert_cmpfloat(ell->x, ==, 144 + 72);
    g_assert_cmpfloat(ell->y, ==, 288);
    g_assert_cmpfloat(ell->w, ==, 72);
    g_assert_true(ell->flip_v);
    g_assert_cmphex(ell->fill, ==, 0xFF0000);
    g_assert_cmphex(ell->line, ==, 0x0000FF);
    slide_deck_free(deck);
    g_bytes_unref(st);
}

static void test_ppt_fixture(void) {
    PreviewDoc* doc = fixture("roadmap.ppt");
    const gchar* t = doc->text->str;
    g_assert_true(g_str_has_prefix(t, "Lookin roadmap\nPowerPoint 97-2003 presentation · "));
    g_assert_nonnull(strstr(t, "2 slides"));
    g_assert_nonnull(strstr(t, "Slide 1 · Lookin roadmap\n" PREVIEW_DOC_IMAGE_CHAR "\nWelcome everyone.\n"));
    g_assert_nonnull(strstr(t, "Slide 2 · Previews\n"));
    g_assert_cmpuint(doc->images->len, ==, 2);
    preview_doc_free(doc);

    gchar* path = g_build_filename("tests", "fixtures", "preview", "roadmap.ppt", NULL);
    OleReader* ole = ole_reader_open(path, NULL);
    SlideDeck* deck = ppt_reader_read(ole, NULL, NULL);
    const Slide* s2 = g_ptr_array_index(deck->slides, 1);
    gboolean bullets = FALSE;
    for (guint i = 0; i < s2->shapes->len; i++) {
        const SlideShape* s = g_ptr_array_index(s2->shapes, i);
        gchar* text = slide_shape_text(s);
        g_assert_null(strstr(text, "Click to edit"));
        if (g_str_has_prefix(text, "Archives")) {
            g_assert_cmpstr(text, ==, "Archives\nSpreadsheets – old and new\nMail files");
            g_assert_cmpstr(para_of(s, 0)->bullet, ==, "•");
            g_assert_cmphex(s->fill, ==, SLIDE_NO_COLOR); /* LibreOffice's style, not drawn */
            bullets = TRUE;
        }
        g_free(text);
    }
    g_assert_true(bullets);
    slide_deck_free(deck);
    ole_reader_free(ole);
    g_free(path);
}

static void test_ppt_truncated(void) {
    gchar* path = g_build_filename("tests", "fixtures", "preview", "roadmap.ppt", NULL);
    OleReader* ole = ole_reader_open(path, NULL);
    GBytes* st = ole_reader_read(ole, "PowerPoint Document", 1 << 20, NULL);
    ole_reader_free(ole);
    gsize len = g_bytes_get_size(st);
    for (gsize cut = 0; cut < len; cut += len / 29 + 1) {
        GBytes* part = g_bytes_new_from_bytes(st, 0, cut);
        PreviewDoc* doc = presentation_preview(part);
        g_assert_nonnull(doc);
        preview_doc_free(doc);
        g_bytes_unref(part);
    }
    g_bytes_unref(st);
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
    g_test_add_func("/office97/word97_formatting", test_word97_formatting);
    g_test_add_func("/office97/word97_encrypted", test_word97_encrypted);
    g_test_add_func("/office97/word6", test_word6);
    g_test_add_func("/office97/word_fixture", test_word_fixture);
    g_test_add_func("/office97/word_truncated", test_word_truncated);
    g_test_add_func("/office97/excel_synthetic", test_excel_synthetic);
    g_test_add_func("/office97/excel_encrypted", test_excel_encrypted);
    g_test_add_func("/office97/excel5", test_excel5);
    g_test_add_func("/office97/excel_fixture", test_excel_fixture);
    g_test_add_func("/office97/excel_truncated", test_excel_truncated);
    g_test_add_func("/office97/ppt_synthetic", test_ppt_synthetic);
    g_test_add_func("/office97/ppt_drawing", test_ppt_drawing);
    g_test_add_func("/office97/ppt_fixture", test_ppt_fixture);
    g_test_add_func("/office97/ppt_truncated", test_ppt_truncated);
    return g_test_run();
}
