#ifndef IMAGE_THEME_H
#define IMAGE_THEME_H

/* A theme's colours from the colours of an image: the main colours of the
 * pixels are found (k-means), then every palette role (palette.h) is
 * derived from them in OKLCH:
 *
 * - surfaces take the hue of the most present colour, lightly tinted; a
 *   mostly dark image gives a dark theme, a mostly light one a light theme;
 * - text keeps a readable contrast on them;
 * - the accent is the most saturated colour of the image;
 * - syntax colours take the other hues of the image;
 * - colours with a meaning (errors in red, additions in green, ANSI
 *   colours...) keep their hue, pulled toward a near hue of the image.
 *
 * Pure GLib, no widgets: unit-tested in tests/. */
#include <glib.h>

#include "theme/palette.h"

typedef struct {
    gchar colors[PAL_COUNT][8]; /* "#rrggbb" by role */
    gboolean dark;              /* dark interface */
} ImageTheme;

/* Derive a theme from 8-bit RGB (channels 3) or RGBA (channels 4) pixels,
 * rows rowstride bytes apart. Transparent pixels are ignored. FALSE if the
 * image has no opaque pixel. */
gboolean image_theme_derive(const guint8* pixels, gint width, gint height, gint rowstride, gint channels,
                            ImageTheme* out);

/* WCAG contrast ratio (1..21) of two "#rrggbb" colours; 0 if one is not a colour */
gdouble image_theme_contrast(const gchar* a, const gchar* b);

#endif /* IMAGE_THEME_H */
