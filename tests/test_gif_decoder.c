/* Unit tests for src/gif_decoder.c: LZW, composition, disposal, loops. */
#include "gif_decoder.h"

#include <string.h>

/* --- Minimal GIF writer: palette of 4 colours, literal LZW codes --- */

static const guint8 PALETTE[4 * 3] = {
    0, 0, 0,       /* 0 black */
    255, 0, 0,     /* 1 red */
    0, 255, 0,     /* 2 green */
    0, 0, 255,     /* 3 blue */
};

typedef struct {
    GByteArray* out;
    guint32 bits;
    guint nbits;
    GByteArray* block;
} BitWriter;

static void put_code(BitWriter* w, guint code, guint size) {
    w->bits |= (guint32)code << w->nbits;
    w->nbits += size;
    while (w->nbits >= 8) {
        guint8 b = (guint8)(w->bits & 0xFF);
        g_byte_array_append(w->block, &b, 1);
        w->bits >>= 8;
        w->nbits -= 8;
    }
}

/* LZW stream of indices using literal codes only, mirroring the decoder's
 * table growth so the code size stays in step */
static GByteArray* lzw_literals(const guint8* idx, gsize n, guint min_size) {
    BitWriter w = { NULL, 0, 0, g_byte_array_new() };
    guint clear = 1u << min_size;
    guint size = min_size + 1;
    guint next = clear + 2;
    gboolean have_old = FALSE;
    put_code(&w, clear, size);
    for (gsize i = 0; i < n; i++) {
        if (next >= 4000) {
            put_code(&w, clear, size);
            size = min_size + 1;
            next = clear + 2;
            have_old = FALSE;
        }
        put_code(&w, idx[i], size);
        if (have_old && next < 4096) {
            next++;
            if (next == (1u << size) && size < 12) {
                size++;
            }
        }
        have_old = TRUE;
    }
    put_code(&w, clear + 1, size);
    if (w.nbits > 0) {
        guint8 b = (guint8)w.bits;
        g_byte_array_append(w.block, &b, 1);
    }
    return w.block;
}

static void put_u16(GByteArray* a, guint v) {
    guint8 b[2] = { (guint8)(v & 0xFF), (guint8)(v >> 8) };
    g_byte_array_append(a, b, 2);
}

static void put_byte(GByteArray* a, guint v) {
    guint8 b = (guint8)v;
    g_byte_array_append(a, &b, 1);
}

static GByteArray* gif_begin(guint width, guint height, gint loops) {
    GByteArray* a = g_byte_array_new();
    g_byte_array_append(a, (const guint8*)"GIF89a", 6);
    put_u16(a, width);
    put_u16(a, height);
    put_byte(a, 0x80 | 0x01); /* global table of 4 colours */
    put_byte(a, 0);
    put_byte(a, 0);
    g_byte_array_append(a, PALETTE, sizeof(PALETTE));
    if (loops >= 0) {
        put_byte(a, 0x21);
        put_byte(a, 0xFF);
        put_byte(a, 11);
        g_byte_array_append(a, (const guint8*)"NETSCAPE2.0", 11);
        put_byte(a, 3);
        put_byte(a, 1);
        put_u16(a, (guint)loops);
        put_byte(a, 0);
    }
    return a;
}

/* One frame: Graphic Control Extension + image of w x h indices at x, y */
static void gif_frame(GByteArray* a, guint x, guint y, guint w, guint h, const guint8* idx,
                      guint dispose, gint transparent, guint delay_cs) {
    put_byte(a, 0x21);
    put_byte(a, 0xF9);
    put_byte(a, 4);
    put_byte(a, (dispose << 2) | (transparent >= 0 ? 1 : 0));
    put_u16(a, delay_cs);
    put_byte(a, transparent >= 0 ? (guint)transparent : 0);
    put_byte(a, 0);

    put_byte(a, 0x2C);
    put_u16(a, x);
    put_u16(a, y);
    put_u16(a, w);
    put_u16(a, h);
    put_byte(a, 0);
    put_byte(a, 2); /* minimum code size */
    GByteArray* lzw = lzw_literals(idx, (gsize)w * h, 2);
    for (guint off = 0; off < lzw->len; off += 255) {
        guint n = MIN(255u, lzw->len - off);
        put_byte(a, n);
        g_byte_array_append(a, lzw->data + off, n);
    }
    put_byte(a, 0);
    g_byte_array_unref(lzw);
}

