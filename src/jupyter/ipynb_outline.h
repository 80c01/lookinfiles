#ifndef IPYNB_OUTLINE_H
#define IPYNB_OUTLINE_H

/* The outline of a Jupyter notebook for the tagbar of its tab
 * (ipynb_tagbar.h): the headings of its Markdown cells (ATX and Setext,
 * code_outline_markdown()), in cell order. Each entry's line is the index
 * of its cell and its column the line of the heading in the cell's source,
 * so the tagbar follows the current cell. Pure GLib, unit-tested. */
#include "code/code_outline_tree.h"
#include "jupyter/ipynb_doc.h"

/* A new array of CodeOutlineEntry* (freed with the array), sorted */
GPtrArray* ipynb_outline_headings(const IpynbDoc* doc);
/* TRUE when a and b list the same headings (names, levels, places) */
gboolean ipynb_outline_same(const GPtrArray* a, const GPtrArray* b);

#endif /* IPYNB_OUTLINE_H */
