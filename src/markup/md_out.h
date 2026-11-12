#ifndef MD_OUT_H
#define MD_OUT_H

/* Output of the rich text parsers (Markdown, HTML, Org): the text to show,
 * the ranges of named text tags, the links and the objects the view draws
 * (pictures, glyphs, tables, highlighted code). Built without widgets (in
 * a worker thread), shown on the UI thread by md_out_show() (md_show.h)
 * with one insertion and the tags applied by range: tens of
 * thousands of small insertions into a buffer shown in a view cost seconds
 * of signal handlers.
 *
 * Offsets are in characters. An object that takes room in the text (a
 * picture, a glyph, a table) is one U+FFFC character, replaced by the object
 * when shown. */
#include <glib-object.h>

/* Character standing for an object */
#define MD_OUT_OBJECT_CHAR "\xEF\xBF\xBC"

typedef enum {
    MD_OBJECT_IMAGE, /* image: a GdkPixbuf, shown fitted to the view */
    MD_OBJECT_GLYPH, /* name: a glyph of icons.h */
    MD_OBJECT_TABLE, /* rows (header first), aligns, cols */
    MD_OBJECT_CODE,  /* name: the fence info; text: the code at offset (no object character) */
} MdObjectKind;

typedef struct {
    MdObjectKind kind;
    gint offset;
    gchar* name;
    gchar* text;
    GObject* image;
    GPtrArray* rows;   /* GPtrArray of cell arrays (GPtrArray of gchar*) */
    GPtrArray* aligns; /* cells of the separator row */
    guint cols;
} MdObject;

typedef struct {
    gint start;
    gint end;
    const gchar* tag; /* interned */
} MdSpan;

typedef struct {
    gint start;
    gint end;
    gchar* url;
} MdLinkSpan;

typedef struct {
    GString* text;
    gint n_chars;
    gint n_lines;    /* line breaks in text: the line of the next text */
    GArray* spans;   /* MdSpan, in the order applied */
    GArray* links;   /* MdLinkSpan */
    GArray* objects; /* MdObject, in the order added */
} MdOut;

MdOut* md_out_new(void);
void md_out_free(MdOut* out);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(MdOut, md_out_free)

/* Current length in characters: the offset of the next text */
gint md_out_offset(const MdOut* out);
/* Line (0-based) of the next text */
gint md_out_line(const MdOut* out);
/* Append valid UTF-8 text (len -1: NUL-terminated) */
void md_out_append(MdOut* out, const gchar* text, gssize len);
/* Append text with one tag */
void md_out_append_tagged(MdOut* out, const gchar* text, const gchar* tag);
/* Tag the characters from start to the current offset (tag NULL: none) */
void md_out_tag_from(MdOut* out, gint start, const gchar* tag);
/* The characters from start to the current offset link to url */
void md_out_link_from(MdOut* out, gint start, const gchar* url);

/* A picture here (takes a reference to image) */
void md_out_add_image(MdOut* out, GObject* image);
/* A glyph and a space here, both with tag (NULL: none), as text_insert_glyph() */
void md_out_add_glyph(MdOut* out, const gchar* glyph, const gchar* tag);
/* A table here, then a line break (takes rows and aligns) */
void md_out_add_table(MdOut* out, GPtrArray* rows, GPtrArray* aligns, guint cols);
/* Highlight code (already appended at offset start) for the fence info */
void md_out_add_code(MdOut* out, gint start, const gchar* code, const gchar* info);

#endif /* MD_OUT_H */