static GifDecoder* gif_end(GByteArray* a) {
    put_byte(a, 0x3B);
    GBytes* bytes = g_byte_array_free_to_bytes(a);
    GError* err = NULL;
    GifDecoder* gif = gif_decoder_new(bytes, &err);
    g_assert_no_error(err);
    g_assert_nonnull(gif);
    g_bytes_unref(bytes);
    return gif;
}

/* Colour index drawn at x, y: 0..3, or -1 if transparent */
static gint pixel_at(GifDecoder* gif, gint x, gint y) {
    const guint8* p = gif_decoder_get_canvas(gif) + ((gsize)y * gif_decoder_get_width(gif) + x) * 4;
    if (p[3] == 0) {
        return -1;
    }
    g_assert_cmpuint(p[3], ==, 255);
    for (gint i = 0; i < 4; i++) {
        if (memcmp(p, PALETTE + i * 3, 3) == 0) {
            return i;
        }
    }
    g_assert_not_reached();
    return -2;
}

/* --- Tests --- */

static void test_not_a_gif(void) {
    GBytes* bytes = g_bytes_new_static("\x89PNG\r\n\x1a\nxxxxxxxxxx", 18);
    GError* err = NULL;
    g_assert_null(gif_decoder_new(bytes, &err));
    g_assert_nonnull(err);
    g_clear_error(&err);
    g_bytes_unref(bytes);

    /* Zero-sized canvas */
    GBytes* empty = g_bytes_new_static("GIF89a\0\0\0\0\0\0\0;", 14);
    g_assert_null(gif_decoder_new(empty, &err));
    g_clear_error(&err);
    g_bytes_unref(empty);
}

static void test_single_frame(void) {
    guint8 idx[6] = { 0, 1, 2, 3, 2, 1 };
    GByteArray* a = gif_begin(3, 2, -1);
    gif_frame(a, 0, 0, 3, 2, idx, 0, -1, 0);
    GifDecoder* gif = gif_end(a);
    g_assert_cmpint(gif_decoder_get_width(gif), ==, 3);
    g_assert_cmpint(gif_decoder_get_height(gif), ==, 2);
    g_assert_cmpuint(gif_decoder_get_frame_count(gif), ==, 1);
    g_assert_cmpuint(gif_decoder_get_play_count(gif), ==, 1);
    guint delay = 0;
    g_assert_true(gif_decoder_next_frame(gif, &delay));
    g_assert_cmpuint(delay, ==, 100); /* 0 cs is shown 100 ms */
    for (gint i = 0; i < 6; i++) {
        g_assert_cmpint(pixel_at(gif, i % 3, i / 3), ==, idx[i]);
    }
    g_assert_false(gif_decoder_next_frame(gif, &delay));
    g_assert_false(gif_decoder_next_frame(gif, &delay));
    gif_decoder_free(gif);
}

/* Large frame: the code size grows to 12 bits and the table is cleared */
static void test_long_stream(void) {
    guint w = 100, h = 90;
    guint8* idx = g_malloc((gsize)w * h);
    for (guint i = 0; i < w * h; i++) {
        idx[i] = (guint8)((i * 7 + i / 13) % 4);
    }
    GByteArray* a = gif_begin(w, h, -1);
    gif_frame(a, 0, 0, w, h, idx, 0, -1, 4);
    GifDecoder* gif = gif_end(a);
    guint delay = 0;
    g_assert_true(gif_decoder_next_frame(gif, &delay));
    g_assert_cmpuint(delay, ==, 40);
    for (guint i = 0; i < w * h; i++) {
        g_assert_cmpint(pixel_at(gif, (gint)(i % w), (gint)(i / w)), ==, idx[i]);
    }
    gif_decoder_free(gif);
    g_free(idx);
}

