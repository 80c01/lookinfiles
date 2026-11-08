/* GIF decoder (see gif_decoder.h). */
#include "gif_decoder.h"
#include "core/i18n.h"

#include <gio/gio.h>
#include <string.h>

#define LZW_MAX_CODES 4096

/* Disposal methods of the Graphic Control Extension */
enum { DISPOSE_NONE = 0, DISPOSE_KEEP = 1, DISPOSE_BACKGROUND = 2, DISPOSE_PREVIOUS = 3 };

struct _GifDecoder {
    GBytes* bytes;
    const guint8* data;
    gsize len;
    gint width;
    gint height;
    guint8 global_colors[256 * 3];
    guint global_count;      /* 0: no global colour table */
    guint frame_count;
    guint play_count;
    gsize pos;               /* next block to read */
    gboolean done;
    guint8* canvas;          /* RGBA, width * height */
    guint8* saved;           /* canvas before a "restore to previous" frame */
    guint8* indices;         /* colour indices of the current frame */
    /* What the previous frame asked to do before the next one is drawn */
    gint prev_dispose;
    gint prev_x, prev_y, prev_w, prev_h;
};

static guint read_u16(const guint8* p) {
    return (guint)p[0] | ((guint)p[1] << 8);
}

/* Position after a chain of data sub-blocks starting at pos (len if the
 * chain is truncated) */
static gsize skip_sub_blocks(const guint8* data, gsize len, gsize pos) {
    while (pos < len) {
        guint size = data[pos];
        pos++;
        if (size == 0) {
            return pos;
        }
        if (size > len - pos) {
            return len;
        }
        pos += size;
    }
    return len;
}

/* Size of a colour table from a packed byte (bit 7: present, bits 0-2: size) */
static guint color_table_count(guint8 packed) {
    return (packed & 0x80) ? 2u << (packed & 0x07) : 0;
}

/* Application extension at pos (the byte after the 0xFF label): loop count */
static void read_application(GifDecoder* gif, gsize pos) {
    const guint8* d = gif->data;
    if (pos + 12 > gif->len || d[pos] != 11) {
        return;
    }
    if (memcmp(d + pos + 1, "NETSCAPE2.0", 11) != 0 && memcmp(d + pos + 1, "ANIMEXTS1.0", 11) != 0) {
        return;
    }
    pos += 12;
    if (pos + 4 <= gif->len && d[pos] >= 3 && d[pos + 1] == 1) {
        guint loops = read_u16(d + pos + 2);
        /* The extension counts repeats after the first play; 0 = forever */
        gif->play_count = loops == 0 ? 0 : loops + 1;
    }
}

/* Walk the blocks once: count the images, find the loop extension */
static void scan_blocks(GifDecoder* gif, gsize pos) {
    const guint8* d = gif->data;
    while (pos < gif->len) {
        guint8 type = d[pos++];
        if (type == 0x21) {
            if (pos >= gif->len) {
                return;
            }
            guint8 label = d[pos++];
            if (label == 0xFF) {
                read_application(gif, pos);
            }
            pos = skip_sub_blocks(d, gif->len, pos);
        } else if (type == 0x2C) {
            if (pos + 9 > gif->len) {
                return;
            }
            gsize table = color_table_count(d[pos + 8]) * 3u;
            pos += 9;
            if (table > gif->len - pos) {
                return;
            }
            pos += table + 1; /* LZW minimum code size */
            if (pos > gif->len) {
                return;
            }
            gif->frame_count++;
            pos = skip_sub_blocks(d, gif->len, pos);
        } else {
            return; /* trailer (0x3B) or garbage */
        }
    }
}

