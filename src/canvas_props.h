#ifndef CANVAS_PROPS_H
#define CANVAS_PROPS_H

/* Properties panel of the Canvas page, floating on the left of the page:
 * the shape style (stroke colour, fill, stroke width, hand-drawn or clean)
 * while a drawing tool is chosen or a shape is selected, and the align and
 * distribute buttons while several blocks are selected; hidden otherwise.
 * Text is formatted with the bar above the text box (canvas_format_bar.h). */
#include "canvas_doc.h"
#include "canvas_geometry.h"
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
    /* Align or distribute the selected blocks */
    void (*on_align)(CanvasAlignOp op, gpointer udata);
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

/* Hide the shape section */
void canvas_props_hide_shape(CanvasProps* props);

/* Show the align section for count selected blocks that can move: aligning
 * needs 2, distributing 3; hidden under 2 */
void canvas_props_show_align(CanvasProps* props, guint count);

#endif /* CANVAS_PROPS_H */
