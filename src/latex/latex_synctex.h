#ifndef LATEX_SYNCTEX_H
#define LATEX_SYNCTEX_H

/* SyncTeX: where a line of a LaTeX source is in its PDF, from the output of
 * "synctex view -i line:column:file.tex -o file.pdf" (the command line
 * utility of TeX Live).
 *
 * Pure GLib, unit-tested. */
#include <glib.h>

/* The SyncTeX file of pdf (file.synctex.gz, else file.synctex) when one
 * exists, or NULL */
gchar* latex_synctex_file(const gchar* pdf);

/* The first place of the output of "synctex view": its page (0-based) and
 * the height of its baseline from the top of the page, in PDF points.
 * FALSE when the output has no result. */
gboolean latex_synctex_parse_view(const gchar* output, gint* page, gdouble* y);

#endif /* LATEX_SYNCTEX_H */
