/* Unit tests for src/office/odt_reader.c (OpenDocument text page by page) */
#include "office/odt_reader.h"

#include <string.h>

#define FIXTURE "tests/fixtures/preview/report.odt"

/* The first segment of the body whose text contains what, or NULL */
static const DocSeg* find_seg(const DocModel* m, const gchar* what, const DocParagraph** para) {
    for (guint i = 0; i < m->blocks->len; i++) {
        const DocBlock* b = g_ptr_array_index(m->blocks, i);
        if (b->kind != DOC_BLOCK_PARAGRAPH) continue;
        for (guint k = 0; k < b->para->segs->len; k++) {
            const DocSeg* s = g_ptr_array_index(b->para->segs, k);
            if (s->text != NULL && strstr(s->text, what) != NULL) {
                if (para != NULL) *para = b->para;
                return s;
            }
        }
    }
    return NULL;
}

static gboolean has_picture(const DocModel* m) {
    for (guint i = 0; i < m->blocks->len; i++) {
        const DocBlock* b = g_ptr_array_index(m->blocks, i);
        for (guint k = 0; b->para != NULL && k < b->para->segs->len; k++) {
            const DocSeg* s = g_ptr_array_index(b->para->segs, k);
            if (s->text == NULL && s->image != NULL) return TRUE;
        }
    }
    return FALSE;
}

static guint tables(const DocModel* m) {
    guint n = 0;
    for (guint i = 0; i < m->blocks->len; i++) {
        n += ((DocBlock*)g_ptr_array_index(m->blocks, i))->kind == DOC_BLOCK_TABLE;
    }
    return n;
}

static void test_read(void) {
    GError* err = NULL;
    g_autoptr(ZipReader) zip = zip_reader_open(FIXTURE, &err);
    g_assert_no_error(err);
    g_autoptr(OdtDocument) odt = odt_reader_read(zip, 100000, &err);
    g_assert_no_error(err);
    g_assert_cmpstr(odt->title, ==, "Q3 report");
    g_assert_cmpstr(odt->author, ==, "Ada Lovelace");
    g_assert_false(odt->truncated);
    g_assert_cmpfloat(odt->page.width, >, 100);
    g_assert_cmpfloat(odt->page.height, >, odt->page.width);

    const DocParagraph* para = NULL;
    const DocSeg* s = find_seg(odt->body, "Summary", &para);
    g_assert_nonnull(s);
    g_assert_cmpint(para->layout.outline, ==, 1);
    s = find_seg(odt->body, "steadily", NULL);
    g_assert_nonnull(s);
    g_assert_true(s->run.italic);
    s = find_seg(odt->body, "new office", NULL);
    g_assert_true(s->run.bold);
    g_assert_true(s->run.underline);
    s = find_seg(odt->body, "version 2", NULL);
    g_assert_true(s->run.link);
    g_assert_null(find_seg(odt->body, "hidden footnote", NULL));
    /* Numbered list: its label first, a hanging indent */
    s = find_seg(odt->body, "Hire two people", &para);
    g_assert_nonnull(s);
    const DocSeg* label = g_ptr_array_index(para->segs, 0);
    g_assert_true(g_str_has_prefix(label->text, "1"));
    g_assert_cmpfloat(para->layout.first, <, 0);
    /* Line break inside a paragraph */
    g_assert_nonnull(find_seg(odt->body, "Line one\xE2\x80\xA8line two", NULL));
    g_assert_cmpuint(tables(odt->body), ==, 1);
    g_assert_true(has_picture(odt->body));

    /* The text view of the model has it all */
    g_autoptr(PreviewDoc) text = doc_model_preview(odt->body);
    g_assert_nonnull(strstr(text->text->str, "Quarterly report"));
    g_assert_nonnull(strstr(text->text->str, "Quarter"));
}

static void test_pages(void) {
    GError* err = NULL;
    g_autoptr(ZipReader) zip = zip_reader_open(FIXTURE, &err);
    g_assert_no_error(err);
    g_autoptr(PreviewDoc) doc = odt_reader_pages(zip, "report.odt", "9 kB", 100000, 1.0, &err);
    g_assert_no_error(err);
    g_assert_true(doc->paged);
    g_assert_nonnull(strstr(doc->text->str, "Q3 report"));
    g_assert_nonnull(strstr(doc->text->str, "OpenDocument text · Ada Lovelace · 9 kB · 1 page"));
    g_assert_cmpuint(doc->images->len, ==, 1);
}

static void test_truncated(void) {
    g_autoptr(ZipReader) zip = zip_reader_open(FIXTURE, NULL);
    g_autoptr(OdtDocument) odt = odt_reader_read(zip, 20, NULL);
    g_assert_true(odt->truncated);
    g_assert_null(find_seg(odt->body, "version 2", NULL));
    g_autoptr(PreviewDoc) doc = odt_reader_pages(zip, "report.odt", NULL, 20, 1.0, NULL);
    g_assert_nonnull(strstr(doc->text->str, "Document truncated"));
}

static void test_not_odt(void) {
    GError* err = NULL;
    g_assert_null(odt_file_pages("tests/fixtures/preview/sample.zip", "x", NULL, 100, 1.0, &err));
    g_assert_nonnull(err);
    g_clear_error(&err);
    g_assert_null(odt_file_pages("/nonexistent.odt", "x", NULL, 100, 1.0, &err));
    g_assert_nonnull(err);
    g_clear_error(&err);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/odt_reader/read", test_read);
    g_test_add_func("/odt_reader/pages", test_pages);
    g_test_add_func("/odt_reader/truncated", test_truncated);
    g_test_add_func("/odt_reader/not_odt", test_not_odt);
    return g_test_run();
}
