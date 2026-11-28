/* Unit tests for src/office/slide_render.c: slides drawn as pictures, deck previews. */
#include "office/slide_render.h"

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

/* A PNG w x 1, the first half red, the other green */
static GBytes* stripes(gint w) {
    GdkPixbuf* pix = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, w, 1);
    guchar* px = gdk_pixbuf_get_pixels(pix);
    for (gint x = 0; x < w; x++) {
        px[x * 3] = x < w / 2 ? 0xFF : 0;
        px[x * 3 + 1] = x < w / 2 ? 0 : 0xFF;
        px[x * 3 + 2] = 0;
    }
    gchar* buf = NULL;
    gsize len = 0;
    g_assert_true(gdk_pixbuf_save_to_buffer(pix, &buf, &len, "png", NULL, NULL));
    g_object_unref(pix);
    return g_bytes_new_take(buf, len);
}

static gboolean near(guint32 a, guint32 b) {
    for (gint shift = 0; shift < 24; shift += 8) {
        gint d = (gint)((a >> shift) & 0xFF) - (gint)((b >> shift) & 0xFF);
        if (d < -2 || d > 2) return FALSE;
    }
    return TRUE;
}

static void test_effects(void) {
    g_autoptr(SlideDeck) deck = slide_deck_new(100, 100);
    Slide* slide = slide_new();
    /* Half transparent fill */
    SlideShape* glass = box(0, 0, 20, 20, 0xFF0000);
    glass->fill_opacity = 0.5;
    g_ptr_array_add(slide->shapes, glass);
    /* Shadow below and right of the shape */
    SlideShape* raised = box(30, 0, 20, 20, 0x000000);
    raised->shadow = 0x0000FF;
    raised->shadow_dx = raised->shadow_dy = 5;
    raised->shadow_opacity = 1;
    g_ptr_array_add(slide->shapes, raised);
    /* Repeated picture, and a picture clipped to an ellipse */
    SlideShape* tiles = box(0, 40, 60, 10, SLIDE_NO_COLOR);
    tiles->picture = stripes(4);
    tiles->picture_tile_w = 20;
    tiles->picture_tile_h = 10;
    g_ptr_array_add(slide->shapes, tiles);
    SlideShape* oval = box(70, 30, 30, 30, SLIDE_NO_COLOR);
    oval->picture = stripes(2);
    oval->geometry = SLIDE_GEOM_ELLIPSE;
    g_ptr_array_add(slide->shapes, oval);
    /* Text made smaller to stay in its box */
    SlideShape* text = box(0, 55, 100, 28, SLIDE_NO_COLOR);
    text->shrink_to_fit = TRUE;
    SlideParagraph* p = slide_paragraph_new();
    SlideRun* r = slide_paragraph_add_run(p, "Too many words here", -1);
    r->size = 30;
    g_ptr_array_add(text->paragraphs, p);
    g_ptr_array_add(slide->shapes, text);
    g_ptr_array_add(deck->slides, slide);

    GBytes* png = slide_render_png(deck, slide, 100);
    g_assert_true(near(pixel(png, 10, 10, NULL), 0xFF8080));
    g_assert_cmphex(pixel(png, 40, 10, NULL), ==, 0x000000);
    g_assert_cmphex(pixel(png, 52, 22, NULL), ==, 0x0000FF);
    g_assert_cmphex(pixel(png, 3, 45, NULL), ==, 0xFF0000);
    g_assert_cmphex(pixel(png, 13, 45, NULL), ==, 0x00FF00);
    g_assert_cmphex(pixel(png, 23, 45, NULL), ==, 0xFF0000);
    g_assert_cmphex(pixel(png, 71, 31, NULL), ==, 0xFFFFFF);  /* outside the ellipse */
    for (gint y = 86; y < 100; y++) {
        for (gint x = 0; x < 100; x += 3) {
            g_assert_cmphex(pixel(png, x, y, NULL), ==, 0xFFFFFF);
        }
    }
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
    g_autoptr(PreviewDoc) doc = slide_deck_preview(deck, "talk.pptx", "3 slides", 2, 1.0);
    g_assert_cmpuint(doc->images->len, ==, 2);
    g_assert_nonnull(strstr(doc->text->str, "Slide 1 · Part 1\n" PREVIEW_DOC_IMAGE_CHAR "\nSay hello\n"));
    g_assert_nonnull(strstr(doc->text->str, "The first 2 of 3 slides are drawn"));
    g_assert_true(g_str_has_suffix(doc->text->str, "Slide 3 · Part 3\n"));
    g_assert_cmpuint(doc->parts_drawn, ==, 2);
    g_assert_cmpuint(doc->parts, ==, 3);
    /* Load Whole File: every slide drawn */
    g_autoptr(PreviewDoc) all = slide_deck_preview(deck, "talk.pptx", "3 slides", G_MAXUINT, 1.0);
    g_assert_cmpuint(all->images->len, ==, 3);
    g_assert_cmpuint(all->parts_drawn, ==, all->parts);
    g_assert_null(strstr(all->text->str, "slides are drawn"));

    g_autoptr(SlideDeck) empty = slide_deck_new(720, 540);
    g_autoptr(PreviewDoc) none = slide_deck_preview(empty, "e", NULL, 20, 1.0);
    g_assert_nonnull(strstr(none->text->str, "(no slides)"));
}

/* Zoomed slides: drawn larger, the same text */
static void test_preview_zoom(void) {
    g_autoptr(SlideDeck) deck = slide_deck_new(720, 405);
    g_ptr_array_add(deck->slides, slide_new());
    g_autoptr(PreviewDoc) fitted = slide_deck_preview(deck, "t", NULL, 20, 0);
    g_autoptr(PreviewDoc) zoomed = slide_deck_preview(deck, "t", NULL, 20, 2.0);
    g_autoptr(PreviewDoc) capped = slide_deck_preview(deck, "t", NULL, 20, 8.0);
    g_assert_cmpfloat(fitted->image_zoom, ==, 1.0);
    g_assert_cmpfloat(zoomed->image_zoom, ==, 2.0);
    g_assert_cmpfloat(capped->image_zoom, ==, PREVIEW_DOC_IMAGE_ZOOM_MAX);
    gint w1 = g_array_index(fitted->images, PreviewImage, 0).max_width;
    g_assert_cmpint(g_array_index(zoomed->images, PreviewImage, 0).max_width, ==, 2 * w1);
    g_assert_cmpstr(zoomed->text->str, ==, fitted->text->str);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/slide_render/draw", test_draw);
    g_test_add_func("/slide_render/effects", test_effects);
    g_test_add_func("/slide_render/table", test_table);
    g_test_add_func("/slide_render/preview", test_preview);
    g_test_add_func("/slide_render/preview_zoom", test_preview_zoom);
    return g_test_run();
}