/* Disposal methods and transparency over a 4 x 1 canvas */
static void test_disposal(void) {
    guint8 full[4] = { 1, 1, 1, 1 };   /* red everywhere */
    guint8 two_green[2] = { 2, 2 };
    guint8 blue_hole[2] = { 3, 0 };    /* 0 is transparent in that frame */
    GByteArray* a = gif_begin(4, 1, 0);
    gif_frame(a, 0, 0, 4, 1, full, 1, -1, 5);          /* keep */
    gif_frame(a, 1, 0, 2, 1, two_green, 3, -1, 5);     /* then restore previous */
    gif_frame(a, 2, 0, 2, 1, blue_hole, 2, 0, 5);      /* then clear its area */
    gif_frame(a, 0, 0, 1, 1, two_green, 0, -1, 5);
    GifDecoder* gif = gif_end(a);
    g_assert_cmpuint(gif_decoder_get_frame_count(gif), ==, 4);
    g_assert_cmpuint(gif_decoder_get_play_count(gif), ==, 0); /* forever */

    g_assert_true(gif_decoder_next_frame(gif, NULL));
    for (gint x = 0; x < 4; x++) {
        g_assert_cmpint(pixel_at(gif, x, 0), ==, 1);
    }
    g_assert_true(gif_decoder_next_frame(gif, NULL));
    gint f2[4] = { 1, 2, 2, 1 };
    for (gint x = 0; x < 4; x++) {
        g_assert_cmpint(pixel_at(gif, x, 0), ==, f2[x]);
    }
    /* Frame 2 restored: red again, then blue at 2, transparent pixel keeps red */
    g_assert_true(gif_decoder_next_frame(gif, NULL));
    gint f3[4] = { 1, 1, 3, 1 };
    for (gint x = 0; x < 4; x++) {
        g_assert_cmpint(pixel_at(gif, x, 0), ==, f3[x]);
    }
    /* Frame 3's area cleared to transparent, green drawn at 0 */
    g_assert_true(gif_decoder_next_frame(gif, NULL));
    gint f4[4] = { 2, 1, -1, -1 };
    for (gint x = 0; x < 4; x++) {
        g_assert_cmpint(pixel_at(gif, x, 0), ==, f4[x]);
    }
    g_assert_false(gif_decoder_next_frame(gif, NULL));
    gif_decoder_free(gif);
}

/* Frames partly outside the canvas are clipped */
static void test_clipping(void) {
    guint8 idx[9] = { 3, 3, 3, 3, 3, 3, 3, 3, 3 };
    GByteArray* a = gif_begin(4, 4, 1);
    gif_frame(a, 2, 2, 3, 3, idx, 0, -1, 1);
    gif_frame(a, 9, 9, 3, 3, idx, 0, -1, 1);
    GifDecoder* gif = gif_end(a);
    g_assert_cmpuint(gif_decoder_get_play_count(gif), ==, 2);
    g_assert_true(gif_decoder_next_frame(gif, NULL));
    for (gint y = 0; y < 4; y++) {
        for (gint x = 0; x < 4; x++) {
            g_assert_cmpint(pixel_at(gif, x, y), ==, (x >= 2 && y >= 2) ? 3 : -1);
        }
    }
    g_assert_true(gif_decoder_next_frame(gif, NULL)); /* fully outside: no change */
    g_assert_cmpint(pixel_at(gif, 3, 3), ==, 3);
    gif_decoder_free(gif);
}

/* Every truncation of a valid file decodes without reading past the end */
static void test_truncated(void) {
    guint8 idx[64];
    for (guint i = 0; i < 64; i++) {
        idx[i] = (guint8)(i % 4);
    }
    GByteArray* a = gif_begin(8, 8, 0);
    gif_frame(a, 0, 0, 8, 8, idx, 2, 1, 3);
    gif_frame(a, 0, 0, 8, 8, idx, 3, -1, 3);
    put_byte(a, 0x3B);
    for (guint len = 0; len < a->len; len++) {
        /* A copy of exactly len bytes, so overreads show under valgrind / ASan */
        GBytes* bytes = g_bytes_new(a->data, len);
        GifDecoder* gif = gif_decoder_new(bytes, NULL);
        if (gif != NULL) {
            guint frames = 0;
            while (gif_decoder_next_frame(gif, NULL)) {
                frames++;
            }
            g_assert_cmpuint(frames, <=, 2);
            gif_decoder_free(gif);
        }
        g_bytes_unref(bytes);
    }
    g_byte_array_unref(a);
}

