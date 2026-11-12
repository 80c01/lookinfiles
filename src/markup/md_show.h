#ifndef MD_SHOW_H
#define MD_SHOW_H

/* Show the output of a rich text parser (md_out.h: Markdown, HTML, Org) in
 * a text buffer: one insertion of the text, the objects in place of their
 * U+FFFC characters (pictures fitted to view, glyphs, table grids, code
 * highlighted like the editor), then the tags by range and the links
 * (markdown_link). The tags must exist in the buffer's tag table. */
#include <gtk/gtk.h>

#include "markup/md_out.h"

/* view may be NULL (no table objects then, pictures at natural size) */
void md_out_show(GtkTextBuffer* buffer, GtkTextView* view, const MdOut* md);

#endif /* MD_SHOW_H */
