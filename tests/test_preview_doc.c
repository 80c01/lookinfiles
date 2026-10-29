/* Unit tests for src/preview_doc.c (styled preview documents, tables). */
#include "preview_doc.h"

#include <string.h>

/* Text of the characters [start, end) of the document */
static gchar* span_text(const PreviewDoc* doc, gint start, gint end) {
    const gchar* a = g_utf8_offset_to_pointer(doc->text->str, start);
    const gchar* b = g_utf8_offset_to_pointer(doc->text->str, end);
    return g_strndup(a, (gsize)(b - a));
}

/* Text of the first span with this style, NULL if none */
static gchar* styled(const PreviewDoc* doc, PreviewStyle style) {
    for (guint i = 0; i < doc->spans->len; i++) {
        const PreviewSpan* s = &g_array_index(doc->spans, PreviewSpan, i);
        if (s->style == style) {
            return span_text(doc, s->start, s->end);
        }
    }
    return NULL;
}

static void test_append_offsets(void) {
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    preview_doc_append(doc, PREVIEW_STYLE_PLAIN, "héllo ");
    g_assert_cmpint(preview_doc_offset(doc), ==, 6);
    preview_doc_append(doc, PREVIEW_STYLE_BOLD, "wörld");
    g_assert_cmpint(preview_doc_offset(doc), ==, 11);
    g_assert_cmpuint(doc->spans->len, ==, 1); /* PLAIN is not stored */
    g_autofree gchar* bold = styled(doc, PREVIEW_STYLE_BOLD);
    g_assert_cmpstr(bold, ==, "wörld");
    /* Invalid UTF-8 is repaired, offsets stay in characters */
    preview_doc_append_len(doc, "a\xFF" "b", 3);
    g_assert_true(g_utf8_validate(doc->text->str, -1, NULL));
    g_assert_cmpint(preview_doc_offset(doc), ==, (gint)g_utf8_strlen(doc->text->str, -1));
    preview_doc_appendf(doc, PREVIEW_STYLE_NUMBER, "%d", 42);
    g_autofree gchar* num = styled(doc, PREVIEW_STYLE_NUMBER);
    g_assert_cmpstr(num, ==, "42");
    /* Empty spans are ignored */
    preview_doc_add_span(doc, 3, 3, PREVIEW_STYLE_DIM);
    g_assert_null(styled(doc, PREVIEW_STYLE_DIM));
}

static void test_card(void) {
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    doc->label_width = 8;
    preview_doc_title(doc, "cert.pem", "Certificate · 1.2 kB");
    preview_doc_field(doc, "Subject", "CN=example");
    preview_doc_field(doc, "Empty", NULL);
    preview_doc_field(doc, "Names", "a.example\nb.example");
    preview_doc_section(doc, "Chain");
    preview_doc_item(doc, "root");
    preview_doc_note(doc, "1 more");
    g_assert_cmpstr(doc->text->str, ==,
                    "cert.pem\n"
                    "Certificate · 1.2 kB\n"
                    "\n"
                    "Subject  CN=example\n"
                    "Names    a.example\n"
                    "         b.example\n"
                    "\n"
                    "Chain\n"
                    "  root\n"
                    "1 more\n");
    g_autofree gchar* title = styled(doc, PREVIEW_STYLE_TITLE);
    g_assert_cmpstr(title, ==, "cert.pem");
    g_autofree gchar* section = styled(doc, PREVIEW_STYLE_SECTION);
    g_assert_cmpstr(section, ==, "Chain");
}

static void test_image_placeholder(void) {
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    preview_doc_append(doc, PREVIEW_STYLE_PLAIN, "ab");
    GBytes* data = g_bytes_new_static("PNG", 3);
    preview_doc_add_image(doc, data, 100);
    g_bytes_unref(data);
    g_assert_cmpuint(doc->images->len, ==, 1);
    const PreviewImage* image = &g_array_index(doc->images, PreviewImage, 0);
    g_assert_cmpint(image->offset, ==, 2);
    g_assert_cmpint(image->max_width, ==, 100);
    g_assert_cmpint(preview_doc_offset(doc), ==, 3);
    g_assert_cmpstr(doc->text->str, ==, "ab" PREVIEW_DOC_IMAGE_CHAR);
}

static void test_font_span(void) {
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    preview_doc_append(doc, PREVIEW_STYLE_PLAIN, "Sample");
    preview_doc_add_font_span(doc, 0, 6, "DejaVu Sans, Bold 24");
    preview_doc_add_font_file(doc, "/tmp/font.ttf");
    const PreviewSpan* s = &g_array_index(doc->spans, PreviewSpan, 0);
    g_assert_cmpint(s->style, ==, PREVIEW_STYLE_FONT);
    g_assert_cmpstr(s->arg, ==, "DejaVu Sans, Bold 24");
    g_assert_cmpuint(doc->font_files->len, ==, 1);
}

