#ifndef PREVIEW_DOC_H
#define PREVIEW_DOC_H

#include <glib.h>

/* A preview built in a worker thread, shown by the viewer on the UI thread.
 *
 * Most formats produce styled text: the text plus spans of named styles
 * (colours, weights, backgrounds) and inline images. The viewer turns the
 * styles into GtkTextTags (preview_doc_view), so search, selection and copy
 * work the same in every preview. Formats that convert to HTML or Markdown
 * hand that source to the existing renderers instead.
 *
 * Shared building blocks keep the formats consistent: a summary card
 * (title, aligned label / value fields, sections) and text tables (aligned
 * columns, numeric columns right-aligned, zebra rows). No widgets: usable in a
 * worker thread and unit-tested. */

typedef enum {
    PREVIEW_STYLE_PLAIN = 0, /* no style (never stored as a span) */
    PREVIEW_STYLE_TITLE,     /* card title: larger, bold */
    PREVIEW_STYLE_DIM,       /* secondary text, field labels */
    PREVIEW_STYLE_SECTION,   /* section title in a card */
    PREVIEW_STYLE_BOLD,
    PREVIEW_STYLE_ITALIC,
    PREVIEW_STYLE_UNDERLINE,
    PREVIEW_STYLE_STRIKE,
    PREVIEW_STYLE_HEADER,    /* table header row */
    PREVIEW_STYLE_ROW_ALT,   /* zebra stripe: whole-line background */
    PREVIEW_STYLE_GRID,      /* table rules and separators */
    PREVIEW_STYLE_KEY,       /* JSON keys, field names */
    PREVIEW_STYLE_STRING,
    PREVIEW_STYLE_NUMBER,
    PREVIEW_STYLE_KEYWORD,   /* true / false / null, keywords */
    PREVIEW_STYLE_COMMENT,
    PREVIEW_STYLE_ADDED,     /* diff: added line (whole-line background) */
    PREVIEW_STYLE_REMOVED,   /* diff: removed line */
    PREVIEW_STYLE_HUNK,      /* diff: @@ hunk header */
    PREVIEW_STYLE_FILE,      /* diff: file header, file names */
    PREVIEW_STYLE_ERROR,
    PREVIEW_STYLE_WARNING,
    PREVIEW_STYLE_INFO,
    PREVIEW_STYLE_SUCCESS,
    PREVIEW_STYLE_LINK,
    PREVIEW_STYLE_HEADING1,
    PREVIEW_STYLE_HEADING2,
    PREVIEW_STYLE_HEADING3,
    PREVIEW_STYLE_CODE,      /* code block: monospace, whole-line background */
    PREVIEW_STYLE_MONO,      /* monospace run in proportional text */
    PREVIEW_STYLE_INDENT,    /* paragraph indented by one level (wrapped lines too) */
    PREVIEW_STYLE_INDENT2,   /* two levels */
    PREVIEW_STYLE_INDENT3,   /* three levels */
    PREVIEW_STYLE_FONT,      /* text in a given font; arg = Pango font description */
    PREVIEW_STYLE_SUPERSCRIPT,
    PREVIEW_STYLE_SUBSCRIPT,
    PREVIEW_STYLE_TABLE,     /* monospace lines that never wrap (grids in wrapped text) */
    PREVIEW_STYLE_FAMILY,    /* arg = font family of a document ("Liberation Serif") */
    PREVIEW_STYLE_SCALE,     /* arg = font scale ("1.500"), 1 = the view's font size */
    PREVIEW_STYLE_COLOR,     /* arg = text colour of a document ("#rrggbb"), made readable on the view */
    PREVIEW_STYLE_HIGHLIGHT, /* arg = background colour of a run ("#rrggbb"), text colour chosen to contrast */
    PREVIEW_STYLE_PARAGRAPH, /* arg = paragraph layout, see preview_doc_add_paragraph_span() */
    PREVIEW_STYLE_COUNT
} PreviewStyle;

