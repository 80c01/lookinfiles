#ifndef FONTS_H
#define FONTS_H

/* The application's fonts, by role, like the colours of palette.h: the
 * interface font and the code font (family list and size in pixels).
 * Every other size derives from them, so that a theme only has to change
 * these values.
 *
 * - CSS (theme_add_css()): fonts_expand() replaces @font_ui, @font_code
 *   (quoted family lists) and the sizes @size_small (UI - 2), @size_bar
 *   (UI - 1: status bars, tabs), @size_normal (UI), @size_title (UI x 1.2)
 *   and @size_code.
 * - C: fonts_family() for text tags and Pango markup (a comma-separated
 *   list Pango accepts), fonts_size() for the terminal.
 * - Rendered documents scale their text tags with FONT_SCALE_*.
 *
 * Pure GLib, unit-tested. */
#include <glib.h>

typedef enum {
    FONT_UI,   /* interface, rendered documents */
    FONT_CODE, /* file contents, code, terminal, key names */
    FONT_COUNT
} FontRole;

/* Relative sizes of text tags in rendered documents (Markdown, HTML,
 * structured previews): one heading scale for all of them */
#define FONT_SCALE_H1 2.0
#define FONT_SCALE_H2 1.5
#define FONT_SCALE_H3 1.25
#define FONT_SCALE_H4 1.0
#define FONT_SCALE_H5 0.875
#define FONT_SCALE_H6 0.85
#define FONT_SCALE_TITLE 1.2  /* title of a preview card */
#define FONT_SCALE_SMALL 0.85 /* metadata, captions, labels */
#define FONT_SCALE_CODE 0.92  /* inline code and code blocks in documents */

/* "Segoe UI, Ubuntu, ..., sans-serif" (static, valid until the next
 * fonts_set() / fonts_reset()) */
const gchar* fonts_family(FontRole role);
/* Size in pixels */
gint fonts_size(FontRole role);
/* Family list for TeX formulas (math_paintable.h): serif faces with true
 * italics, as TeX draws math; sized from FONT_UI, not themed */
const gchar* fonts_math_family(void);

/* A valid family list: names of letters, digits, spaces, '-' and '_',
 * separated by commas */
gboolean fonts_valid_family(const gchar* family);
#define FONTS_MIN_SIZE 6
#define FONTS_MAX_SIZE 72

/* Change a role (for themes): family is a comma-separated list (NULL keeps
 * it), size_px in 6..72 (0 keeps it). FALSE (and no change) if invalid. */
gboolean fonts_set(FontRole role, const gchar* family, gint size_px);
/* Back to the defaults */
void fonts_reset(void);

/* Zoom: the current sizes become the reference (call after applying a
 * theme, before the user's sizes); fonts_zoom_percent() is the code size
 * against it (100 when unchanged) */
void fonts_mark_base(void);
gint fonts_zoom_percent(void);

/* The family list for CSS: "'Segoe UI', 'Ubuntu', ..., sans-serif" */
gchar* fonts_css_family(FontRole role);
/* Copy of css with the @font_... and @size_... tokens replaced; other
 * '@' (palette colours) are kept */
gchar* fonts_expand(const gchar* css);

#endif /* FONTS_H */
