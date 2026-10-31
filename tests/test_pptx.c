/* Unit tests for src/pptx.c and src/pptx_shapes.c: presentations as slides. */
#include "pptx.h"

#include <string.h>

/* First shape of a slide whose text starts with prefix (NULL if none) */
static const SlideShape* shape_with_text(const Slide* slide, const gchar* prefix) {
    for (guint i = 0; i < slide->shapes->len; i++) {
        const SlideShape* s = g_ptr_array_index(slide->shapes, i);
        gchar* text = slide_shape_text(s);
        gboolean match = g_str_has_prefix(text, prefix);
        g_free(text);
        if (match) {
            return s;
        }
    }
    return NULL;
}

static const SlideRun* first_run(const SlideShape* s, guint para) {
    const SlideParagraph* p = g_ptr_array_index(s->paragraphs, para);
    return g_ptr_array_index(p->runs, 0);
}

static void test_minimal(void) {
    /* No master, layout, theme or positions: the Office defaults */
    GError* error = NULL;
    g_autoptr(SlideDeck) deck = pptx_read_deck("tests/fixtures/preview/roadmap.pptx", &error);
    g_assert_no_error(error);
    g_assert_cmpuint(deck->slides->len, ==, 2);
    const Slide* s1 = g_ptr_array_index(deck->slides, 0);
    g_assert_cmpstr(s1->title, ==, "Lookin roadmap");
    const SlideShape* title = shape_with_text(s1, "Lookin roadmap");
    g_assert_cmpfloat(first_run(title, 0)->size, ==, 54);
    g_assert_cmpint(((SlideParagraph*)g_ptr_array_index(title->paragraphs, 0))->align, ==, SLIDE_ALIGN_CENTER);
    const SlideShape* sub = shape_with_text(s1, "Autumn 2026");
    g_assert_null(((SlideParagraph*)g_ptr_array_index(sub->paragraphs, 0))->bullet);

    const Slide* s2 = g_ptr_array_index(deck->slides, 1);
    g_assert_cmpstr(s2->notes, ==, "Secret speaker notes");
    const SlideShape* body = shape_with_text(s2, "Archives");
    g_assert_cmpstr(((SlideParagraph*)g_ptr_array_index(body->paragraphs, 0))->bullet, ==, "•");
    g_assert_true(first_run(body, 0)->bold);
    g_assert_cmpfloat(first_run(body, 1)->size, <, first_run(body, 0)->size);
    /* The table and picture, unpositioned, share the body's area */
    const SlideShape* table = shape_with_text(s2, "Format");
    g_assert_nonnull(table->table);
    g_assert_cmpuint(table->table->rows, ==, 2);
    g_assert_cmpfloat(table->x, >, body->x + body->w);
    gboolean picture = FALSE;
    for (guint i = 0; i < s2->shapes->len; i++) {
        picture |= ((SlideShape*)g_ptr_array_index(s2->shapes, i))->picture != NULL;
    }
    g_assert_true(picture);
}