GifDecoder* gif_decoder_new(GBytes* data, GError** error) {
    g_return_val_if_fail(data != NULL, NULL);
    gsize len = 0;
    const guint8* d = g_bytes_get_data(data, &len);
    if (len < 13 || (memcmp(d, "GIF87a", 6) != 0 && memcmp(d, "GIF89a", 6) != 0)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("Not a GIF image"));
        return NULL;
    }
    gint width = (gint)read_u16(d + 6);
    gint height = (gint)read_u16(d + 8);
    if (width <= 0 || height <= 0 || (guint64)width * (guint64)height > GIF_MAX_PIXELS) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("Invalid GIF size %1$d x %2$d"), width, height);
        return NULL;
    }
    GifDecoder* gif = g_new0(GifDecoder, 1);
    gif->bytes = g_bytes_ref(data);
    gif->data = d;
    gif->len = len;
    gif->width = width;
    gif->height = height;
    gif->play_count = 1;
    gsize pos = 13;
    gif->global_count = color_table_count(d[10]);
    if (gif->global_count > 0) {
        gsize table = gif->global_count * 3u;
        if (table > len - pos) {
            table = len - pos; /* truncated: keep what is there, rest black */
        }
        memcpy(gif->global_colors, d + pos, table);
        pos += gif->global_count * 3u;
    }
    gif->pos = pos;
    if (pos < len) {
        scan_blocks(gif, pos);
    } else {
        gif->done = TRUE;
    }
    gsize pixels = (gsize)width * (gsize)height;
    gif->canvas = g_malloc0(pixels * 4);
    gif->indices = g_malloc(pixels);
    return gif;
}

void gif_decoder_free(GifDecoder* gif) {
    if (gif == NULL) {
        return;
    }
    g_bytes_unref(gif->bytes);
    g_free(gif->canvas);
    g_free(gif->saved);
    g_free(gif->indices);
    g_free(gif);
}

gint gif_decoder_get_width(GifDecoder* gif) {
    return gif->width;
}

gint gif_decoder_get_height(GifDecoder* gif) {
    return gif->height;
}

guint gif_decoder_get_frame_count(GifDecoder* gif) {
    return gif->frame_count;
}

guint gif_decoder_get_play_count(GifDecoder* gif) {
    return gif->play_count;
}

const guint8* gif_decoder_get_canvas(GifDecoder* gif) {
    return gif->canvas;
}

/* Copy the data sub-blocks at *pos into one buffer; *pos moves past them */
static GByteArray* collect_sub_blocks(GifDecoder* gif, gsize* pos) {
    GByteArray* out = g_byte_array_new();
    gsize p = *pos;
    while (p < gif->len) {
        guint size = gif->data[p++];
        if (size == 0) {
            break;
        }
        if (size > gif->len - p) {
            size = (guint)(gif->len - p);
        }
        g_byte_array_append(out, gif->data + p, size);
        p += size;
    }
    *pos = p;
    return out;
}

/* LZW decode of an image's data into out (at most out_len indices).
 * Returns how many indices were written (fewer on corrupt data). */
