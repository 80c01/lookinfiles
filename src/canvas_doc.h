#ifndef CANVAS_DOC_H
#define CANVAS_DOC_H

#include <glib.h>

/* Canvas notes: a page of text boxes and pictures placed freely.
 * A canvas note is a directory "<title>.canvas/" holding page.xml and the
 * pictures in media/:
 *
 *   <canvas version="1">
 *     <text x="40" y="60" w="320">Agenda <span s="b">today</span></text>
 *     <image x="400" y="60" w="480" h="300" src="media/0001.png"/>
 *   </canvas>
 *
 * Text is plain UTF-8 (newlines kept) with styled runs. Blocks are kept in
 * drawing order (the last one on top). Pure model, no widgets. */

#define CANVAS_PAGE_FILE "page.xml"
#define CANVAS_MEDIA_DIR "media"
#define CANVAS_EXT       ".canvas"

typedef enum {
    CANVAS_BLOCK_TEXT,
    CANVAS_BLOCK_IMAGE
} CanvasBlockKind;

/* Text styles, combined in a run */
typedef enum {
    CANVAS_STYLE_BOLD = 1 << 0,
    CANVAS_STYLE_ITALIC = 1 << 1,
    CANVAS_STYLE_UNDERLINE = 1 << 2,
    CANVAS_STYLE_STRIKE = 1 << 3,
    CANVAS_STYLE_CODE = 1 << 4,
    CANVAS_STYLE_HEADING = 1 << 5
} CanvasStyle;

/* Styled run of a text block, in characters (not bytes): [start, end) */
typedef struct {
    gint start;
    gint end;
    guint styles;
} CanvasRun;

typedef struct {
    CanvasBlockKind kind;
    gint x, y;       /* top-left corner on the page */
    gint w, h;       /* size; h = 0 for a text box: as tall as its text */
    gchar* text;     /* TEXT: UTF-8 content */
    GArray* runs;    /* TEXT: CanvasRun, sorted, not overlapping, styles != 0 */
    gchar* src;      /* IMAGE: path relative to the note directory */
} CanvasBlock;

typedef struct {
    GPtrArray* blocks; /* CanvasBlock*, drawing order */
} CanvasDoc;

CanvasDoc* canvas_doc_new(void);
void canvas_doc_free(CanvasDoc* doc);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(CanvasDoc, canvas_doc_free)

/* Parse page.xml content. NULL (and error set) when it is not a canvas. */
CanvasDoc* canvas_doc_parse(const gchar* xml, gssize length, GError** error);

/* page.xml content of the document (free it) */
gchar* canvas_doc_to_xml(const CanvasDoc* doc);

/* Load "<dir>/page.xml"; a directory without it is an empty page. */
CanvasDoc* canvas_doc_load(const gchar* dir, GError** error);

/* Write "<dir>/page.xml" (directory created if missing) */
gboolean canvas_doc_save(const CanvasDoc* doc, const gchar* dir, GError** error);

/* Delete the files of "<dir>/media" no block refers to; returns how many */
guint canvas_doc_prune_media(const CanvasDoc* doc, const gchar* dir);

/* Relative path of a new picture: "media/0001.png", the first number free in
 * "<dir>/media" (free it) */
gchar* canvas_doc_new_media_path(const gchar* dir, const gchar* ext);

/* New blocks, appended on top (owned by the document) */
CanvasBlock* canvas_doc_add_text(CanvasDoc* doc, gint x, gint y, gint w, const gchar* text);
CanvasBlock* canvas_doc_add_image(CanvasDoc* doc, gint x, gint y, gint w, gint h, const gchar* src);

/* Remove (and free) a block; FALSE if it is not in the document */
gboolean canvas_doc_remove(CanvasDoc* doc, CanvasBlock* block);

/* Move a block on top of the others */
void canvas_doc_raise(CanvasDoc* doc, CanvasBlock* block);

/* Add a styled run to a text block (merged with the runs already there) */
void canvas_block_add_run(CanvasBlock* block, gint start, gint end, guint styles);

/* Markdown of the page: blocks from top to bottom, then left to right;
 * picture links are "<media_prefix><src>" (prefix may be NULL) (free it) */
gchar* canvas_doc_to_markdown(const CanvasDoc* doc, const gchar* media_prefix);

#endif /* CANVAS_DOC_H */
