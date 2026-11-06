#ifndef CANVAS_PROPS_H
#define CANVAS_PROPS_H

/* Properties panel of the Canvas page, floating on the left of the page:
 * - shape style (stroke colour, fill, stroke width, hand-drawn or clean)
 *   while a drawing tool is chosen or a shape is selected;
 * - text formatting (styles, colours, lists, indent) while a text box is
 *   edited or selected;
 * hidden otherwise. */
#include "canvas_doc.h"
#include "canvas_text.h"
#include <gtk/gtk.h>

/* Style of a shape: the next shapes drawn, or the selected one */
typedef struct {
    const gchar* stroke_color; /* palette role name: "fg", "accent"... (static) */
    CanvasFillStyle fill;
    gint stroke_width;
    gboolean rough;
} CanvasShapeStyle;

typedef struct {
    /* The user changed the shape style (any of its fields) */
    void (*on_shape_style)(const CanvasShapeStyle* style, gpointer udata);
    void (*on_format_style)(guint style_flag, gpointer udata);
    void (*on_format_preset)(CanvasPreset preset, gpointer udata);
    void (*on_format_list)(const gchar* prefix, gpointer udata);
    void (*on_format_indent)(gint step, gpointer udata);
    void (*on_format_checkbox)(gpointer udata);
} CanvasPropsCallbacks;

typedef struct CanvasProps CanvasProps;

CanvasProps* canvas_props_new(const CanvasPropsCallbacks* cbs, gpointer user_data);
GtkWidget* canvas_props_get_widget(CanvasProps* props);
void canvas_props_free(CanvasProps* props);

/* Default shape style */
void canvas_props_default_style(CanvasShapeStyle* style);

/* The stroke colour name of the panel matching name (static), or "fg":
 * colours of older notes ("danger", "muted") map to the panel's */
const gchar* canvas_props_color_name(const gchar* name);

/* Show the shape section with style selected; with_fill: the fill row
 * (closed shapes) */
void canvas_props_show_shape(CanvasProps* props, const CanvasShapeStyle* style, gboolean with_fill);

/* Show the text section */
void canvas_props_show_text(CanvasProps* props);

void canvas_props_hide(CanvasProps* props);

#endif /* CANVAS_PROPS_H */
