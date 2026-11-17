/* Unit tests for src/office/doc_pages.c (documents drawn page by page) */
#include "office/doc_pages.h"

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string.h>

static void write_paragraph(DocWriter* w, const gchar* text) {
    DocRun run;
    doc_run_init(&run);
    doc_writer_text(w, text, -1, &run);
    doc_writer_end_paragraph(w, NULL, NULL, NULL);
}

/* A body of n paragraphs of a few lines each */
static DocModel* long_body(guint n) {
    DocWriter* w = doc_writer_new("DejaVu Serif", 11);
    for (guint i = 0; i < n; i++) {
        write_paragraph(w, "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor "
                           "incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud "
                           "exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.");
    }
    return doc_writer_finish_model(w);
}

static DocModel* one_line(const gchar* text) {
    DocWriter* w = doc_writer_new(NULL, 11);
    write_paragraph(w, text);
    return doc_writer_finish_model(w);
}

static guint pictures(const PreviewDoc* doc) {
    return doc->images->len;
}

/* Pixels of a page picture that are not white */
static guint inked(const PreviewImage* img, gint y0, gint y1) {
    guint n = 0;
    const guint8* data = g_bytes_get_data(img->data, NULL);
    for (gint y = MAX(y0, 0); y < MIN(y1, img->height); y++) {
        const guint32* row = (const guint32*)(data + (gsize)y * img->stride);
        for (gint x = 0; x < img->width; x++) {
            n += (row[x] & 0xFFFFFF) != 0xFFFFFF;
        }
    }
    return n;
}

static void test_one_page(void) {
    DocPageSetup page;
    doc_page_setup_init(&page);
    g_autoptr(DocModel) body = one_line("Hello pages");
    g_assert_cmpuint(doc_pages_count(&page, NULL, body, NULL), ==, 1);
    g_autoptr(PreviewDoc) doc = doc_pages_preview("Report", "Word document", &page, NULL, body, NULL, NULL, 1.0);
    g_assert_true(doc->paged);
    g_assert_nonnull(strstr(doc->text->str, "Report"));
    g_assert_nonnull(strstr(doc->text->str, "Word document · 1 page"));
    g_assert_cmpuint(pictures(doc), ==, 1);
    const PreviewImage* img = &g_array_index(doc->images, PreviewImage, 0);
    /* A4 at 1.6 pixels per point */
    g_assert_cmpint(img->width, ==, 952);
    g_assert_cmpint(img->height, ==, 1347);
    g_assert_cmpint(img->stride, >=, img->width * 4);
    /* The text in the top margin box, nothing in the margin above it */
    gint top = (gint)(page.top * 1.6);
    g_assert_cmpuint(inked(img, 0, top - 2), ==, 0);
    g_assert_cmpuint(inked(img, top, top + 40), >, 0);
}

static void test_flow(void) {
    DocPageSetup page;
    doc_page_setup_init(&page);
    g_autoptr(DocModel) body = long_body(60);
    guint a4 = doc_pages_count(&page, NULL, body, NULL);
    g_assert_cmpuint(a4, >, 2);
    /* A smaller page holds less */
    DocPageSetup a5 = page;
    a5.width = 419.5;
    a5.height = 595.3;
    g_assert_cmpuint(doc_pages_count(&a5, NULL, body, NULL), >, a4);
    /* So do wider margins */
    DocPageSetup wide = page;
    wide.left = wide.right = 150;
    g_assert_cmpuint(doc_pages_count(&wide, NULL, body, NULL), >, a4);
}

static void test_page_breaks(void) {
    DocPageSetup page;
    doc_page_setup_init(&page);
    DocWriter* w = doc_writer_new(NULL, 11);
    doc_writer_page_break(w); /* at the start: no empty first page */
    write_paragraph(w, "One");
    doc_writer_page_break(w);
    write_paragraph(w, "Two");
    doc_writer_page_break(w);
    write_paragraph(w, "Three");
    g_autoptr(DocModel) body = doc_writer_finish_model(w);
    g_assert_cmpuint(doc_pages_count(&page, NULL, body, NULL), ==, 3);
    g_autoptr(PreviewDoc) doc = doc_pages_preview("T", NULL, &page, NULL, body, NULL, NULL, 1.0);
    g_assert_nonnull(strstr(doc->text->str, "Page 3 of 3"));
    g_assert_cmpuint(pictures(doc), ==, 3);
}

