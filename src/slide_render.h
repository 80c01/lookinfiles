#ifndef SLIDE_RENDER_H
#define SLIDE_RENDER_H

#include <glib.h>

#include "preview_doc.h"

/* Slides of presentations (.pptx, .ppt) drawn like PowerPoint draws them:
 * a model of the slides (shapes with their position, rotation, geometry,
 * fill, outline, picture, text and tables), filled by the readers, drawn
 * with cairo and Pango into a PNG per slide, and the preview of a deck
 * (one picture per slide, with its speaker notes). Usable in a worker
 * thread; no widgets. Positions and sizes are in points. */

#define SLIDE_NO_COLOR 0xFFFFFFFFu

typedef enum {
    SLIDE_ALIGN_LEFT,
    SLIDE_ALIGN_CENTER,
    SLIDE_ALIGN_RIGHT,
    SLIDE_ALIGN_JUSTIFY
} SlideAlign;

typedef enum {
    SLIDE_ANCHOR_TOP,
    SLIDE_ANCHOR_MIDDLE,
    SLIDE_ANCHOR_BOTTOM
} SlideAnchor;

typedef enum {
    SLIDE_GEOM_RECT,
    SLIDE_GEOM_ROUND_RECT,
    SLIDE_GEOM_ELLIPSE,
    SLIDE_GEOM_LINE,
    SLIDE_GEOM_TRIANGLE,
    SLIDE_GEOM_RIGHT_TRIANGLE,
    SLIDE_GEOM_DIAMOND,
    SLIDE_GEOM_PARALLELOGRAM,
    SLIDE_GEOM_HEXAGON,
    SLIDE_GEOM_RIGHT_ARROW,
    SLIDE_GEOM_LEFT_ARROW,
    SLIDE_GEOM_UP_ARROW,
    SLIDE_GEOM_DOWN_ARROW,
    SLIDE_GEOM_CHEVRON,
    SLIDE_GEOM_PENTAGON,     /* home plate */
    SLIDE_GEOM_PATH          /* custom: SlideShape.path */
} SlideGeometry;

typedef struct {
    gchar* text;
    gchar* font;          /* NULL: the deck's font */
    gdouble size;         /* points */
    gboolean bold;
    gboolean italic;
    gboolean underline;
    gboolean strike;
    gint baseline;        /* 1 superscript, -1 subscript */
    guint32 color;        /* 0xRRGGBB */
} SlideRun;

typedef struct {
    GPtrArray* runs;      /* SlideRun* */
    SlideAlign align;
    gdouble left;         /* text margin */
    gdouble indent;       /* first line, relative to left (negative: the bullet hangs) */
    gdouble space_before;
    gdouble space_after;
    gdouble line_spacing; /* multiple of single spacing, 0: single */
    gchar* bullet;        /* NULL: none */
    guint32 bullet_color; /* SLIDE_NO_COLOR: the first run's colour */
    gdouble bullet_scale; /* of the first run's size, 0: 1 */
    gchar* bullet_font;
    gdouble size;         /* size of an empty paragraph (its end mark) */
} SlideParagraph;

/* A cell of a table */
typedef struct {
    GPtrArray* paragraphs;  /* SlideParagraph* */
    guint32 fill;
    guint col_span;
    guint row_span;
    gboolean merged;        /* covered by another cell's span */
    SlideAnchor anchor;
} SlideCell;

typedef struct {
    guint rows;
    guint cols;
    gdouble* col_widths;
    gdouble* row_heights;
    SlideCell* cells;       /* rows * cols */
    guint32 border;         /* SLIDE_NO_COLOR: none */
} SlideTable;

/* One element of a custom geometry, in the path's own units */
typedef enum { SLIDE_PATH_MOVE, SLIDE_PATH_LINE, SLIDE_PATH_CURVE, SLIDE_PATH_CLOSE } SlidePathOp;

typedef struct {
    SlidePathOp op;
    gdouble x[3];
    gdouble y[3];
} SlidePathStep;

typedef struct {
    gdouble x, y, w, h;     /* box */
    gdouble rotation;       /* degrees, clockwise */
    gboolean flip_h, flip_v;
    SlideGeometry geometry;
    GArray* path;           /* SlidePathStep, for SLIDE_GEOM_PATH */
    gdouble path_w, path_h; /* units of the path */
    guint32 fill;           /* SLIDE_NO_COLOR: no fill */
    guint32 fill_end;       /* gradient end colour, SLIDE_NO_COLOR: solid fill */
    gdouble gradient_angle; /* degrees, 0: left to right */
    guint32 line;           /* SLIDE_NO_COLOR: no outline */
    gdouble line_width;
    GBytes* picture;        /* encoded image stretched over the box */
    GPtrArray* paragraphs;  /* SlideParagraph* */
    SlideAnchor anchor;
    gdouble inset_left, inset_top, inset_right, inset_bottom;
    gboolean no_wrap;
    gdouble font_scale;     /* shrink on overflow: 1 = none */
    SlideTable* table;
} SlideShape;

typedef struct {
    gchar* title;           /* title text (for the label and search) */
    guint32 background;
    guint32 background_end; /* gradient end, SLIDE_NO_COLOR: solid */
    GBytes* background_picture;
    GPtrArray* shapes;      /* SlideShape*, back to front */
    gchar* notes;           /* speaker notes, NULL: none */
} Slide;

typedef struct {
    gdouble width, height;
    gchar* font;            /* default font */
    GPtrArray* slides;      /* Slide* */
} SlideDeck;

SlideDeck* slide_deck_new(gdouble width, gdouble height);
void slide_deck_free(SlideDeck* deck);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(SlideDeck, slide_deck_free)

Slide* slide_new(void);
void slide_free(Slide* slide);
SlideShape* slide_shape_new(void);
void slide_shape_free(SlideShape* shape);
SlideParagraph* slide_paragraph_new(void);
void slide_paragraph_free(SlideParagraph* para);
/* Run appended to the paragraph (text copied; font may be NULL) */
SlideRun* slide_paragraph_add_run(SlideParagraph* para, const gchar* text, gssize len);
SlideTable* slide_table_new(guint rows, guint cols);
void slide_table_free(SlideTable* table);
SlideCell* slide_table_cell(SlideTable* table, guint row, guint col);

/* Text of the paragraphs of a shape, one line each (caller frees) */
gchar* slide_shape_text(const SlideShape* shape);

/* The slide as a PNG width pixels wide; NULL if it cannot be drawn */
GBytes* slide_render_png(const SlideDeck* deck, const Slide* slide, gint width);

/* Preview of a deck: title and subtitle, then each slide as a picture with
 * its number, title and speaker notes; slides after max_drawn are listed by
 * title only */
PreviewDoc* slide_deck_preview(const SlideDeck* deck, const gchar* title, const gchar* subtitle, guint max_drawn);

#endif /* SLIDE_RENDER_H */
