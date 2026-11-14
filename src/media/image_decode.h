#ifndef IMAGE_DECODE_H
#define IMAGE_DECODE_H

#include <gtk/gtk.h>

#include "media/animation.h"

/* Images decoded for the preview in worker threads, at the size they are
 * shown, turned upright from their EXIF orientation (camera RAW: the JPEG
 * preview it embeds). The last images decoded are cached (keyed by file
 * version and size), and the neighbours of the previewed file can be
 * decoded ahead (image_decode_prefetch()), so moving to them shows them at
 * once. A request for an image already being decoded waits for that
 * decoding instead of starting another. */

/* How an image is shown: fitted to a box (or 1:1 with render_raw), zoom
 * relative to the fitted size */
typedef struct {
    gboolean render_raw;
    gdouble zoom;
    gint box_w;
    gint box_h;
} ImageDecodeSize;

typedef struct {
    gchar* error_msg;  /* why the image cannot be shown, or NULL */
    gchar* fmt_upper;  /* format name, upper case */
    gchar* size_str;   /* file size */
    gint orig_w;       /* upright size (EXIF orientation applied) */
    gint orig_h;
    gint target_w;     /* size shown */
    gint target_h;
    gboolean is_fitted;
    gchar** details;   /* photo details lines (EXIF), or NULL */
    GdkTexture* texture;
    AnimationFrames* animation; /* animated GIF: played instead of texture */
} ImageDecoded;

void image_decoded_free(ImageDecoded* decoded);

/* Called on the main thread with the decoded image (owned by the callee),
 * never when the request's cancellable was cancelled */
typedef void (*ImageDecodeReady)(ImageDecoded* decoded, gpointer user_data);

/* Decode the image at path (name: its file name) for size. destroy frees
 * user_data once the request is done or cancelled. */
void image_decode_async(const gchar* path, const gchar* name, const ImageDecodeSize* size,
                        GCancellable* cancellable, ImageDecodeReady ready, gpointer user_data,
                        GDestroyNotify destroy);

/* Decode these images (NULL-terminated, may be NULL) ahead into the cache,
 * replacing the previous prefetch: those no longer listed are cancelled */
void image_decode_prefetch(const gchar* const* paths, const ImageDecodeSize* size);

/* The whole image, upright, at its own size (capped), or NULL. Blocking:
 * call it from a worker thread. */
GdkTexture* image_decode_whole(const gchar* path);

#endif /* IMAGE_DECODE_H */
