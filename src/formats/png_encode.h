/* PNG encoding of an RGB picture, fast: the preview draws pictures (slides)
 * in a worker thread and hands them to the view as PNG; cairo's own writer
 * compresses at zlib's default level, which took two thirds of the preview
 * of a presentation (docs/performance.md). Pure GLib / GIO (zlib through
 * GZlibCompressor), unit-tested. */
#ifndef PNG_ENCODE_H
#define PNG_ENCODE_H

#include <glib.h>

/* Level for pictures that only live until they are shown: fast, a bit larger */
#define PNG_ENCODE_FAST 1

/* PNG (8-bit RGB) of width x height pixels in cairo's RGB24 layout: rows of
 * 32-bit native-endian 0x00RRGGBB words, stride bytes apart; level is zlib's
 * (0: stored, 1: fastest, 9: smallest). NULL if the size is invalid. */
GBytes* png_encode_xrgb32(const guint8* data, gint width, gint height, gint stride, gint level);

#endif
