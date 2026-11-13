#ifndef IPYNB_DOC_H
#define IPYNB_DOC_H

#include <glib.h>

/* A Jupyter notebook (.ipynb, nbformat 4) held for editing: its cells
 * (type, source, execution count, outputs) can be read, changed, added,
 * removed and moved, and the notebook written back as Jupyter writes it
 * (indent of one space, sources as lists of lines, non-ASCII text as is).
 * Everything the editor does not change (notebook and cell metadata, ids,
 * attachments, outputs, members it does not know) is kept as it was, in
 * its order, so that saving an unchanged notebook gives back the same file.
 * Pure GLib, unit-tested. */

typedef enum {
    IPYNB_CELL_CODE,
    IPYNB_CELL_MARKDOWN,
    IPYNB_CELL_RAW      /* also a cell type the editor does not know */
} IpynbCellType;

typedef enum {
    IPYNB_OUTPUT_TEXT,   /* stream or plain text result */
    IPYNB_OUTPUT_ERROR,  /* stderr stream or error with its traceback */
    IPYNB_OUTPUT_IMAGE,  /* PNG, JPEG, GIF or SVG data */
    IPYNB_OUTPUT_MATH    /* a formula (text/latex in math delimiters): text is its TeX */
} IpynbOutputKind;

typedef struct {
    IpynbOutputKind kind;
    gboolean result;     /* an execute_result (shown "Out [n]") */
    gchar* text;         /* TEXT and ERROR (ANSI colours removed); MATH: TeX without its delimiters */
    GBytes* image;       /* IMAGE: the decoded data */
    gchar* mime;         /* IMAGE: "image/png"... */
} IpynbOutput;

typedef struct IpynbDoc IpynbDoc;

/* The notebook in text; NULL with error set if it is not an nbformat 4
 * notebook (older ones are only previewed). */
IpynbDoc* ipynb_doc_parse(const gchar* text, gsize len, GError** error);
/* A new notebook (nbformat 4.5) with one empty code cell */
IpynbDoc* ipynb_doc_new(void);
void ipynb_doc_free(IpynbDoc* doc);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(IpynbDoc, ipynb_doc_free)

/* The notebook as Jupyter writes it, ending with a newline. Caller frees. */
gchar* ipynb_doc_to_json(const IpynbDoc* doc);

/* Kernel language ("python") and its file extension (".py"), or NULL */
const gchar* ipynb_doc_language(const IpynbDoc* doc);
const gchar* ipynb_doc_file_extension(const IpynbDoc* doc);
/* Kernel display name ("Python 3"), or NULL */
const gchar* ipynb_doc_kernel(const IpynbDoc* doc);

guint ipynb_doc_n_cells(const IpynbDoc* doc);
IpynbCellType ipynb_doc_cell_type(const IpynbDoc* doc, guint index);
/* The source text of the cell. Caller frees. */
gchar* ipynb_doc_cell_source(const IpynbDoc* doc, guint index);
/* The source of a Markdown cell to render: its "attachment:name" pictures
 * as data: URIs. Caller frees. */
gchar* ipynb_doc_cell_markdown(const IpynbDoc* doc, guint index);
void ipynb_doc_set_source(IpynbDoc* doc, guint index, const gchar* source);
/* Change the cell type: a code cell gets an empty execution count and
 * outputs, the other types lose them */
void ipynb_doc_set_type(IpynbDoc* doc, guint index, IpynbCellType type);
/* Execution count of a code cell, or -1 (never run, or not code) */
gint ipynb_doc_cell_execution_count(const IpynbDoc* doc, guint index);

/* The outputs of a code cell (IpynbOutput*, freed with the array); text
 * outputs longer than max_lines lines (0: no limit) are cut with a note.
 * Outputs without text or a known image (HTML only, widgets) give a short
 * note. */
GPtrArray* ipynb_doc_cell_outputs(const IpynbDoc* doc, guint index, guint max_lines);
/* Remove the outputs and execution count of one code cell, or of all
 * (index G_MAXUINT). Returns TRUE if something was removed. */
gboolean ipynb_doc_clear_outputs(IpynbDoc* doc, guint index);

/* Insert a new empty cell of type at index (clamped); returns its index */
guint ipynb_doc_insert_cell(IpynbDoc* doc, guint index, IpynbCellType type);
void ipynb_doc_remove_cell(IpynbDoc* doc, guint index);
/* Take the cell at index out of the notebook (NULL if there is none), to
 * put it back with ipynb_doc_put_cell() (which owns it then; index clamped)
 * or free it */
typedef struct IpynbCell IpynbCell;
IpynbCell* ipynb_doc_take_cell(IpynbDoc* doc, guint index);
guint ipynb_doc_put_cell(IpynbDoc* doc, guint index, IpynbCell* cell);
void ipynb_cell_free(IpynbCell* cell);
/* Move the cell at from to position to (both clamped) */
void ipynb_doc_move_cell(IpynbDoc* doc, guint from, guint to);
/* The parts of a cell source split as JupyterLab does: at the cursor
 * (start == end, character offsets), or before and after the selection
 * start..end; each part without the newlines at its ends, the empty ones
 * left out. NULL if that gives fewer than two parts. Free with g_strfreev(). */
gchar** ipynb_split_source(const gchar* source, glong start, glong end);
/* Split the cell at index into parts (NULL-terminated, two or more): new
 * cells of its type before it take the first ones, the cell keeps the last
 * one with its outputs, metadata and key. Returns the index of that cell. */
guint ipynb_doc_split_cell(IpynbDoc* doc, guint index, gchar** parts);

/* --- Running cells (ipynb_kernel.h) --- */

/* A key of the cell at index that stays the same while cells are added,
 * removed or moved (0 if there is no such cell; not saved), and the index
 * of the cell with that key now (-1: deleted) */
guint ipynb_doc_cell_key(const IpynbDoc* doc, guint index);
gint ipynb_doc_find_cell(const IpynbDoc* doc, guint key);
/* Set the execution count of a code cell (-1: none) */
void ipynb_doc_set_execution_count(IpynbDoc* doc, guint index, gint count);
/* Add an output (an nbformat output object, as JSON) to a code cell,
 * stored as Jupyter stores it: text as lists of lines, a stream written
 * in pieces joined to the output before it. FALSE with error set if json
 * is not an object or the cell not a code cell. */
gboolean ipynb_doc_append_output(IpynbDoc* doc, guint index, const gchar* json, gsize len, GError** error);
/* Kernel name of the notebook ("python3"), or NULL */
const gchar* ipynb_doc_kernel_name(const IpynbDoc* doc);
/* Set the notebook's kernelspec and language_info metadata (JSON objects,
 * either may be NULL), as Jupyter does once a kernel runs. Returns TRUE if
 * they changed. */
gboolean ipynb_doc_set_kernel_info(IpynbDoc* doc, const gchar* kernelspec, const gchar* language_info);

#endif /* IPYNB_DOC_H */