static void test_inheritance(void) {
    GError* error = NULL;
    g_autoptr(SlideDeck) deck = pptx_read_deck("tests/fixtures/preview/deck.pptx", &error);
    g_assert_no_error(error);
    g_assert_cmpfloat(deck->width, ==, 960);
    g_assert_cmpfloat(deck->height, ==, 540);
    g_assert_cmpstr(deck->font, ==, "DejaVu Sans");
    g_assert_cmpuint(deck->slides->len, ==, 3);

    /* Title slide: the layout's gradient, no master shapes, its placeholders */
    const Slide* s1 = g_ptr_array_index(deck->slides, 0);
    g_assert_cmphex(s1->background, ==, 0x1F2A44);
    g_assert_cmphex(s1->background_end, ==, 0x2E6BD6);
    g_assert_cmpuint(s1->shapes->len, ==, 2);
    const SlideShape* title = shape_with_text(s1, "Previews that look right");
    g_assert_cmpfloat(title->x, ==, 72);
    g_assert_cmpfloat(title->w, ==, 816);
    g_assert_cmpint(title->anchor, ==, SLIDE_ANCHOR_BOTTOM);
    const SlideRun* r = first_run(title, 0);
    g_assert_cmpfloat(r->size, ==, 54);
    g_assert_true(r->bold);                       /* master title style */
    g_assert_cmpstr(r->font, ==, "DejaVu Serif"); /* +mj-lt */
    g_assert_cmphex(r->color, ==, 0xFFFFFF);      /* layout list style: bg1 */
    const SlideShape* sub = shape_with_text(s1, "Autumn");
    g_assert_cmphex(first_run(sub, 0)->color, ==, 0xD9D9D9); /* bg1, lumMod 85% */

    /* Content slide: master band, layout footer, master background */
    const Slide* s2 = g_ptr_array_index(deck->slides, 1);
    g_assert_cmphex(s2->background, ==, 0xEEF1F6);
    g_assert_cmpstr(s2->notes, ==, "Mention the colour fixes.");
    const SlideShape* band = g_ptr_array_index(s2->shapes, 0);
    g_assert_cmphex(band->fill, ==, 0x2E6BD6);
    g_assert_nonnull(shape_with_text(s2, "Lookin · preview deck"));
    const SlideShape* body = shape_with_text(s2, "Word documents");
    g_assert_cmpfloat(body->w, ==, 420);           /* layout placeholder */
    g_assert_cmpfloat(body->font_scale, ==, 0.925);
    const SlideParagraph* p0 = g_ptr_array_index(body->paragraphs, 0);
    g_assert_cmpstr(p0->bullet, ==, "•");
    g_assert_cmphex(p0->bullet_color, ==, 0xE8833A);
    g_assert_cmpfloat(p0->left, ==, 18);
    g_assert_cmpfloat(p0->indent, ==, -18);
    g_assert_cmphex(first_run(body, 0)->color, ==, 0x404040); /* tx1 lumMod 75% lumOff 25% */
    const SlideRun* red = g_ptr_array_index(p0->runs, 1);
    g_assert_true(red->italic);
    g_assert_cmphex(red->color, ==, 0xC0392B);
    const SlideParagraph* p1 = g_ptr_array_index(body->paragraphs, 1);
    g_assert_cmpstr(p1->bullet, ==, "–");
    g_assert_cmpfloat(first_run(body, 1)->size, ==, 20);
    g_assert_cmpstr(((SlideParagraph*)g_ptr_array_index(body->paragraphs, 3))->bullet, ==, "1.");
    g_assert_cmpstr(((SlideParagraph*)g_ptr_array_index(body->paragraphs, 4))->bullet, ==, "2.");
    /* Group: child coordinates scaled into the group's box; style references */
    const SlideShape* circle = shape_with_text(s2, "3×");
    g_assert_cmpfloat(circle->x, ==, 500);
    g_assert_cmpfloat(circle->w, ==, 90);
    g_assert_cmpint(circle->geometry, ==, SLIDE_GEOM_ELLIPSE);
    g_assert_cmphex(circle->fill, ==, 0x4FB0A5);
    g_assert_cmphex(circle->line, ==, 0x285853); /* accent5, shade 50% */
    g_assert_cmphex(first_run(circle, 0)->color, ==, 0xFFFFFF);
    gboolean star = FALSE;
    for (guint i = 0; i < s2->shapes->len; i++) {
        const SlideShape* s = g_ptr_array_index(s2->shapes, i);
        if (s->geometry == SLIDE_GEOM_PATH) {
            star = TRUE;
            g_assert_cmpfloat(s->rotation, ==, 15);
            g_assert_cmpuint(s->path->len, ==, 11);
            g_assert_cmphex(s->line, ==, 0xE8833A);
            g_assert_cmpfloat(s->line_width, ==, 1.5);
        }
    }
    g_assert_true(star);

    /* Table style: header row, banded rows */
    const Slide* s3 = g_ptr_array_index(deck->slides, 2);
    const SlideShape* table = shape_with_text(s3, "Format");
    g_assert_cmpuint(table->table->cols, ==, 3);
    g_assert_cmpfloat(table->table->col_widths[0], ==, 400);
    const SlideCell* head = slide_table_cell(table->table, 0, 0);
    g_assert_cmphex(head->fill, ==, 0x2E6BD6);
    const SlideRun* hr = g_ptr_array_index(((SlideParagraph*)g_ptr_array_index(head->paragraphs, 0))->runs, 0);
    g_assert_true(hr->bold);
    g_assert_cmphex(hr->color, ==, 0xFFFFFF);
    g_assert_cmphex(slide_table_cell(table->table, 1, 0)->fill, !=, slide_table_cell(table->table, 2, 0)->fill);
}

static void test_preview(void) {
    GError* error = NULL;
    guint n = 0;
    g_autoptr(PreviewDoc) doc = pptx_file_preview("tests/fixtures/preview/deck.pptx", "deck.pptx", "8 kB", &n, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(n, ==, 3);
    g_assert_true(g_str_has_prefix(doc->text->str, "deck.pptx\nPowerPoint presentation · 3 slides · 8 kB\n"));
    g_assert_nonnull(strstr(doc->text->str, "Slide 2 · What changed\n" PREVIEW_DOC_IMAGE_CHAR "\nMention the colour fixes."));
    g_assert_cmpuint(doc->images->len, ==, 3);
    gsize len = 0;
    const guchar* png = g_bytes_get_data(g_array_index(doc->images, PreviewImage, 0).data, &len);
    g_assert_cmpuint(len, >, 8);
    g_assert_cmpint(memcmp(png, "\x89PNG", 4), ==, 0);
}

static void test_not_a_presentation(void) {
    GError* error = NULL;
    g_assert_null(pptx_file_preview("tests/fixtures/preview/budget.xlsx", "x", NULL, NULL, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_assert_null(pptx_read_deck("/nonexistent.pptx", &error));
    g_assert_nonnull(error);
    g_error_free(error);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/pptx/minimal", test_minimal);
    g_test_add_func("/pptx/inheritance", test_inheritance);
    g_test_add_func("/pptx/preview", test_preview);
    g_test_add_func("/pptx/not_a_presentation", test_not_a_presentation);
    return g_test_run();
}
