#ifndef CODE_OUTLINE_TREE_H
#define CODE_OUTLINE_TREE_H

/* Outline of a file for the tagbar and the symbol picker (docs/code-nav.md):
 * its symbols (from a language server or the built-in scanner) or its
 * Markdown headings, as entries in file order, then as a tree: symbols
 * nested in their container and grouped by kind (or kept in file order),
 * headings nested by level. Pure GLib, unit-tested. */
#include "code/code_symbol.h"

typedef struct {
    gchar* name;
    gchar* detail;        /* signature / type, or NULL */
    gchar* container;     /* name of the enclosing symbol (language server), or NULL */
    CodeSymbolKind kind;
    gint level;           /* Markdown heading level (1 to 6); 0 for a symbol */
    gboolean declaration; /* prototype, forward declaration */
    gint line;            /* 0-based */
    gint column;          /* byte index in the line */
} CodeOutlineEntry;

CodeOutlineEntry* code_outline_entry_new(const gchar* name, const gchar* detail, const gchar* container,
                                         CodeSymbolKind kind, gint line, gint column);
void code_outline_entry_free(CodeOutlineEntry* entry);
/* A new array of CodeOutlineEntry* (freed with the array) */
GPtrArray* code_outline_entries_new(void);
/* Sort entries by line, then column */
void code_outline_sort(GPtrArray* entries);

/* Headings of Markdown text (ATX "# Title" and Setext "Title / ===="),
 * outside fenced code blocks and the front matter, in file order */
GPtrArray* code_outline_markdown(const gchar* text, gsize len);

/* Heading level of a text tag of the rendered previews: "md_h1" .. "md_h6"
 * (Markdown), "html_h*", "org_h*", "pd_h*" and "pd_outline_h*" (documents); 0 for another tag */
gint code_outline_heading_tag_level(const gchar* tag_name);
/* A heading entry at line (column 0): text made valid UTF-8, its runs of
 * white space (new lines too) made one space, trimmed; NULL when nothing
 * is left */
CodeOutlineEntry* code_outline_heading_new(const gchar* text, gint level, gint line);

typedef enum {
    CODE_OUTLINE_BY_KIND, /* symbols grouped by kind (Functions, Types...) */
    CODE_OUTLINE_BY_LINE  /* symbols in file order */
} CodeOutlineSort;

/* One row of the tree: a group of symbols of one kind, or an entry */
typedef struct {
    gint entry;           /* index in the entries, -1 for a group */
    CodeSymbolKind group; /* kind of a group's symbols */
    GPtrArray* children;  /* CodeOutlineNode*, owned */
} CodeOutlineNode;

/* The tree of entries (sorted by code_outline_sort()): each entry goes
 * under the nearest entry before it named as its container, a heading
 * under the nearest heading of a lower level before it; with BY_KIND the
 * symbols left at the top go into one group per kind (headings stay at the
 * top). Returns the top nodes (CodeOutlineNode*, freed with the array). */
GPtrArray* code_outline_tree(const GPtrArray* entries, CodeOutlineSort sort);
void code_outline_node_free(CodeOutlineNode* node);

/* "Functions", "Types"... */
const gchar* code_outline_group_label(CodeSymbolKind kind);

/* Index of the entry the cursor line is in: the last one (entries sorted)
 * starting at or before line; -1 when line is before them all */
gint code_outline_entry_at_line(const GPtrArray* entries, gint line);

#endif /* CODE_OUTLINE_TREE_H */
