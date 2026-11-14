#ifndef JPEG_DECODE_H
#define JPEG_DECODE_H

/* Progressive JPEG files decoded straight at the size they are shown, with
 * libjpeg (HAVE_LIBJPEG): the decoder scales the image down by M/8 while
 * decoding, fast DCT and plain upsampling (the image is scaled down after
 * anyway); then a bilinear scale to the exact size. gdk-pixbuf's loader
 * spends a third more on these (it redraws every scan); on baseline JPEGs
 * it is as fast, so those are left to it. NULL for what is left to
 * gdk-pixbuf: baseline JPEGs, CMYK colours, not a JPEG, no libjpeg. */
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>

/* The progressive JPEG file at path decoded to width x height (RGB), or
 * NULL (baseline, not a JPEG, unsupported, unreadable, or cancelled). A
 * damaged file decodes as far as libjpeg can, like gdk-pixbuf. */
GdkPixbuf* jpeg_decode_file(const gchar* path, gint width, gint height, GCancellable* cancellable);

#endif /* JPEG_DECODE_H */