typedef enum {
    PREVIEW_DOC_TEXT,     /* styled text (text, spans, images) */
    PREVIEW_DOC_HTML,     /* text is HTML for the HTML renderer */
    PREVIEW_DOC_MARKDOWN  /* text is Markdown for the Markdown renderer */
} PreviewDocFormat;

/* A styled range, in characters (not bytes) */
typedef struct {
    gint start;
    gint end;
    PreviewStyle style;
    gchar* arg;           /* FONT ("Family, Bold 24"), FAMILY, SCALE, COLOR, HIGHLIGHT, PARAGRAPH */
} PreviewSpan;

/* An image at a character offset (its placeholder is one U+FFFC character) */
typedef struct {
    gint offset;
    GBytes* data;         /* encoded image (PNG, JPEG...) */
    gint max_width;       /* scaled down to this width, in pixels */
} PreviewImage;

typedef struct {
    PreviewDocFormat format;
    GString* text;
    gint n_chars;
    GArray* spans;        /* PreviewSpan */
    GArray* images;       /* PreviewImage */
    gboolean monospace;   /* whole preview in a monospace font */
    gboolean wrap;        /* wrap long lines (FALSE: scroll horizontally) */
    gint label_width;     /* card field labels are padded to this many columns */
    gboolean scroll_to_end; /* open scrolled to the end (logs: latest entries) */
    GPtrArray* font_files;  /* font files the view loads before showing (font samples) */
    guint pages;          /* pages after this view, reached with PgDn (mailbox messages), 0: none */
} PreviewDoc;

/* Width of the labels of preview_doc_field() unless changed */
#define PREVIEW_DOC_LABEL_WIDTH 14

/* Placeholder character of an inline image */
#define PREVIEW_DOC_IMAGE_CHAR "\xEF\xBF\xBC"

/* Empty styled-text document: proportional font, wrapped lines */
PreviewDoc* preview_doc_new(void);
/* Document handed to the HTML or Markdown renderer (takes source) */
PreviewDoc* preview_doc_new_source(PreviewDocFormat format, gchar* source);
void preview_doc_free(PreviewDoc* doc);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PreviewDoc, preview_doc_free)

/* Current length in characters (the offset of the next appended text) */
gint preview_doc_offset(const PreviewDoc* doc);

/* Append text (len -1: NUL-terminated); invalid UTF-8 is repaired */
void preview_doc_append_len(PreviewDoc* doc, const gchar* text, gssize len);
void preview_doc_append(PreviewDoc* doc, PreviewStyle style, const gchar* text);
void preview_doc_appendf(PreviewDoc* doc, PreviewStyle style, const gchar* format, ...)
    G_GNUC_PRINTF(3, 4);

/* Style a range of characters (empty ranges and PLAIN are ignored) */
void preview_doc_add_span(PreviewDoc* doc, gint start, gint end, PreviewStyle style);
/* Text in a font: a Pango font description ("DejaVu Sans, Bold 24") */
void preview_doc_add_font_span(PreviewDoc* doc, gint start, gint end, const gchar* description);
/* Span of a style that takes an argument (FONT, FAMILY, SCALE...) */
void preview_doc_add_arg_span(PreviewDoc* doc, gint start, gint end, PreviewStyle style, const gchar* arg);

/* Layout of whole paragraphs, in pixels added to the view's own margins */
typedef enum {
    PREVIEW_ALIGN_LEFT,
    PREVIEW_ALIGN_CENTER,
    PREVIEW_ALIGN_RIGHT,
    PREVIEW_ALIGN_JUSTIFY
} PreviewAlign;

typedef struct {
    PreviewAlign align;
    gint left;            /* left indent */
    gint right;           /* right indent */
    gint indent;          /* first line, relative to left (negative: hanging) */
    gint above;           /* space above the paragraph */
    gint below;           /* space below */
} PreviewParagraph;

