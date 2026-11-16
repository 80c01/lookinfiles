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

/* --- Sections, folded as in JupyterLab ---
 * A Markdown cell with headings opens a section at the level of its highest
 * heading (# is 1): the cells after it up to the next cell opening a section
 * of that level or higher. A collapsed heading (IPYNB_FLAG_HEADING_COLLAPSED)
 * hides its section, sections inside it included. */

/* The level of the highest heading of the cell at index (1..6), 0 for a
 * cell without headings or not Markdown */
guint ipynb_outline_cell_level(const IpynbDoc* doc, guint index);
/* The index just after the section of the cell at index (index + 1 for a
 * cell that opens none); levels: one per cell, n of them */
guint ipynb_outline_section_end(const guint* levels, guint n, guint index);
/* hidden[i] (n of them) set to TRUE for the cells inside a collapsed
 * section; collapsed: one flag per cell */
void ipynb_outline_hidden(const guint* levels, const gboolean* collapsed, guint n, gboolean* hidden);

#endif /* IPYNB_OUTLINE_H */
