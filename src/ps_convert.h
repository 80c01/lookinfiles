#ifndef PS_CONVERT_H
#define PS_CONVERT_H

/* PostScript (.ps, .eps) and TeX DVI files are previewed as PDF: Ghostscript
 * (and dvips for DVI) turn the first pages into a PDF kept in the user cache
 * directory, which the PDF viewer then shows. Both tools are optional. */
#include <gio/gio.h>

typedef enum {
    PS_SOURCE_POSTSCRIPT,
    PS_SOURCE_DVI
} PsSource;

/* Command lines (NULL-terminated, free with g_strfreev). `last_page` limits
 * the pages converted (0: all). Paths must be absolute. Ghostscript always
 * runs with -dSAFER; dvips runs with -R2 (no shell escapes). */
gchar** ps_gs_argv(const gchar* gs, const gchar* input, const gchar* output,
                   gboolean eps, gint last_page);
gchar** ps_dvips_argv(const gchar* dvips, const gchar* input, const gchar* output, gint last_page);

/* TRUE if the tools needed for this source are installed */
gboolean ps_convert_available(PsSource source);

/* Convert the first `last_page` pages of path to a PDF in the cache and
 * return its path (free with g_free), reusing an up-to-date conversion.
 * NULL with `error` set if a tool is missing or fails, or if cancelled. */
gchar* ps_convert_to_pdf(const gchar* path, PsSource source, gint last_page,
                         GCancellable* cancellable, GError** error);

#endif /* PS_CONVERT_H */