static void test_header_footer(void) {
    DocPageSetup page;
    doc_page_setup_init(&page);
    g_autoptr(DocModel) body = long_body(120);
    guint plain = doc_pages_count(&page, NULL, body, NULL);
    /* A tall header pushes the body down: more pages */
    DocWriter* w = doc_writer_new(NULL, 11);
    for (gint i = 0; i < 12; i++) {
        write_paragraph(w, "Header line");
    }
    g_autoptr(DocModel) header = doc_writer_finish_model(w);
    g_autoptr(DocModel) footer = one_line("Footer");
    g_assert_cmpuint(doc_pages_count(&page, header, body, footer), >, plain);
    /* Header and footer are drawn on every page, in the margins */
    g_autoptr(DocModel) short_body = one_line("Body");
    g_autoptr(DocModel) small_header = one_line("Header");
    g_autoptr(PreviewDoc) doc = doc_pages_preview("T", NULL, &page, small_header, short_body, footer, NULL, 1.0);
    const PreviewImage* img = &g_array_index(doc->images, PreviewImage, 0);
    gint header_y = (gint)(page.header * 1.6), footer_y = (gint)((page.height - page.footer) * 1.6);
    g_assert_cmpuint(inked(img, header_y, header_y + 24), >, 0);
    g_assert_cmpuint(inked(img, footer_y - 24, footer_y), >, 0);
    /* Empty stories are left out */
    g_autoptr(DocModel) empty = one_line("   ");
    g_assert_cmpuint(doc_pages_count(&page, empty, body, empty), ==, plain);
}

static void test_tables(void) {
    DocPageSetup page;
    doc_page_setup_init(&page);
    DocWriter* w = doc_writer_new(NULL, 11);
    doc_writer_table_start(w);
    DocRun run;
    doc_run_init(&run);
    for (gint r = 0; r < 120; r++) {
        doc_writer_row_start(w, r == 0);
        for (gint c = 0; c < 3; c++) {
            doc_writer_cell_start(w, 1);
            gchar* text = g_strdup_printf("Cell %d.%d", r, c);
            doc_writer_text(w, text, -1, &run);
            doc_writer_end_paragraph(w, NULL, NULL, NULL);
            g_free(text);
            doc_writer_cell_end(w);
        }
        doc_writer_row_end(w);
    }
    doc_writer_table_end(w);
    g_autoptr(DocModel) body = doc_writer_finish_model(w);
    guint n = doc_pages_count(&page, NULL, body, NULL);
    g_assert_cmpuint(n, >=, 2);
    g_assert_cmpuint(n, <=, 8);
    g_autoptr(PreviewDoc) doc = doc_pages_preview("T", NULL, &page, NULL, body, NULL, NULL, 1.0);
    /* Borders: ink at the left edge of the body column */
    const PreviewImage* img = &g_array_index(doc->images, PreviewImage, 0);
    gint top = (gint)(page.top * 1.6), x = (gint)(page.left * 1.6);
    const guint8* data = g_bytes_get_data(img->data, NULL);
    guint dark = 0;
    for (gint y = top; y < top + 200; y++) {
        const guint32* row = (const guint32*)(data + (gsize)y * img->stride);
        dark += (row[x] & 0xFF) < 0x80 || (row[x + 1] & 0xFF) < 0x80;
    }
    g_assert_cmpuint(dark, >, 150);
}

