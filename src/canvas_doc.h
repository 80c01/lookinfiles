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
 * drawing order (the last one on top). Each block has an id, unique on the
 * page (id="3"); a line or an arrow may have its ends bound to blocks
 * (start="1" end="2"): they follow them. Pure model, no widgets. */

#define CANVAS_PAGE_FILE "page.xml"
#define CANVAS_MEDIA_DIR "media"
#define CANVAS_EXT       ".canvas"

typedef enum {
    CANVAS_BLOCK_TEXT,
    CANVAS_BLOCK_IMAGE,
    CANVAS_BLOCK_SHAPE,
    CANVAS_BLOCK_TABLE
} CanvasBlockKind;

/* Text styles, combined in a run */
typedef enum {
    CANVAS_STYLE_BOLD = 1 << 0,
    CANVAS_STYLE_ITALIC = 1 << 1,
    CANVAS_STYLE_UNDERLINE = 1 << 2,
    CANVAS_STYLE_STRIKE = 1 << 3,
    CANVAS_STYLE_CODE = 1 << 4,
    CANVAS_STYLE_HEADING = 1 << 5,
    CANVAS_STYLE_SUBHEADING = 1 << 6,
    CANVAS_STYLE_HIGHLIGHT = 1 << 7,
    CANVAS_STYLE_SERIF = 1 << 8,
    CANVAS_STYLE_MONO = 1 << 9,
    CANVAS_STYLE_SMALL = 1 << 10,
    CANVAS_STYLE_LARGE = 1 << 11,
    CANVAS_STYLE_MUTED = 1 << 12,
    CANVAS_STYLE_ACCENT = 1 << 13,
    CANVAS_STYLE_DANGER = 1 << 14,
    CANVAS_STYLE_SUCCESS = 1 << 15,
    CANVAS_STYLE_WARNING = 1 << 16
} CanvasStyle;

/* Shape geometry */
typedef enum {
    CANVAS_SHAPE_RECT,
    CANVAS_SHAPE_ROUND_RECT,
    CANVAS_SHAPE_ELLIPSE,
    CANVAS_SHAPE_DIAMOND,
    CANVAS_SHAPE_LINE,
    CANVAS_SHAPE_ARROW,
    CANVAS_SHAPE_FREEHAND
} CanvasShapeType;

/* Shape fill style */
typedef enum {
    CANVAS_FILL_NONE,
    CANVAS_FILL_SOLID,
    CANVAS_FILL_SKETCH
} CanvasFillStyle;

typedef struct {
    gdouble x;
    gdouble y;
} CanvasPoint;

/* Styled run of a text block, in characters (not bytes): [start, end) */
typedef struct {
    gint start;
    gint end;
    guint styles;
} CanvasRun;

typedef struct {
    CanvasBlockKind kind;
    gint id;         /* > 0, unique in the document */
    gint x, y;       /* top-left corner on the page */
    gint w, h;       /* size; h = 0 for a text box: as tall as its text */
    /* TEXT */
    gchar* text;     /* UTF-8 content */
    GArray* runs;    /* CanvasRun, sorted, not overlapping, styles != 0 */
    /* IMAGE */
    gchar* src;      /* path relative to the note directory */
    gdouble crop_x, crop_y, crop_w, crop_h; /* crop rectangle in 0..1 (crop_w=1,crop_h=1: full) */
    /* SHAPE */
    CanvasShapeType shape_type;
    gchar* stroke_color; /* color name or palette role: fg, accent, muted, danger, success, warning */
    CanvasFillStyle fill_style;
    gint stroke_width;
    gboolean rough;   /* TRUE for hand-drawn / excalidraw sketch style */
    GArray* points;   /* CanvasPoint for freehand strokes */
    gint start_id, end_id; /* line or arrow: blocks its ends are bound to (0: free) */
    /* TABLE */
    gint table_rows;
    gint table_cols;
    GPtrArray* cells; /* gchar* array of length table_rows * table_cols */
    GArray* col_widths; /* gint array of column widths in pixels */
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

/* New Canvas note in dir (created): a heading text box with the title */
gboolean canvas_doc_create(const gchar* dir, const gchar* title, GError** error);

/* Delete a Canvas note: page.xml, media/ and the directory. Other files are
 * left (and the directory with them); FALSE if anything is left. */
gboolean canvas_doc_delete(const gchar* dir);

/* Delete the files of "<dir>/media" no block refers to; returns how many */
guint canvas_doc_prune_media(const CanvasDoc* doc, const gchar* dir);

/* Relative path of a new picture: "media/0001.png", the number after the
 * highest one in "<dir>/media" (free it) */
gchar* canvas_doc_new_media_path(const gchar* dir, const gchar* ext);

/* New blocks, appended on top (owned by the document), with a new id */
CanvasBlock* canvas_doc_add_text(CanvasDoc* doc, gint x, gint y, gint w, const gchar* text);
CanvasBlock* canvas_doc_add_image(CanvasDoc* doc, gint x, gint y, gint w, gint h, const gchar* src);
CanvasBlock* canvas_doc_add_shape(CanvasDoc* doc, gint x, gint y, gint w, gint h, CanvasShapeType shape_type);
CanvasBlock* canvas_doc_add_table(CanvasDoc* doc, gint x, gint y, gint rows, gint cols);

/* Table block operations */
const gchar* canvas_table_get_cell(const CanvasBlock* b, gint row, gint col);
void canvas_table_set_cell(CanvasBlock* b, gint row, gint col, const gchar* text);
gint canvas_table_get_col_width(const CanvasBlock* b, gint col);
void canvas_table_set_col_width(CanvasBlock* b, gint col, gint width);
void canvas_table_insert_row(CanvasBlock* b, gint at_row);
void canvas_table_insert_col(CanvasBlock* b, gint at_col);
void canvas_table_delete_row(CanvasBlock* b, gint row);
void canvas_table_delete_col(CanvasBlock* b, gint col);

/* Remove (and free) a block; FALSE if it is not in the document */
gboolean canvas_doc_remove(CanvasDoc* doc, CanvasBlock* block);

/* Move a block on top of the others */
void canvas_doc_raise(CanvasDoc* doc, CanvasBlock* block);

/* Add a styled run to a text block (merged with the runs already there) */
void canvas_block_add_run(CanvasBlock* block, gint start, gint end, guint styles);

/* Markdown of the page: blocks from top to bottom, then left to right;
 * picture links are "<media_prefix><src>" (prefix may be NULL) (free it) */
gchar* canvas_doc_to_markdown(const CanvasDoc* doc, const gchar* media_prefix);

/* Write the Markdown of the Canvas note in dir next to it: "<title>.md" (or
 * "<title> (2).md"... when taken), its pictures linked inside the note
 * ("<title>.canvas/media/0001.png"). Returns the new file's path (free it),
 * or NULL with error set. */
gchar* canvas_doc_export_markdown(const gchar* dir, GError** error);

#endif /* CANVAS_DOC_H */