/* Garbage after a valid header never crashes */
static void test_garbage(void) {
    GRand* rand = g_rand_new_with_seed(42);
    for (guint round = 0; round < 300; round++) {
        GByteArray* a = gif_begin(16, 16, -1);
        guint n = g_rand_int_range(rand, 1, 400);
        for (guint i = 0; i < n; i++) {
            /* Bias towards block introducers so frames get parsed */
            guint r = g_rand_int_range(rand, 0, 10);
            put_byte(a, r == 0 ? 0x2C : r == 1 ? 0x21 : g_rand_int_range(rand, 0, 256));
        }
        GBytes* bytes = g_byte_array_free_to_bytes(a);
        GifDecoder* gif = gif_decoder_new(bytes, NULL);
        g_assert_nonnull(gif);
        guint guard = 0;
        while (gif_decoder_next_frame(gif, NULL) && guard < 1000) {
            guard++;
        }
        g_assert_cmpuint(guard, <, 1000);
        gif_decoder_free(gif);
        g_bytes_unref(bytes);
    }
    g_rand_free(rand);
}

/* Compressed, interlaced, optimized (sub-rectangle and transparent) frames
 * written by ImageMagick, compared with its own coalesced frames:
 * magick layers.gif -coalesce rgba:-, transparent pixels zeroed, SHA-256 */
static void test_imagemagick_fixture(void) {
    static const gchar* expected[4] = {
        "a21a0ef154a6cff3a49af77efefe7c16b67efdc52f1e976d06f4e43aac1fcddb",
        "2c3c9762d25a6e8e2b79a572ca08782a28af0bc47eb4bcce4137ddcb73a87066",
        "77989910db5ee891dfde39081b0bb233c5148e4f9e0dd8269c47ebfce0c5dc73",
        "75613b18253362eac4d569b6b67f4ef20b20f1e793afb099077a0b276d78be58",
    };
    gchar* contents = NULL;
    gsize len = 0;
    g_assert_true(g_file_get_contents("tests/fixtures/gif/layers.gif", &contents, &len, NULL));
    GBytes* bytes = g_bytes_new_take(contents, len);
    GifDecoder* gif = gif_decoder_new(bytes, NULL);
    g_bytes_unref(bytes);
    g_assert_nonnull(gif);
    g_assert_cmpint(gif_decoder_get_width(gif), ==, 48);
    g_assert_cmpint(gif_decoder_get_height(gif), ==, 32);
    g_assert_cmpuint(gif_decoder_get_frame_count(gif), ==, 4);
    g_assert_cmpuint(gif_decoder_get_play_count(gif), ==, 2); /* -loop 2: one repeat stored */
    gsize size = 48 * 32 * 4;
    guint8* frame = g_malloc(size);
    for (guint i = 0; i < 4; i++) {
        guint delay = 0;
        g_assert_true(gif_decoder_next_frame(gif, &delay));
        g_assert_cmpuint(delay, ==, 70);
        memcpy(frame, gif_decoder_get_canvas(gif), size);
        for (gsize p = 0; p < size; p += 4) {
            if (frame[p + 3] == 0) {
                memset(frame + p, 0, 3);
            }
        }
        gchar* sum = g_compute_checksum_for_data(G_CHECKSUM_SHA256, frame, size);
        g_assert_cmpstr(sum, ==, expected[i]);
        g_free(sum);
    }
    g_assert_false(gif_decoder_next_frame(gif, NULL));
    g_free(frame);
    gif_decoder_free(gif);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/gif/not_a_gif", test_not_a_gif);
    g_test_add_func("/gif/single_frame", test_single_frame);
    g_test_add_func("/gif/long_stream", test_long_stream);
    g_test_add_func("/gif/disposal", test_disposal);
    g_test_add_func("/gif/clipping", test_clipping);
    g_test_add_func("/gif/truncated", test_truncated);
    g_test_add_func("/gif/garbage", test_garbage);
    g_test_add_func("/gif/imagemagick_fixture", test_imagemagick_fixture);
    return g_test_run();
}
