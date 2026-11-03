#ifndef CANVAS_TEXT_H
#define CANVAS_TEXT_H

#include "canvas_doc.h"
#include <gtk/gtk.h>

/* Number of style tags recognized in CanvasStyle bit order */
#define CANVAS_TEXT_STYLE_COUNT 17

/* Tag names in bit order */
extern const gchar* const CANVAS_TEXT_STYLE_TAGS[CANVAS_TEXT_STYLE_COUNT];

/* Ensure all font/color/weight tags exist on the buffer */
void canvas_text_ensure_tags(GtkTextBuffer* buffer);

/* Get the GtkTextTag for the given bit index (0..CANVAS_TEXT_STYLE_COUNT-1) */
GtkTextTag* canvas_text_style_tag(GtkTextBuffer* buffer, guint bit_index);

/* Bitmask of CanvasStyle flags active at iter */
guint canvas_text_styles_at(GtkTextBuffer* buffer, const GtkTextIter* iter);

/* Apply styles bitmask to range [start, end) */
void canvas_text_apply_styles(GtkTextBuffer* buffer, gint start, gint end, guint styles);

/* Toggle a style bit on the current selection or update pending styles */
void canvas_text_toggle_style(GtkTextBuffer* buffer, guint* pending, guint style);

/* Apply a style preset to the current paragraph or selection (H1, H2, Body, Quote) */
typedef enum {
    CANVAS_PRESET_H1,
    CANVAS_PRESET_H2,
    CANVAS_PRESET_BODY,
    CANVAS_PRESET_QUOTE
} CanvasPreset;

void canvas_text_apply_preset(GtkTextBuffer* buffer, CanvasPreset preset);

/* Toggle checkbox on the line at cursor, or insert a new checkbox if none: [ ] <-> [X] */
gboolean canvas_text_toggle_checkbox(GtkTextBuffer* buffer);

/* Toggle checkbox when user clicks near [ ] or [X] in the text view */
gboolean canvas_text_click_checkbox(GtkTextView* view, gdouble x, gdouble y);

/* Insert bullet list (- ), numbered list (1. ), or task (- [ ] ) */
void canvas_text_insert_list(GtkTextBuffer* buffer, const gchar* prefix);

/* Adjust margin/indent of selected paragraphs (step = +1 or -1) */
void canvas_text_indent(GtkTextBuffer* buffer, gint step);

/* Enter key handler for auto-continuation of lists and checkboxes.
 * Returns TRUE if handled (swallowed), FALSE to let default GtkTextView handler run. */
gboolean canvas_text_handle_enter(GtkTextView* view);

#endif /* CANVAS_TEXT_H */
