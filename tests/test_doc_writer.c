/* Unit tests for src/doc_writer.c (Word-like documents as preview documents) */
#include "doc_writer.h"

#include <string.h>

/* Argument of the first span of a style over the text "what" */
static const PreviewSpan* span_on(const PreviewDoc* doc, PreviewStyle style, const gchar* what) {
    const gchar* at = strstr(doc->text->str, what);
    g_assert_nonnull(at);
    gint start = (gint)g_utf8_pointer_to_offset(doc->text->str, at);
    gint end = start + (gint)g_utf8_strlen(what, -1);
    for (guint i = 0; i < doc->spans->len; i++) {
        const PreviewSpan* s = &g_array_index(doc->spans, PreviewSpan, i);
        if (s->style == style && s->start <= start && s->end >= end) {
            return s;
        }
    }
    return NULL;
}

static void test_runs_and_paragraphs(void) {
    DocWriter* w = doc_writer_new("Liberation Serif", 12);
    DocRun run;
    doc_run_init(&run);
    run.size = 24;
    run.bold = TRUE;
    run.color = 0x1F3864;
    doc_writer_text(w, "Big title", -1, &run);
    DocPara para;
    doc_para_init(&para);
    para.align = PREVIEW_ALIGN_CENTER;
    para.after = 12;
    doc_writer_end_paragraph(w, &para, NULL, NULL);

    doc_run_init(&run);
    doc_writer_text(w, "Plain \x01then ", -1, &run);
    run.italic = TRUE;
    run.font = "DejaVu Sans";
    doc_writer_text(w, "italic", -1, &run);
    run.italic = FALSE;
    run.font = NULL;
    run.highlight = 0xFFFF00;
    doc_writer_text(w, " marked", -1, &run);
    doc_run_init(&run);
    run.vert = 1;
    doc_writer_text(w, "2", -1, &run);
    doc_run_init(&run);
    run.caps = TRUE;
    doc_writer_text(w, " loud", -1, &run);
    doc_writer_end_paragraph(w, NULL, NULL, NULL);

    doc_para_init(&para);
    para.left = 36;
    para.first = -18;
    doc_writer_text(w, "First item", -1, &run);
    doc_writer_end_paragraph(w, &para, "1.", NULL);
    g_assert_cmpuint(doc_writer_length(w), ==, strlen("Big title") + strlen("Plain then italic marked2 loud") +
                                                   strlen("FIRST ITEM"));

    g_autoptr(PreviewDoc) doc = doc_writer_finish(w);
    g_assert_cmpstr(doc->text->str, ==, "Big title\nPlain then italic marked2 LOUD\n1.\xE2\x80\x82" "FIRST ITEM\n");

    const PreviewSpan* s = span_on(doc, PREVIEW_STYLE_SCALE, "Big title");
    g_assert_nonnull(s);
    g_assert_cmpstr(s->arg, ==, "2.000");
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_BOLD, "Big title"));
    s = span_on(doc, PREVIEW_STYLE_COLOR, "Big title");
    g_assert_cmpstr(s->arg, ==, "#1f3864");
    s = span_on(doc, PREVIEW_STYLE_PARAGRAPH, "Big title\n");
    PreviewParagraph layout;
    g_assert_true(preview_paragraph_parse(s->arg, &layout));
    g_assert_cmpint(layout.align, ==, PREVIEW_ALIGN_CENTER);
    g_assert_cmpint(layout.below, ==, 16);

    /* The default font on every run, the run's own font where it has one */
    s = span_on(doc, PREVIEW_STYLE_FAMILY, "Plain then ");
    g_assert_cmpstr(s->arg, ==, "Liberation Serif");
    s = span_on(doc, PREVIEW_STYLE_FAMILY, "italic");
    g_assert_cmpstr(s->arg, ==, "DejaVu Sans");
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_ITALIC, "italic"));
    g_assert_null(span_on(doc, PREVIEW_STYLE_SCALE, "italic"));
    s = span_on(doc, PREVIEW_STYLE_HIGHLIGHT, " marked");
    g_assert_cmpstr(s->arg, ==, "#ffff00");
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_SUPERSCRIPT, "2"));

    /* Hanging indent of the list item: the label stands out on the left */
    s = span_on(doc, PREVIEW_STYLE_PARAGRAPH, "FIRST ITEM");
    g_assert_true(preview_paragraph_parse(s->arg, &layout));
    g_assert_cmpint(layout.left, ==, 48);
    g_assert_cmpint(layout.indent, ==, -24);
}