static gsize lzw_decode(const guint8* in, gsize in_len, guint min_size, guint8* out, gsize out_len) {
    if (min_size < 1 || min_size > 11) {
        return 0;
    }
    guint16 prefix[LZW_MAX_CODES];
    guint8 suffix[LZW_MAX_CODES];
    guint8 stack[LZW_MAX_CODES + 1];
    guint clear = 1u << min_size;
    guint eoi = clear + 1;
    guint next = clear + 2;
    guint size = min_size + 1;
    gint old = -1;
    guint8 first = 0;
    guint32 bits = 0;
    guint nbits = 0;
    gsize ip = 0;
    gsize op = 0;

    for (guint i = 0; i < clear; i++) {
        prefix[i] = 0;
        suffix[i] = (guint8)i;
    }
    while (op < out_len) {
        while (nbits < size && ip < in_len) {
            bits |= (guint32)in[ip++] << nbits;
            nbits += 8;
        }
        if (nbits < size) {
            break; /* out of data */
        }
        guint code = bits & ((1u << size) - 1);
        bits >>= size;
        nbits -= size;

        if (code == clear) {
            size = min_size + 1;
            next = clear + 2;
            old = -1;
            continue;
        }
        if (code == eoi) {
            break;
        }
        if (old < 0) {
            if (code >= clear) {
                break; /* the first code after a clear must be a literal */
            }
            out[op++] = (guint8)code;
            first = (guint8)code;
            old = (gint)code;
            continue;
        }
        if (code > next || (code == next && next >= LZW_MAX_CODES)) {
            break; /* invalid code */
        }
        guint sp = 0;
        guint cur = code;
        if (code == next) {
            stack[sp++] = first; /* KwKwK: string(old) + its first byte */
            cur = (guint)old;
        }
        while (cur >= clear) {
            stack[sp++] = suffix[cur];
            cur = prefix[cur];
        }
        first = (guint8)cur;
        stack[sp++] = first;
        while (sp > 0 && op < out_len) {
            out[op++] = stack[--sp];
        }
        if (next < LZW_MAX_CODES) {
            prefix[next] = (guint16)old;
            suffix[next] = first;
            next++;
            if (next == (1u << size) && size < 12) {
                size++;
            }
        }
        old = (gint)code;
    }
    return op;
}

/* Clip a frame rectangle to the canvas; FALSE if nothing is left */
static gboolean clip_rect(GifDecoder* gif, gint* x, gint* y, gint* w, gint* h) {
    gint x1 = MIN(*x + *w, gif->width);
    gint y1 = MIN(*y + *h, gif->height);
    if (*x >= gif->width || *y >= gif->height || x1 <= *x || y1 <= *y) {
        return FALSE;
    }
    *w = x1 - *x;
    *h = y1 - *y;
    return TRUE;
}

/* Apply what the previous frame asked to do with its area */
static void dispose_previous(GifDecoder* gif) {
    gint x = gif->prev_x, y = gif->prev_y, w = gif->prev_w, h = gif->prev_h;
    if (gif->prev_dispose == DISPOSE_BACKGROUND && clip_rect(gif, &x, &y, &w, &h)) {
        /* Background is transparent, as in browsers */
        for (gint row = y; row < y + h; row++) {
            memset(gif->canvas + ((gsize)row * gif->width + x) * 4, 0, (gsize)w * 4);
        }
    } else if (gif->prev_dispose == DISPOSE_PREVIOUS && gif->saved != NULL) {
        memcpy(gif->canvas, gif->saved, (gsize)gif->width * gif->height * 4);
    }
    gif->prev_dispose = DISPOSE_NONE;
}

/* Row of the image that the n-th decoded row goes to (interlaced order) */
static gint interlaced_row(gint n, gint height) {
    static const gint start[4] = { 0, 4, 2, 1 };
    static const gint step[4] = { 8, 8, 4, 2 };
    for (gint pass = 0; pass < 4; pass++) {
        gint rows = start[pass] < height ? (height - start[pass] + step[pass] - 1) / step[pass] : 0;
        if (n < rows) {
            return start[pass] + n * step[pass];
        }
        n -= rows;
    }
    return height - 1;
}

/* Decode the image whose descriptor is at gif->pos (after the 0x2C byte)
 * and draw it. FALSE if the data ends before the image. */
