#ifndef CANVAS_CLIP_H
#define CANVAS_CLIP_H

/* Copy, cut, paste and duplicate the selected blocks of the Canvas page.
 * The clipboard holds the blocks (their page.xml, with the note they come
 * from, so pictures follow into another note) and their text as Markdown
 * for other applications; a picture in the clipboard pastes as a picture
 * block. */
#include <gtk/gtk.h>

typedef struct CanvasPage CanvasPage;

/* Ctrl+D: copies of the selected blocks, a little lower right, selected */
void canvas_clip_duplicate(CanvasPage* p);

/* Ctrl+C / Ctrl+X: the selected blocks to the clipboard (cut: then
 * deleted); FALSE when nothing is selected */
gboolean canvas_clip_copy(CanvasPage* p, gboolean cut);

/* Ctrl+V: TRUE if the clipboard holds blocks; they are pasted at the
 * pointer (or a little lower right of where they were) once read */
gboolean canvas_clip_paste(CanvasPage* p);

/* A picture in the clipboard becomes a new picture block with its top-left
 * corner at (x, y), once read; FALSE when the clipboard holds no picture */
gboolean canvas_clip_paste_picture(CanvasPage* p, gint x, gint y);

/* Paste of the context menu, at (x, y): copied blocks, else a picture, else
 * text as a new text box; FALSE when the clipboard holds none of them */
gboolean canvas_clip_paste_at(CanvasPage* p, gint x, gint y);

/* TRUE if canvas_clip_paste_at() has something to paste */
gboolean canvas_clip_can_paste(CanvasPage* p);

#endif /* CANVAS_CLIP_H */
