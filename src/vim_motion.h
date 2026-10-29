#ifndef VIM_MOTION_H
#define VIM_MOTION_H

/* Vim cursor motions and text objects on a GtkTextBuffer (no widgets).
 * Motions move an iterator and return FALSE when they cannot move at all
 * (the iterator is then unchanged). A "line end" position is the newline
 * of the line (or the buffer end on the last line). */
#include <gtk/gtk.h>

/* Character class for word motions: 0 blank, 1 punctuation, 2 keyword
 * (letters, digits, '_'). With big (WORD motions) every non-blank is 2. */
gint vim_char_class(gunichar c, gboolean big);

/* Virtual (screen) column of iter, tabs expanded with tabstop */
gint vim_iter_vcol(const GtkTextIter* iter, gint tabstop);
/* Move iter on its line to the character covering vcol (or the line end) */
void vim_iter_set_vcol(GtkTextIter* iter, gint vcol, gint tabstop);
/* Virtual column just after the character at iter */
gint vim_iter_vcol_end(const GtkTextIter* iter, gint tabstop);

void vim_iter_line_start(GtkTextIter* iter);
/* To the newline of the line (buffer end on the last line) */
void vim_iter_line_end(GtkTextIter* iter);
/* To the first non-blank character of the line (its end when blank) */
void vim_iter_first_nonblank(GtkTextIter* iter);
/* Normal-mode position: back from the line end onto the last character */
void vim_iter_clamp_normal(GtkTextIter* iter);
gboolean vim_iter_at_line_end(const GtkTextIter* iter);
gboolean vim_line_is_empty(GtkTextBuffer* buf, gint line);
gboolean vim_line_is_blank(GtkTextBuffer* buf, gint line);  /* only spaces / tabs */
/* Width of the leading whitespace of a line, in columns */
gint vim_line_indent(GtkTextBuffer* buf, gint line, gint tabstop);

/* Word motions (w W, e E, b B, ge gE). for_operator: w / W stop at the
 * end of the line instead of crossing it on the last step (dw, cw). */
gboolean vim_motion_word_forward(GtkTextIter* iter, gint count, gboolean big, gboolean for_operator);
gboolean vim_motion_word_end(GtkTextIter* iter, gint count, gboolean big);
gboolean vim_motion_word_backward(GtkTextIter* iter, gint count, gboolean big);
gboolean vim_motion_word_end_backward(GtkTextIter* iter, gint count, gboolean big);

/* f F t T on the current line; skip_adjacent: a repeated t / T (; ,)
 * does not stop right before the character it is already next to */
gboolean vim_motion_find_char(GtkTextIter* iter, gunichar c, gint count, gboolean forward,
                              gboolean till, gboolean skip_adjacent);

/* % : jump to the matching bracket (the first bracket after the cursor on
 * the line when the cursor is not on one) */
gboolean vim_motion_match_pair(GtkTextIter* iter);
/* [( [{ ]) ]} : the count-th unmatched open / close bracket */
gboolean vim_motion_unmatched(GtkTextIter* iter, gunichar bracket, gint count);
/* { } : previous / next empty line */
gboolean vim_motion_paragraph(GtkTextIter* iter, gint count, gboolean forward);
/* ( ) : previous / next sentence start */
gboolean vim_motion_sentence(GtkTextIter* iter, gint count, gboolean forward);

/* Text objects: obj is one of w W s p ( [ { < " ' ` t (aliases b = (,
 * B = {, ) ] } > accepted). [start, end) is the selected range; in: the
 * visual selection (start == end without one; end = cursor). linewise is
 * set for paragraphs and multi-line inner blocks. */
gboolean vim_textobj(GtkTextBuffer* buf, gunichar obj, gboolean around, gint count,
                     GtkTextIter* start, GtkTextIter* end, gboolean* linewise);

#endif /* VIM_MOTION_H */