static gboolean draw_image(GifDecoder* gif, gint dispose, gint transparent) {
    const guint8* d = gif->data;
    gsize pos = gif->pos;
    if (pos + 9 > gif->len) {
        return FALSE;
    }
    gint fx = (gint)read_u16(d + pos);
    gint fy = (gint)read_u16(d + pos + 2);
    gint fw = (gint)read_u16(d + pos + 4);
    gint fh = (gint)read_u16(d + pos + 6);
    guint8 packed = d[pos + 8];
    gboolean interlaced = (packed & 0x40) != 0;
    pos += 9;

    guint8 local_colors[256 * 3];
    const guint8* colors = gif->global_colors;
    guint count = gif->global_count;
    guint local_count = color_table_count(packed);
    if (local_count > 0) {
        gsize table = local_count * 3u;
        if (table > gif->len - pos) {
            return FALSE;
        }
        memcpy(local_colors, d + pos, table);
        pos += table;
        colors = local_colors;
        count = local_count;
    }
    if (pos >= gif->len) {
        return FALSE;
    }
    guint min_size = d[pos++];
    GByteArray* lzw = collect_sub_blocks(gif, &pos);
    gif->pos = pos;

    /* The previous frame's disposal, then save the canvas if this one
     * restores it afterwards */
    dispose_previous(gif);
    gsize canvas_bytes = (gsize)gif->width * gif->height * 4;
    if (dispose == DISPOSE_PREVIOUS) {
        if (gif->saved == NULL) {
            gif->saved = g_malloc(canvas_bytes);
        }
        memcpy(gif->saved, gif->canvas, canvas_bytes);
    }
    gif->prev_dispose = dispose;
    gif->prev_x = fx;
    gif->prev_y = fy;
    gif->prev_w = fw;
    gif->prev_h = fh;

    /* Frame pixels: decoded at full frame size, drawn clipped. The index
     * buffer holds the canvas size, larger frames are clipped at decode. */
    gsize frame_pixels = (gsize)fw * (gsize)fh;
    gsize max_pixels = (gsize)gif->width * gif->height;
    gsize wanted = MIN(frame_pixels, max_pixels);
    gsize decoded = lzw_decode(lzw->data, lzw->len, min_size, gif->indices, wanted);
    g_byte_array_unref(lzw);
    if (fw == 0) {
        return TRUE;
    }

    gint rows = (gint)(decoded / (gsize)fw);
    for (gint n = 0; n < rows; n++) {
        gint row = interlaced ? interlaced_row(n, fh) : n;
        gint cy = fy + row;
        if (cy >= gif->height) {
            continue;
        }
        const guint8* src = gif->indices + (gsize)n * fw;
        guint8* dst = gif->canvas + ((gsize)cy * gif->width) * 4;
        for (gint col = 0; col < fw; col++) {
            gint cx = fx + col;
            if (cx >= gif->width) {
                break;
            }
            guint idx = src[col];
            if ((gint)idx == transparent || idx >= count) {
                continue;
            }
            guint8* px = dst + (gsize)cx * 4;
            px[0] = colors[idx * 3];
            px[1] = colors[idx * 3 + 1];
            px[2] = colors[idx * 3 + 2];
            px[3] = 255;
        }
    }
    return TRUE;
}

gboolean gif_decoder_next_frame(GifDecoder* gif, guint* delay_ms) {
    const guint8* d = gif->data;
    gint dispose = DISPOSE_NONE;
    gint transparent = -1;
    guint delay = 0;
    while (!gif->done && gif->pos < gif->len) {
        guint8 type = d[gif->pos++];
        if (type == 0x21) {
            if (gif->pos >= gif->len) {
                break;
            }
            guint8 label = d[gif->pos++];
            gsize p = gif->pos;
            if (label == 0xF9 && p + 5 <= gif->len && d[p] >= 4) {
                guint8 packed = d[p + 1];
                dispose = (packed >> 2) & 0x07;
                delay = read_u16(d + p + 2) * 10u;
                transparent = (packed & 0x01) ? d[p + 4] : -1;
            }
            gif->pos = skip_sub_blocks(d, gif->len, p);
        } else if (type == 0x2C) {
            if (!draw_image(gif, dispose, transparent)) {
                break;
            }
            if (delay_ms != NULL) {
                *delay_ms = delay <= 10 ? 100 : delay;
            }
            return TRUE;
        } else {
            break; /* trailer (0x3B) or garbage */
        }
    }
    gif->done = TRUE;
    return FALSE;
}
