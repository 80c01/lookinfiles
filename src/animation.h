#ifndef ANIMATION_H
#define ANIMATION_H

#include <gtk/gtk.h>

/* A GdkPaintable that plays an animated image (GIF, animated WebP with its
 * loader) at a given size, for the preview text view: frames advance on a
 * timer while the paintable exists, and stop when it is released. */

#define LOOKIN_TYPE_ANIMATION (lookin_animation_get_type())
G_DECLARE_FINAL_TYPE(LookinAnimation, lookin_animation, LOOKIN, ANIMATION, GObject)

/* Paintable playing animation scaled to width x height pixels */
GdkPaintable* lookin_animation_new(GdkPixbufAnimation* animation, gint width, gint height);

#endif /* ANIMATION_H */
