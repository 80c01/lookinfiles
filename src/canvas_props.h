#ifndef CANVAS_PROPS_H
#define CANVAS_PROPS_H

/* Properties panel of the Canvas page, floating on the left of the page:
 * the shape style (stroke colour, fill, stroke width, hand-drawn or clean)
 * while a drawing tool is chosen or a shape is selected, and the actions on
 * the selected blocks (align and distribute when there are several, drawing
 * order, lock, crop a picture) while blocks are selected and not edited;
 * hidden otherwise.
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
    /* Drawing order of the selection: 2 front, 1 forward, -1 backward, -2 back */
    void (*on_order)(gint where, gpointer udata);
    /* Lock or unlock the selected blocks */
    void (*on_lock)(gpointer udata);
    /* Crop the selected picture */
    void (*on_crop)(gpointer udata);
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

/* The selected blocks, for the selection section */
typedef struct {
    guint selected;   /* blocks selected (0: section hidden) */
    guint movable;    /* of them, not locked: aligning needs 2, distributing 3 */
    gboolean locked;  /* all locked: the lock button unlocks */
    gboolean picture; /* a single picture: the crop button shows */
} CanvasPropsSelection;

void canvas_props_show_selection(CanvasProps* props, const CanvasPropsSelection* sel);

#endif /* CANVAS_PROPS_H */
