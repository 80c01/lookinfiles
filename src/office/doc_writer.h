#ifndef DOC_WRITER_H
#define DOC_WRITER_H

#include <glib.h>

#include "preview/preview_doc.h"

/* Word processing documents (.docx, .doc) written as a PreviewDoc that looks
 * like the document: fonts, sizes relative to the default size, colours,
 * highlights, paragraph alignment, indents and spacing, list labels with
 * hanging indents, tables as grids (cell text wrapped), pictures and page
 * breaks; the page header above the text and the footer below. The readers
 * call it while they parse: runs of text, then the end of each paragraph
 * with its layout. Pure GLib, unit-tested. */

#define DOC_COLOR_NONE 0xFFFFFFFFu /* automatic colour, no highlight */

/* Character formatting of a run */
typedef struct {
    const gchar* font;   /* family, NULL: the document's default font */
    gdouble size;        /* points, 0: the default size */
    gboolean bold;
    gboolean italic;
    gboolean underline;
    gboolean strike;
    gboolean caps;       /* shown in capitals */
    gboolean link;
    gint vert;           /* 1 superscript, -1 subscript, 0 baseline */
    guint32 color;       /* 0xRRGGBB or DOC_COLOR_NONE */
    guint32 highlight;   /* 0xRRGGBB or DOC_COLOR_NONE */
} DocRun;

/* Paragraph layout, in points */
typedef struct {
    PreviewAlign align;
    gdouble left;
    gdouble right;
    gdouble first;       /* first line, relative to left (negative: hanging) */
    gdouble before;
    gdouble after;
    gint outline;        /* heading at this outline level (1-9; 7-9 listed as 6), 0: body text */
} DocPara;

void doc_run_init(DocRun* run);
void doc_para_init(DocPara* para);

/* ---- The document as written ----
 *
 * What the writer keeps: paragraphs (their list label first), tables of
 * cells holding paragraphs, and page breaks, in order. The text view
 * (doc_model_preview()) and the page view (doc_pages.h) are drawn from it. */

typedef struct {
    gchar* text;            /* NULL for a picture */
    DocRun run;             /* run.font is owned */
    GBytes* image;          /* encoded picture; NULL: a format not shown (image_name instead) */
    gdouble image_width;    /* points, 0: its own size */
    gchar* image_name;
} DocSeg;

typedef struct {
    GPtrArray* segs;        /* DocSeg* */
    DocPara layout;
} DocParagraph;

typedef struct {
    GPtrArray* paras;       /* DocParagraph* */
    guint span;             /* columns */
} DocCell;

typedef struct {
    GPtrArray* cells;       /* DocCell* */
    gboolean header;
} DocRow;

typedef struct {
    GPtrArray* rows;        /* DocRow* */
} DocTable;

typedef enum {
    DOC_BLOCK_PARAGRAPH,
    DOC_BLOCK_TABLE,
    DOC_BLOCK_PAGE_BREAK
} DocBlockKind;

typedef struct {
    DocBlockKind kind;
    DocParagraph* para;     /* DOC_BLOCK_PARAGRAPH */
    DocTable* table;        /* DOC_BLOCK_TABLE */
} DocBlock;

typedef struct {
    GPtrArray* blocks;      /* DocBlock* */
    gchar* default_font;    /* may be NULL */
    gdouble default_size;   /* points */
} DocModel;

void doc_model_free(DocModel* model);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(DocModel, doc_model_free)
/* TRUE if the model holds some text or a picture */
gboolean doc_model_has_content(const DocModel* model);

/* Page size and margins, in points; header: from the top edge to the
 * header, footer: from the bottom edge to the footer */
typedef struct {
    gdouble width, height;
    gdouble top, bottom, left, right;
    gdouble header, footer;
} DocPageSetup;

/* A4 with 2.5 cm margins, the header and footer 1.25 cm from the edges */
void doc_page_setup_init(DocPageSetup* setup);
/* Values out of range (a page smaller than its margins...) put back to
 * usable ones */
void doc_page_setup_fix(DocPageSetup* setup);

typedef struct DocWriter DocWriter;

/* Writer of a document whose default font and size are these (size in
 * points, 0: 11) */
DocWriter* doc_writer_new(const gchar* default_font, gdouble default_size);
void doc_writer_free(DocWriter* w);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(DocWriter, doc_writer_free)

/* Text of the current paragraph (len -1: NUL-terminated). Tabs are kept,
 * other control characters dropped. */
void doc_writer_text(DocWriter* w, const gchar* text, gssize len, const DocRun* run);
/* Picture in the current paragraph, width_pt wide (0: its own size); data
 * NULL (a format the preview cannot show) puts its name instead */
void doc_writer_image(DocWriter* w, GBytes* data, gdouble width_pt, const gchar* name);
/* End of the paragraph: label ("•", "2.", may be NULL) before its text in
 * label_run (NULL: plain) */
void doc_writer_end_paragraph(DocWriter* w, const DocPara* para, const gchar* label, const DocRun* label_run);
void doc_writer_page_break(DocWriter* w);

/* Tables: rows of cells holding paragraphs. Nested tables are flattened
 * into the cell of the outer one. */
void doc_writer_table_start(DocWriter* w);
void doc_writer_row_start(DocWriter* w, gboolean header);
void doc_writer_cell_start(DocWriter* w, guint span);
void doc_writer_cell_end(DocWriter* w);
void doc_writer_row_end(DocWriter* w);
void doc_writer_table_end(DocWriter* w);

/* Characters of text written so far (for the text limit) */
gsize doc_writer_length(const DocWriter* w);

/* The document written (open paragraphs and tables are ended); the
 * writer is freed */
PreviewDoc* doc_writer_finish(DocWriter* w);
/* The same as a model (caller frees); the writer is freed */
DocModel* doc_writer_finish_model(DocWriter* w);
/* The text view of a model (what doc_writer_finish() gives) */
PreviewDoc* doc_model_preview(const DocModel* model);

/* The preview: title, dimmed subtitle, the header (dimmed, may be NULL),
 * the body, the footer (dimmed, may be NULL) and a note (truncation, may be
 * NULL) */
PreviewDoc* doc_writer_page(const gchar* title, const gchar* subtitle, const PreviewDoc* header,
                            const PreviewDoc* body, const PreviewDoc* footer, const gchar* note);

#endif /* DOC_WRITER_H */
