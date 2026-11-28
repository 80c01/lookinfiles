#ifndef LATEX_SYNCTEX_H
#define LATEX_SYNCTEX_H

/* SyncTeX: where a line of a LaTeX source is in its PDF, from the output of
 * "synctex view -i line:column:file.tex -o file.pdf" (the command line
 * utility of TeX Live), and back: the source line of a point of the PDF,
 * from "synctex edit -o page:x:y:file.pdf".
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

/* The -o argument of "synctex edit" for the point (x, y) of page (0-based),
 * in PDF points from the top left of the page: "page:x:y:pdf" (page
 * 1-based). Caller frees. */
gchar* latex_synctex_edit_arg(gint page, gdouble x, gdouble y, const gchar* pdf);

/* The first place of the output of "synctex edit": *input the source file
 * (made absolute from dir when relative, "./" removed; g_free), *line its
 * line (0-based). FALSE when the output has no result. */
gboolean latex_synctex_parse_edit(const gchar* output, const gchar* dir, gchar** input, gint* line);

#endif /* LATEX_SYNCTEX_H */
