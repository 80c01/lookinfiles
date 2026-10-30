#ifndef GIF_DECODER_H
#define GIF_DECODER_H

/* GIF (87a / 89a) decoder: frames composed one at a time on an RGBA canvas
 * (LZW, interlacing, local colour tables, transparency, disposal methods,
 * frame delays, NETSCAPE loop count). Replaces GdkPixbufAnimation,
 * deprecated in gdk-pixbuf 2.44. Pure GLib, no widgets: usable from a
 * worker thread. Truncated or corrupt data ends the animation early; it
 * never reads outside the buffer. */
#include <glib.h>

/* Largest canvas accepted, in pixels */
#define GIF_MAX_PIXELS (64u * 1024 * 1024)

typedef struct _GifDecoder GifDecoder;

/* Decoder over a GIF file in memory (keeps a reference to data). NULL with
 * error if it is not a GIF or its size is invalid. */
GifDecoder* gif_decoder_new(GBytes* data, GError** error);
void gif_decoder_free(GifDecoder* gif);

/* Canvas (logical screen) size */
gint gif_decoder_get_width(GifDecoder* gif);
gint gif_decoder_get_height(GifDecoder* gif);

/* Number of images in the file (counted when the decoder is created) */
guint gif_decoder_get_frame_count(GifDecoder* gif);

/* How many times the animation is played: 0 = forever, else >= 1 (1 when
 * the file has no loop extension) */
guint gif_decoder_get_play_count(GifDecoder* gif);

/* Compose the next frame on the canvas. delay_ms (may be NULL) receives how
 * long it is shown (browser rule: 10 ms or less is shown 100 ms). FALSE when
 * there is no other frame. */
gboolean gif_decoder_next_frame(GifDecoder* gif, guint* delay_ms);

/* Canvas after the last gif_decoder_next_frame(): width * height pixels of
 * R, G, B, A bytes (not premultiplied), rows of width * 4 bytes. Owned by
 * the decoder, changed by the next call. */
const guint8* gif_decoder_get_canvas(GifDecoder* gif);

#endif /* GIF_DECODER_H */