static void test_text_width(void) {
    g_assert_cmpuint(preview_text_width("abc"), ==, 3);
    g_assert_cmpuint(preview_text_width("日本"), ==, 4);
    g_assert_cmpuint(preview_text_width("e\xCC\x81"), ==, 1); /* e + combining acute */
    g_assert_cmpuint(preview_text_width(""), ==, 0);
}

static void test_numbers(void) {
    g_assert_true(preview_cell_is_number("12"));
    g_assert_true(preview_cell_is_number("-3.5"));
    g_assert_true(preview_cell_is_number("1,234"));
    g_assert_true(preview_cell_is_number("45%"));
    g_assert_true(preview_cell_is_number("1e9"));
    g_assert_true(preview_cell_is_number(".5"));
    g_assert_true(preview_cell_is_number("$10"));
    g_assert_false(preview_cell_is_number(""));
    g_assert_false(preview_cell_is_number("abc"));
    g_assert_false(preview_cell_is_number("2026-01-01"));
    g_assert_false(preview_cell_is_number("1e"));
    g_assert_false(preview_cell_is_number("-"));
}

static void test_table(void) {
    g_autoptr(PreviewTable) t = preview_table_new(3);
    const gchar* header[] = { "name", "qty", "note" };
    preview_table_set_header(t, header);
    const gchar* r1[] = { "apple", "12", "red\nfruit" };
    const gchar* r2[] = { "kiwi", "3" };
    preview_table_add_row(t, r1, 3);
    preview_table_add_row(t, r2, 2);
    g_assert_cmpuint(preview_table_get_n_rows(t), ==, 2);
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    preview_doc_add_table(doc, t);
    g_assert_true(doc->monospace);
    g_assert_false(doc->wrap);
    g_assert_cmpstr(doc->text->str, ==,
                    "name  │ qty │ note\n"
                    "──────┼─────┼──────────\n"
                    "apple │  12 │ red fruit\n"
                    "kiwi  │   3 │ \n");
    g_autofree gchar* header_text = styled(doc, PREVIEW_STYLE_HEADER);
    g_assert_cmpstr(header_text, ==, "name");
    g_autofree gchar* number = styled(doc, PREVIEW_STYLE_NUMBER);
    g_assert_cmpstr(number, ==, "12");
    g_autofree gchar* zebra = styled(doc, PREVIEW_STYLE_ROW_ALT);
    g_assert_cmpstr(zebra, ==, "kiwi  │   3 │ \n");
}

static void test_table_row_numbers_and_cut(void) {
    g_autoptr(PreviewTable) t = preview_table_new(1);
    preview_table_set_row_numbers(t, TRUE);
    preview_table_set_max_cell_width(t, 5);
    for (gint i = 0; i < 10; i++) {
        const gchar* cell[] = { i == 0 ? "abcdefgh" : "x" };
        preview_table_add_row(t, cell, 1);
    }
    g_autoptr(PreviewDoc) doc = preview_doc_new();
    preview_doc_add_table(doc, t);
    gchar** lines = g_strsplit(doc->text->str, "\n", -1);
    g_assert_cmpstr(lines[0], ==, " 1 │ abcd…");
    g_assert_cmpstr(lines[9], ==, "10 │ x");
    g_strfreev(lines);
}

static void test_source_doc(void) {
    g_autoptr(PreviewDoc) doc = preview_doc_new_source(PREVIEW_DOC_HTML, g_strdup("<p>é</p>"));
    g_assert_cmpint(doc->format, ==, PREVIEW_DOC_HTML);
    g_assert_cmpstr(doc->text->str, ==, "<p>é</p>");
    g_assert_cmpint(doc->n_chars, ==, 8);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/preview_doc/append_offsets", test_append_offsets);
    g_test_add_func("/preview_doc/card", test_card);
    g_test_add_func("/preview_doc/image_placeholder", test_image_placeholder);
    g_test_add_func("/preview_doc/font_span", test_font_span);
    g_test_add_func("/preview_doc/text_width", test_text_width);
    g_test_add_func("/preview_doc/numbers", test_numbers);
    g_test_add_func("/preview_doc/table", test_table);
    g_test_add_func("/preview_doc/table_row_numbers_and_cut", test_table_row_numbers_and_cut);
    g_test_add_func("/preview_doc/source_doc", test_source_doc);
    return g_test_run();
}
