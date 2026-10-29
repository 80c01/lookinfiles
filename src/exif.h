#ifndef EXIF_H
#define EXIF_H

#include <glib.h>

/* EXIF photo details (camera, lens, exposure, date taken, GPS position,
 * orientation) from JPEG (APP1), TIFF, PNG (eXIf chunk) and WebP (EXIF chunk)
 * files. Reads the first EXIF_READ_BYTES only. Pure GLib, unit-tested. */

#define EXIF_READ_BYTES (256 * 1024)

typedef struct {
    gchar* make;
    gchar* model;
    gchar* lens;
    gchar* software;
    gchar* artist;
    gchar* copyright;
    gchar* date_taken;       /* "2026:10:29 10:00:00" as stored */
    gint orientation;        /* 1..8, 0 if absent */
    gdouble exposure_time;   /* seconds, 0 if absent */
    gdouble f_number;
    gdouble focal_length;    /* mm */
    gint focal_length_35mm;
    gint iso;
    gint flash;              /* -1 if absent; bit 0: fired */
    gboolean has_gps;
    gdouble latitude;        /* degrees, south negative */
    gdouble longitude;       /* degrees, west negative */
    gboolean has_altitude;
    gdouble altitude;        /* metres */
} ExifInfo;

/* Details from a TIFF structure ("II*\0" / "MM\0*" header); NULL if invalid */
ExifInfo* exif_parse_tiff(const guchar* data, gsize len);

/* Details of an image file's first bytes (JPEG, TIFF, PNG, WebP); NULL if
 * there are none */
ExifInfo* exif_parse(const guchar* data, gsize len);

/* exif_parse() on the first EXIF_READ_BYTES of a file (worker thread) */
ExifInfo* exif_read_file(const gchar* path);

void exif_info_free(ExifInfo* info);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(ExifInfo, exif_info_free)

/* Summary lines for the preview, NULL entries skipped:
 *   camera   "Canon EOS R5 · RF24-105mm F4 L IS USM"
 *   exposure "1/250 s · f/4 · ISO 200 · 50 mm (75 mm equiv.) · flash"
 *   taken    "Taken 2026-10-29 10:00"
 *   place    "48.85840° N, 2.29450° E · 35 m"
 *   other    "Edited with GIMP 2.10 · © Ada"
 * Returns a NULL-terminated array (possibly empty). Caller frees (g_strfreev). */
gchar** exif_summary_lines(const ExifInfo* info);

#endif /* EXIF_H */
