#ifndef NOTEBOOK_H
#define NOTEBOOK_H

#include <glib.h>

/* Jupyter notebooks (.ipynb, nbformat 4 and 3) converted to Markdown for the
 * Markdown renderer: Markdown cells as they are, code cells as fenced blocks
 * in the kernel language with their execution count, and their outputs:
 * text (stream, plain results, errors with ANSI colours removed), images
 * (PNG, JPEG, SVG as data: URIs) and Markdown. Long text outputs and large
 * images are cut. Pure GLib, unit-tested. */

/* Most lines of one text output */
#define NOTEBOOK_MAX_OUTPUT_LINES 60
/* Largest image output kept (base64 size) */
#define NOTEBOOK_MAX_IMAGE (4 * 1024 * 1024)

typedef struct {
    guint n_cells;
    guint n_code;
    gchar* language;     /* kernel language ("python"), or NULL */
    gchar* kernel;       /* kernel display name ("Python 3"), or NULL */
} NotebookInfo;

/* Markdown of a notebook; NULL with error set if it is not one. info (may
 * be NULL) receives the counts; free its strings with notebook_info_clear(). */
gchar* notebook_to_markdown(const gchar* text, gsize len, NotebookInfo* info, GError** error);
void notebook_info_clear(NotebookInfo* info);

/* Text without ANSI escape sequences (colours in tracebacks). Caller frees. */
gchar* notebook_strip_ansi(const gchar* text);

#endif /* NOTEBOOK_H */