static void test_table(void) {
    DocWriter* w = doc_writer_new(NULL, 0);
    DocRun run;
    doc_run_init(&run);
    DocPara right;
    doc_para_init(&right);
    right.align = PREVIEW_ALIGN_RIGHT;
    doc_writer_table_start(w);
    doc_writer_row_start(w, TRUE);
    doc_writer_cell_start(w, 1);
    doc_writer_text(w, "Quarter", -1, &run);
    doc_writer_end_paragraph(w, NULL, NULL, NULL);
    doc_writer_cell_end(w);
    doc_writer_cell_start(w, 1);
    doc_writer_text(w, "Files", -1, &run);
    doc_writer_cell_end(w); /* text without a paragraph end */
    doc_writer_row_end(w);
    doc_writer_row_start(w, FALSE);
    doc_writer_cell_start(w, 1);
    doc_writer_text(w, "Q1", -1, &run);
    doc_writer_end_paragraph(w, NULL, NULL, NULL);
    doc_writer_cell_end(w);
    doc_writer_cell_start(w, 1);
    doc_writer_text(w, "12", -1, &run);
    doc_writer_end_paragraph(w, &right, NULL, NULL);
    /* A nested table is flattened into the cell */
    doc_writer_table_start(w);
    doc_writer_row_start(w, FALSE);
    doc_writer_cell_start(w, 1);
    doc_writer_text(w, "in", -1, &run);
    doc_writer_end_paragraph(w, NULL, NULL, NULL);
    doc_writer_cell_end(w);
    doc_writer_row_end(w);
    doc_writer_table_end(w);
    doc_writer_cell_end(w);
    doc_writer_row_end(w);
    doc_writer_row_start(w, FALSE);
    doc_writer_cell_start(w, 2);
    doc_writer_text(w, "Total over both columns", -1, &run);
    doc_writer_end_paragraph(w, NULL, NULL, NULL);
    doc_writer_cell_end(w);
    doc_writer_row_end(w);
    doc_writer_table_end(w);
    doc_writer_text(w, "After", -1, &run);
    g_autoptr(PreviewDoc) doc = doc_writer_finish(w);
    g_assert_cmpstr(doc->text->str, ==,
                    "┌─────────┬───────────────┐\n"
                    "│ Quarter │ Files         │\n"
                    "├─────────┼───────────────┤\n"
                    "│ Q1      │            12 │\n"
                    "│         │ in            │\n"
                    "├─────────┼───────────────┤\n"
                    "│ Total over both columns │\n"
                    "└─────────┴───────────────┘\n"
                    "After\n");
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_BOLD, "Quarter"));
    g_assert_null(span_on(doc, PREVIEW_STYLE_BOLD, "Q1"));
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_TABLE, "│ Q1"));
    g_assert_null(span_on(doc, PREVIEW_STYLE_TABLE, "After"));
}

static void test_wrapped_cells(void) {
    DocWriter* w = doc_writer_new(NULL, 0);
    DocRun run;
    doc_run_init(&run);
    doc_writer_table_start(w);
    doc_writer_row_start(w, FALSE);
    for (gint i = 0; i < 3; i++) {
        doc_writer_cell_start(w, 1);
        doc_writer_text(w, "a long sentence that cannot fit in one column of the grid at all", -1, &run);
        doc_writer_end_paragraph(w, NULL, NULL, NULL);
        doc_writer_cell_end(w);
    }
    doc_writer_table_end(w);
    g_autoptr(PreviewDoc) doc = doc_writer_finish(w);
    gchar** lines = g_strsplit(doc->text->str, "\n", -1);
    g_assert_cmpuint(g_strv_length(lines), >, 4);
    for (guint i = 0; lines[i][0] != '\0'; i++) {
        g_assert_cmpuint(g_utf8_strlen(lines[i], -1), <=, 110);
        g_assert_cmpuint(g_utf8_strlen(lines[i], -1), ==, g_utf8_strlen(lines[0], -1));
    }
    g_assert_true(g_str_has_prefix(lines[1], "│ a long sentence that cannot fit "));
    g_strfreev(lines);
}

static void test_page(void) {
    DocRun run;
    doc_run_init(&run);
    DocWriter* h = doc_writer_new(NULL, 0);
    doc_writer_text(h, "Header text", -1, &run);
    g_autoptr(PreviewDoc) header = doc_writer_finish(h);
    DocWriter* w = doc_writer_new(NULL, 0);
    doc_writer_text(w, "Page one", -1, &run);
    doc_writer_page_break(w);
    doc_writer_text(w, "Page two", -1, &run);
    GBytes* png = g_bytes_new_static("x", 1);
    doc_writer_image(w, png, 72, "a.png");
    g_bytes_unref(png);
    doc_writer_image(w, NULL, 0, "chart.emf");
    g_autoptr(PreviewDoc) body = doc_writer_finish(w);
    g_assert_cmpuint(body->images->len, ==, 1);
    g_assert_cmpint(g_array_index(body->images, PreviewImage, 0).max_width, ==, 96);
    g_assert_nonnull(strstr(body->text->str, "Page one\n────"));
    g_assert_nonnull(strstr(body->text->str, "Page two" PREVIEW_DOC_IMAGE_CHAR "[chart.emf]\n"));

    g_autoptr(PreviewDoc) doc = doc_writer_page("Report", "Word document", header, body, NULL, "Truncated.");
    g_assert_true(g_str_has_prefix(doc->text->str, "Report\nWord document\n\nHeader text\n────"));
    g_assert_nonnull(span_on(doc, PREVIEW_STYLE_DIM, "Header text"));
    g_assert_null(span_on(doc, PREVIEW_STYLE_DIM, "Page one"));
    g_assert_nonnull(strstr(doc->text->str, "Truncated."));
    g_assert_cmpuint(doc->images->len, ==, 1);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/doc_writer/runs_and_paragraphs", test_runs_and_paragraphs);
    g_test_add_func("/doc_writer/table", test_table);
    g_test_add_func("/doc_writer/wrapped_cells", test_wrapped_cells);
    g_test_add_func("/doc_writer/page", test_page);
    return g_test_run();
}
