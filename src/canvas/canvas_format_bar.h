#ifndef CANVAS_FORMAT_BAR_H
#define CANVAS_FORMAT_BAR_H

/* Text format bar of the Canvas page: one row floating just above the text
 * box being edited or selected (below it when there is no room), over the
 * page overlay. Paragraph style, bold / italic / underline / strikethrough,
 * more styles, text colour, centred lines, lists, checkbox, indent. */
#include "canvas/canvas_text.h"
#include <gtk/gtk.h>

typedef struct {
    void (*on_style)(guint style_flag, gpointer udata);
    void (*on_preset)(CanvasPreset preset, gpointer udata);
    void (*on_list)(const gchar* prefix, gpointer udata);
    void (*on_indent)(gint step, gpointer udata);
    void (*on_checkbox)(gpointer udata);
    void (*on_center)(gpointer udata);
} CanvasFormatBarCallbacks;

typedef struct CanvasFormatBar CanvasFormatBar;

CanvasFormatBar* canvas_format_bar_new(const CanvasFormatBarCallbacks* cbs, gpointer user_data);
void canvas_format_bar_free(CanvasFormatBar* bar);

/* The bar, to add as an overlay of the GtkOverlay over the page */
GtkWidget* canvas_format_bar_get_widget(CanvasFormatBar* bar);

/* Show the bar next to target (a block frame on the page); it follows the
 * block while it moves, the view scrolls or zooms. NULL hides it. */
void canvas_format_bar_show_for(CanvasFormatBar* bar, GtkWidget* target);

/* Show the style buttons (bold, italic, underline, strikethrough) whose flag
 * is in styles as pressed */
void canvas_format_bar_set_styles(CanvasFormatBar* bar, guint styles);

/* Show the Center Text button as pressed when the text box is centred */
void canvas_format_bar_set_centered(CanvasFormatBar* bar, gboolean centered);

#endif /* CANVAS_FORMAT_BAR_H */
