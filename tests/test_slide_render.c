/* Unit tests for src/slide_render.c: slides drawn as pictures, deck previews. */
#include "slide_render.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string.h>

/* Colour of a pixel of a PNG */
static guint32 pixel(GBytes* png, gint x, gint y, gint* width) {
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    g_assert_true(gdk_pixbuf_loader_write(loader, g_bytes_get_data(png, NULL), g_bytes_get_size(png), NULL));
    g_assert_true(gdk_pixbuf_loader_close(loader, NULL));
    GdkPixbuf* pix = gdk_pixbuf_loader_get_pixbuf(loader);
    if (width != NULL) *width = gdk_pixbuf_get_width(pix);
    const guchar* p = gdk_pixbuf_read_pixels(pix) + y * gdk_pixbuf_get_rowstride(pix) + x * gdk_pixbuf_get_n_channels(pix);
    guint32 rgb = ((guint32)p[0] << 16) | ((guint32)p[1] << 8) | p[2];
    g_object_unref(loader);
    return rgb;
}

static SlideShape* box(gdouble x, gdouble y, gdouble w, gdouble h, guint32 fill) {
    SlideShape* s = slide_shape_new();
    s->x = x;
    s->y = y;
    s->w = w;
    s->h = h;
    s->fill = fill;
    return s;
}

static void test_draw(void) {
    g_autoptr(SlideDeck) deck = slide_deck_new(720, 405);
    Slide* slide = slide_new();
    slide->background = 0x102030;
    g_ptr_array_add(slide->shapes, box(0, 0, 360, 405, 0xC00000));
    SlideShape* round = box(400, 50, 200, 100, 0x00A000);
    round->geometry = SLIDE_GEOM_ELLIPSE;
    g_ptr_array_add(slide->shapes, round);
    SlideShape* text = box(400, 250, 300, 100, SLIDE_NO_COLOR);
    SlideParagraph* p = slide_paragraph_new();
    SlideRun* r = slide_paragraph_add_run(p, "Hello", -1);
    r->size = 40;
    r->color = 0xFFFFFF;
    p->bullet = g_strdup("•");
    p->indent = -20;
    p->left = 20;
    g_ptr_array_add(text->paragraphs, p);
    g_ptr_array_add(slide->shapes, text);
    g_ptr_array_add(deck->slides, slide);
    gchar* all = slide_shape_text(text);
    g_assert_cmpstr(all, ==, "Hello");
    g_free(all);

    GBytes* png = slide_render_png(deck, slide, 720);
    g_assert_nonnull(png);
    gint width = 0;
    g_assert_cmphex(pixel(png, 100, 100, &width), ==, 0xC00000);
    g_assert_cmpint(width, ==, 720);
    g_assert_cmphex(pixel(png, 500, 100, NULL), ==, 0x00A000);  /* ellipse centre */
    g_assert_cmphex(pixel(png, 402, 52, NULL), ==, 0x102030);   /* outside the ellipse */
    g_assert_cmphex(pixel(png, 700, 380, NULL), ==, 0x102030);
    g_bytes_unref(png);
    /* Half the size: the same picture scaled */
    png = slide_render_png(deck, slide, 360);
    g_assert_cmphex(pixel(png, 250, 50, &width), ==, 0x00A000);
    g_assert_cmpint(width, ==, 360);
    g_bytes_unref(png);
}

static void test_table(void) {
    g_autoptr(SlideDeck) deck = slide_deck_new(720, 405);
    Slide* slide = slide_new();
    SlideShape* s = box(100, 100, 400, 100, SLIDE_NO_COLOR);
    s->table = slide_table_new(2, 2);
    for (guint i = 0; i < 2; i++) {
        s->table->col_widths[i] = 200;
        s->table->row_heights[i] = 50;
    }
    slide_table_cell(s->table, 0, 0)->fill = 0x0000C0;
    slide_table_cell(s->table, 0, 0)->col_span = 2;
    slide_table_cell(s->table, 0, 1)->merged = TRUE;
    SlideParagraph* p = slide_paragraph_new();
    slide_paragraph_add_run(p, "cell", -1);
    g_ptr_array_add(slide_table_cell(s->table, 1, 1)->paragraphs, p);
    g_ptr_array_add(slide->shapes, s);
    g_ptr_array_add(deck->slides, slide);
    gchar* text = slide_shape_text(s);
    g_assert_cmpstr(text, ==, "cell");
    g_free(text);
    GBytes* png = slide_render_png(deck, slide, 720);
    g_assert_cmphex(pixel(png, 450, 120, NULL), ==, 0x0000C0); /* spanned header */
    g_assert_cmphex(pixel(png, 150, 175, NULL), ==, 0xFFFFFF);
    g_bytes_unref(png);
}

static void test_preview(void) {
    g_autoptr(SlideDeck) deck = slide_deck_new(0, 0);
    g_assert_cmpfloat(deck->width, ==, 720);
    for (gint i = 0; i < 3; i++) {
        Slide* s = slide_new();
        s->title = g_strdup_printf("Part\n%d", i + 1);
        if (i == 0) s->notes = g_strdup("Say hello");
        g_ptr_array_add(deck->slides, s);
    }
    g_autoptr(PreviewDoc) doc = slide_deck_preview(deck, "talk.pptx", "3 slides", 2);
    g_assert_cmpuint(doc->images->len, ==, 2);
    g_assert_nonnull(strstr(doc->text->str, "Slide 1 · Part 1\n" PREVIEW_DOC_IMAGE_CHAR "\nSay hello\n"));
    g_assert_nonnull(strstr(doc->text->str, "The first 2 of 3 slides are drawn"));
    g_assert_true(g_str_has_suffix(doc->text->str, "Slide 3 · Part 3\n"));

    g_autoptr(SlideDeck) empty = slide_deck_new(720, 540);
    g_autoptr(PreviewDoc) none = slide_deck_preview(empty, "e", NULL, 20);
    g_assert_nonnull(strstr(none->text->str, "(no slides)"));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/slide_render/draw", test_draw);
    g_test_add_func("/slide_render/table", test_table);
    g_test_add_func("/slide_render/preview", test_preview);
    return g_test_run();
}
