#ifndef ANIMATION_H
#define ANIMATION_H

#include <gtk/gtk.h>

/* Animated GIF for the preview text view: the frames are decoded and scaled
 * in a worker thread (animation_frames_load_gif()), then played by a
 * GdkPaintable on a timer while it exists; it stops when it is released. */

/* Decoded frames (textures at the display size, delays, play count) */
typedef struct _AnimationFrames AnimationFrames;

/* Frames of the GIF file at path scaled to width x height pixels. NULL if
 * the file is not an animated GIF (one image or less) or cannot be read.
 * Stops decoding when cancelled or past a memory budget (plays the frames
 * decoded so far). Blocking: worker thread only. */
AnimationFrames* animation_frames_load_gif(const gchar* path, gint width, gint height,
                                           GCancellable* cancellable);
void animation_frames_free(AnimationFrames* frames);

#define LOOKIN_TYPE_ANIMATION (lookin_animation_get_type())
G_DECLARE_FINAL_TYPE(LookinAnimation, lookin_animation, LOOKIN, ANIMATION, GObject)

/* Paintable playing frames (takes ownership of them); main thread */
GdkPaintable* lookin_animation_new(AnimationFrames* frames);

#endif /* ANIMATION_H */