/* Lay out the paragraphs of [start, end) (the argument is the struct as text) */
void preview_doc_add_paragraph_span(PreviewDoc* doc, gint start, gint end, const PreviewParagraph* para);
/* Read back the argument of a PARAGRAPH span; FALSE if it is malformed */
gboolean preview_paragraph_parse(const gchar* arg, PreviewParagraph* para);

/* Append another document: its text, spans and images (fonts files too) */
void preview_doc_append_doc(PreviewDoc* doc, const PreviewDoc* other);

/* --- Document colours (0xRRGGBB) --- */

/* "#rrggbb" (caller frees) */
gchar* preview_color_format(guint32 rgb);
/* "#rrggbb" or "rrggbb" to 0xRRGGBB; FALSE if it is not one */
gboolean preview_color_parse(const gchar* text, guint32* rgb);
/* WCAG contrast ratio of two colours, 1 to 21 */
gdouble preview_color_contrast(guint32 a, guint32 b);
/* A document colour readable on background: kept when it contrasts enough,
 * greys become text (the theme's text colour), other colours are lightened
 * or darkened keeping their hue */
guint32 preview_color_readable(guint32 color, guint32 background, guint32 text);

/* Font file the view must load for the font spans (font previews) */
void preview_doc_add_font_file(PreviewDoc* doc, const gchar* path);

/* Inline image at the current position (keeps a reference to data) */
void preview_doc_add_image(PreviewDoc* doc, GBytes* data, gint max_width);

/* --- Summary card --- */

/* Title line, then a dimmed subtitle line (optional), then a blank line */
void preview_doc_title(PreviewDoc* doc, const gchar* title, const gchar* subtitle);
/* Blank line (unless at the start), then a bold section title */
void preview_doc_section(PreviewDoc* doc, const gchar* title);
/* "label       value" line, label dimmed and padded to doc->label_width.
 * Nothing is added when value is NULL or empty. */
void preview_doc_field(PreviewDoc* doc, const gchar* label, const gchar* value);
void preview_doc_field_styled(PreviewDoc* doc, const gchar* label, const gchar* value,
                             PreviewStyle value_style);
/* "  text" list line */
void preview_doc_item(PreviewDoc* doc, const gchar* text);
/* Dimmed line (truncation notices, hints) */
void preview_doc_note(PreviewDoc* doc, const gchar* text);

/* --- Tables --- */

typedef struct PreviewTable PreviewTable;

PreviewTable* preview_table_new(guint n_cols);
void preview_table_free(PreviewTable* table);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PreviewTable, preview_table_free)

guint preview_table_get_n_cols(const PreviewTable* table);
guint preview_table_get_n_rows(const PreviewTable* table);
/* Header row (n_cols cells; NULL cells are empty) */
void preview_table_set_header(PreviewTable* table, const gchar* const* cells);
/* Body row: the first n_cols cells are copied (missing ones are empty) */
void preview_table_add_row(PreviewTable* table, const gchar* const* cells, guint n_cells);
/* Show a dimmed row number column ("1", "2"...) before the cells */
void preview_table_set_row_numbers(PreviewTable* table, gboolean row_numbers);
/* Right-align a column (numeric columns are right-aligned anyway) */
void preview_table_set_right_aligned(PreviewTable* table, guint col);
/* Widest cell in characters; longer cells end with "…" (default 40) */
void preview_table_set_max_cell_width(PreviewTable* table, guint width);

/* Display width of a string in a monospace font: wide East Asian characters
 * count 2, combining marks 0 */
guint preview_text_width(const gchar* text);

/* TRUE if a cell looks numeric ("12", "-3.5", "1,234", "45%", "1e9") */
gboolean preview_cell_is_number(const gchar* cell);

/* Append the table: header (bold) and rule, rows with numeric columns
 * right-aligned and coloured, zebra stripes. Sets doc->monospace and turns
 * wrapping off. */
void preview_doc_add_table(PreviewDoc* doc, const PreviewTable* table);

#endif /* PREVIEW_DOC_H */
