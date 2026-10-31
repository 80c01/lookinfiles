#ifndef PPTX_SHAPES_PRIVATE_H
#define PPTX_SHAPES_PRIVATE_H

/* Types shared by pptx_shapes.c (theme, shape tree parser) and
 * pptx_build.c (slides from the trees); internal to those two files. */
#include "pptx_shapes.h"

#define MAX_LEVELS 9
#define EMU_PER_PT 12700.0

enum { C_DK1, C_LT1, C_DK2, C_LT2, C_ACCENT1, C_ACCENT2, C_ACCENT3, C_ACCENT4, C_ACCENT5, C_ACCENT6, C_HLINK,
       C_FOLHLINK, C_COUNT };

struct PptxTheme {
    guint32 colors[C_COUNT];
    gchar* major;
    gchar* minor;
};


enum {
    LS_MARL = 1 << 0, LS_INDENT = 1 << 1, LS_ALIGN = 1 << 2, LS_SIZE = 1 << 3, LS_BOLD = 1 << 4,
    LS_ITALIC = 1 << 5, LS_UNDERLINE = 1 << 6, LS_STRIKE = 1 << 7, LS_COLOR = 1 << 8, LS_FONT = 1 << 9,
    LS_BULLET = 1 << 10, LS_BULLET_FONT = 1 << 11, LS_BULLET_COLOR = 1 << 12, LS_BULLET_SCALE = 1 << 13,
    LS_BEFORE = 1 << 14, LS_AFTER = 1 << 15, LS_LINE = 1 << 16, LS_BASELINE = 1 << 17,
};

/* Paragraph and character properties of one list level */
typedef struct {
    guint set;
    gdouble marl, indent;
    SlideAlign align;
    gdouble size;
    gboolean bold, italic, underline, strike;
    gint baseline;
    guint32 color;
    gchar* font;
    gchar* bullet;          /* with LS_BULLET: NULL none, "#scheme" automatic numbers */
    gchar* bullet_font;
    guint32 bullet_color;
    gdouble bullet_scale;
    gdouble before, after;  /* points, or negative: minus a fraction of a line */
    gdouble line;
} LevelStyle;

typedef struct {
    gchar* text;      /* NULL: line break */
    LevelStyle rpr;
} RawRun;

typedef struct {
    gint lvl;
    LevelStyle ppr;
    LevelStyle end;   /* endParaRPr */
    GPtrArray* runs;  /* RawRun* */
} RawPara;

enum { B_ANCHOR = 1, B_LEFT = 2, B_TOP = 4, B_RIGHT = 8, B_BOTTOM = 16, B_WRAP = 32, B_SCALE = 64 };

typedef struct {
    guint set;
    SlideAnchor anchor;
    gdouble left, top, right, bottom;
    gboolean no_wrap;
    gdouble font_scale;
} BodyProps;

/* A table cell as read */
typedef struct {
    GPtrArray* paras;   /* RawPara* */
    guint32 fill;
    guint col_span, row_span;
    gboolean merged;
    SlideAnchor anchor;
} RawCell;

typedef struct {
    SlideShape* shape;       /* geometry, fill, outline, picture */
    gboolean has_xfrm;
    gboolean fill_set, line_set;
    guint32 ref_fill, ref_line, ref_font;
    gboolean is_ph;
    gchar* ph_type;          /* NULL: none given */
    gint ph_idx;             /* -1: none */
    gboolean hidden;
    BodyProps body;
    LevelStyle lst[MAX_LEVELS];
    GPtrArray* paras;        /* RawPara* */
    /* Table */
    GArray* grid;            /* gdouble column widths */
    GArray* heights;         /* gdouble row heights */
    GPtrArray* rows;         /* GPtrArray* of RawCell* */
    gboolean table_styled;
    gboolean first_row, band_rows;
    const gchar* label;      /* chart or diagram drawn as a box with this word */
} PShape;

struct PptxTree {
    GPtrArray* shapes;       /* PShape*, back to front */
    gint clr_map[C_COUNT];   /* logical colour (MAP_NAMES) -> scheme colour */
    gboolean has_bg;
    guint32 bg, bg_end;
    GBytes* bg_picture;
    LevelStyle styles[3][MAX_LEVELS];  /* master text styles: title, body, other */
    gboolean show_master_sp;
};

enum { KIND_TITLE, KIND_BODY, KIND_OTHER };

void pptx_ls_clear(LevelStyle* s);
void pptx_ls_merge(LevelStyle* d, const LevelStyle* s);
void pptx_body_merge(BodyProps* d, const BodyProps* s);
/* Colour lightened towards white (tint, v = 1: unchanged) */
guint32 pptx_tint(guint32 rgb, gdouble v);

#endif /* PPTX_SHAPES_PRIVATE_H */