static void test_picture(void) {
    DocPageSetup page;
    doc_page_setup_init(&page);
    GdkPixbuf* pix = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 40, 20);
    gdk_pixbuf_fill(pix, 0xFF0000FF);
    gchar* png = NULL;
    gsize len = 0;
    g_assert_true(gdk_pixbuf_save_to_buffer(pix, &png, &len, "png", NULL, NULL));
    g_object_unref(pix);
    GBytes* bytes = g_bytes_new_take(png, len);
    DocWriter* w = doc_writer_new(NULL, 11);
    doc_writer_image(w, bytes, 144, "red.png"); /* 2 inches wide */
    doc_writer_end_paragraph(w, NULL, NULL, NULL);
    doc_writer_image(w, NULL, 0, "chart.emf");
    doc_writer_end_paragraph(w, NULL, NULL, NULL);
    g_bytes_unref(bytes);
    g_autoptr(DocModel) body = doc_writer_finish_model(w);
    g_autoptr(PreviewDoc) doc = doc_pages_preview("T", NULL, &page, NULL, body, NULL, NULL, 1.0);
    const PreviewImage* img = &g_array_index(doc->images, PreviewImage, 0);
    /* Red pixels: 144 pt wide (230 px), 115 px high */
    gint top = (gint)(page.top * 1.6), left = (gint)(page.left * 1.6);
    const guint8* data = g_bytes_get_data(img->data, NULL);
    const guint32* row = (const guint32*)(data + (gsize)(top + 50) * img->stride);
    g_assert_cmphex(row[left + 10] & 0xFFFFFF, ==, 0xFF0000);
    g_assert_cmphex(row[left + 225] & 0xFFFFFF, ==, 0xFF0000);
    g_assert_cmphex(row[left + 240] & 0xFFFFFF, ==, 0xFFFFFF);
}

static void test_limits(void) {
    DocPageSetup page;
    doc_page_setup_init(&page);
    DocWriter* w = doc_writer_new(NULL, 11);
    for (gint i = 0; i < 30; i++) {
        write_paragraph(w, "Page");
        doc_writer_page_break(w);
    }
    write_paragraph(w, "Last");
    g_autoptr(DocModel) body = doc_writer_finish_model(w);
    g_autoptr(PreviewDoc) doc = doc_pages_preview("T", "S", &page, NULL, body, NULL, "Truncated.", 1.0);
    g_assert_cmpuint(pictures(doc), ==, DOC_PAGES_MAX_DRAWN);
    g_assert_nonnull(strstr(doc->text->str, "S · 31 pages"));
    g_assert_nonnull(strstr(doc->text->str, "The first 20 of 31 pages are drawn"));
    g_assert_nonnull(strstr(doc->text->str, "Truncated."));
    /* Zoomed: fewer pages drawn (memory), larger */
    g_autoptr(PreviewDoc) zoomed = doc_pages_preview("T", "S", &page, NULL, body, NULL, NULL, 3.0);
    g_assert_cmpuint(pictures(zoomed), <, DOC_PAGES_MAX_DRAWN);
    g_assert_cmpuint(pictures(zoomed), >=, 1);
    g_assert_cmpint(g_array_index(zoomed->images, PreviewImage, 0).width, ==, 2857); /* 595.3 pt x 4.8 */
    /* A page setup out of range is put back to a usable one */
    DocPageSetup broken = { 0, -5, 900, 900, 500, 500, 2000, 2000 };
    g_assert_cmpuint(doc_pages_count(&broken, NULL, body, NULL), ==, 31);
}

static void test_text_view(void) {
    DocPageSetup page;
    doc_page_setup_init(&page);
    g_autoptr(DocModel) body = one_line("Body text");
    g_autoptr(DocModel) header = one_line("Head text");
    g_autoptr(PreviewDoc) doc = doc_pages_or_text(FALSE, "T", "S", &page, header, body, NULL, NULL, 1.0);
    g_assert_false(doc->paged);
    g_assert_nonnull(strstr(doc->text->str, "Head text"));
    g_assert_nonnull(strstr(doc->text->str, "Body text"));
    g_autoptr(PreviewDoc) pages = doc_pages_or_text(TRUE, "T", "S", &page, header, body, NULL, NULL, 1.0);
    g_assert_true(pages->paged);
    g_assert_null(strstr(pages->text->str, "Body text"));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/doc_pages/one_page", test_one_page);
    g_test_add_func("/doc_pages/flow", test_flow);
    g_test_add_func("/doc_pages/page_breaks", test_page_breaks);
    g_test_add_func("/doc_pages/header_footer", test_header_footer);
    g_test_add_func("/doc_pages/tables", test_tables);
    g_test_add_func("/doc_pages/picture", test_picture);
    g_test_add_func("/doc_pages/limits", test_limits);
    g_test_add_func("/doc_pages/text_view", test_text_view);
    return g_test_run();
}
