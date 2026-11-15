#ifndef TEXT_OUTLINE_H
#define TEXT_OUTLINE_H

#include <glib.h>

#include "preview/preview_doc.h"

/* A text shown as written, line by line, with what a parser found in it:
 * coloured ranges, foldable blocks of lines, the path of the value on each
 * line and headings for the tagbar. text_outline_print() writes it to a
 * PreviewDoc, leaving out the folded blocks. Shared by the data formats
 * whose own layout is kept (TOML, YAML). Pure GLib, unit-tested. */

/* Most lines written; the rest is replaced by a note */
#define TEXT_OUTLINE_MAX_LINES 10000

/* What a folded block counts after its first line */
typedef enum {
    TEXT_OUTLINE_KEYS,    /* "  4 keys" */
    TEXT_OUTLINE_ITEMS    /* "  3 items" */
} TextOutlineCount;

/* A foldable block: lines first to last */
typedef struct {
    guint first;
    guint last;
    gchar* id;            /* its path, the fold point id */
    gint depth;           /* nesting, from 1 (PreviewFold.level) */
    TextOutlineCount kind;
    guint count;
    gboolean ellipsis;    /* folded: "…" after the first line (an open bracket) */
} TextOutlineBlock;

typedef struct TextOutline TextOutline;

/* Lines of text (it must outlive the outline): a final line break starts
 * no line */
TextOutline* text_outline_new(const gchar* text, gsize len);
void text_outline_free(TextOutline* outline);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(TextOutline, text_outline_free)

guint text_outline_n_lines(const TextOutline* outline);
/* Line holding the byte at pos */
guint text_outline_line_at(const TextOutline* outline, gsize pos);
/* Byte offset of the start of line */
gsize text_outline_line_start(const TextOutline* outline, guint line);

/* Colour bytes [start, end) (ranges in increasing order, a range may cross
 * lines) */
void text_outline_add_token(TextOutline* outline, gsize start, gsize end, PreviewStyle style);
/* The path of the line holding pos, unless it has one already */
void text_outline_set_path(TextOutline* outline, gsize pos, const gchar* path);
/* Tag bytes [start, end) (on one line) as a tagbar heading of level 1..6 */
void text_outline_add_heading(TextOutline* outline, gsize start, gsize end, gint level);
/* Add a block (id copied); returns its index for text_outline_block() */
guint text_outline_add_block(TextOutline* outline, guint first, guint last, const gchar* id, gint depth,
                             TextOutlineCount kind, guint count, gboolean ellipsis);
/* Block index, to extend or count (until text_outline_print()) */
TextOutlineBlock* text_outline_block(TextOutline* outline, guint index);

/* The fold level in effect: fold's, or for PREVIEW_FOLD_AUTO (fold NULL
 * too) everything open when it fits in TEXT_OUTLINE_MAX_LINES, else the
 * deepest level that fits */
gint text_outline_level(TextOutline* outline, const PreviewFold* fold);

/* Append the lines to doc, a line break after each, leaving out the
 * blocks folded at level (fold toggles them): each block shown is a fold
 * point, a folded one written as its first line and its count. Each line
 * gets its path. TRUE if the text stopped at TEXT_OUTLINE_MAX_LINES (with
 * a note). */
gboolean text_outline_print(TextOutline* outline, PreviewDoc* doc, const PreviewFold* fold, gint level);

#endif /* TEXT_OUTLINE_H */
