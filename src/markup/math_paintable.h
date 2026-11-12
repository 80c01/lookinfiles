#ifndef MATH_PAINTABLE_H
#define MATH_PAINTABLE_H

/* A TeX formula (tex_math.h) laid out as TeX does (stacked fractions,
 * roots, scripts, limits, stretched delimiters, matrices) and drawn as a
 * GdkPaintable to insert in a text buffer: Pango text in the math fonts
 * (fonts_math_family()) and Cairo lines, in the document text colour of
 * the palette when drawn (it follows theme changes), sharp at any scale.
 *
 * A paintable stands on the baseline of its line: lower it by *descent
 * pixels (a text tag with "rise" = -descent * PANGO_SCALE) so that the
 * baseline of the formula meets the text's. */
#include <gtk/gtk.h>

/* tex at size_px (the text size); display: display style (big operators
 * with limits above and below, larger fractions). pc: the Pango context of
 * the view (NULL: the default font map). */
GdkPaintable* math_paintable_new(PangoContext* pc, const gchar* tex, gboolean display, gdouble size_px,
                                 gint* descent);

#endif /* MATH_PAINTABLE_H */
