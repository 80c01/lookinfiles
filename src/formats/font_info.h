#ifndef FONT_INFO_H
#define FONT_INFO_H

#include <glib.h>

/* Font file details for the preview: TrueType / OpenType (.ttf, .otf),
 * collections (.ttc, first font) and WOFF (zlib-compressed tables). WOFF2
 * tables are Brotli-compressed: only its header is read. Reads the table
 * directory and the 'name', 'maxp' and 'OS/2' tables. Pure GLib/GIO,
 * unit-tested. */

typedef struct {
    gchar* format;        /* "TrueType", "OpenType (CFF)", "TrueType collection", "WOFF", "WOFF2" */
    gchar* family;        /* typographic family (name ID 16, else 1) */
    gchar* style;         /* typographic subfamily (17, else 2): "Bold Italic" */
    gchar* full_name;     /* 4 */
    gchar* version;       /* 5, "Version " prefix removed */
    gchar* designer;      /* 9 */
    gchar* manufacturer;  /* 8 */
    gchar* copyright;     /* 0 */
    gchar* license;       /* 13 */
    gchar* license_url;   /* 14 */
    guint n_glyphs;       /* maxp, 0 if unknown */
    guint n_fonts;        /* fonts in a collection (1 otherwise) */
    guint weight;         /* OS/2 usWeightClass, 0 if unknown */
} FontInfo;

/* Details of font file data; NULL with error set if it is not a font */
FontInfo* font_info_parse(const guchar* data, gsize len, GError** error);

/* font_info_parse() on a file (at most 32 MB are read) */
FontInfo* font_info_read_file(const gchar* path, GError** error);

void font_info_free(FontInfo* info);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FontInfo, font_info_free)

/* "Thin", "Regular", "Bold"... for a weight class, NULL if 0 */
const gchar* font_info_weight_name(guint weight);

#endif /* FONT_INFO_H */
