#ifndef CANVAS_ITEM_H
#define CANVAS_ITEM_H

#include "canvas/canvas_doc.h"
#include "canvas/canvas_geometry.h"
#include <gtk/gtk.h>

#define ITEM_KEY "canvas-item"
#define MIN_TEXT_WIDTH 80
#define MIN_IMAGE_SIZE 24
#define MIN_SHAPE_SIZE 16
/* Class of the blocks selected with others (canvas_page.c) */
#define CANVAS_SELECTED_CLASS "canvas-selected"

typedef struct CanvasPage CanvasPage;

typedef struct {
    CanvasPage* page;
    CanvasBlockKind kind;
    gint id;            /* unique on the page (canvas_doc.h) */
    gint start_id, end_id; /* line or arrow: blocks its ends are bound to */
    gboolean locked;    /* kept in place (canvas_item_set_locked()) */
    gint group;         /* > 0: selected and moved with the blocks of the same group */
    GtkWidget* frame;   /* child of the surface */
    GtkWidget* content; /* text view, picture, shape or table */
    GtkTextBuffer* buffer;
    gchar* src;         /* picture, relative to note directory */
    gdouble crop_x, crop_y, crop_w, crop_h;
    /* Shape */
    CanvasShapeType shape_type;
    gchar* stroke_color;
    CanvasFillStyle fill_style;
    gint stroke_width;
    gboolean rough;
    GArray* points;
    gint active_point;  /* line or arrow: point clicked last (Delete removes it); -1: none */
    GtkWidget* label;   /* text view over a shape (not a line or a stroke) */
    /* Table */
    CanvasBlock* table_block;
    gint x, y, w, h;
    gdouble angle;      /* turned around its centre (canvas_item_rotatable()) */
    gdouble aspect;     /* width / height */
    guint pending;
    guint insert_styles;
} CanvasItem;

typedef void (*CanvasItemNotify)(CanvasPage* p);

CanvasItem* canvas_item_of(GtkWidget* w);
void canvas_item_select(CanvasItem* it);
/* Type in it: a text box, or the label of a shape */
void canvas_item_edit(CanvasItem* it);
/* No text selected in it any more (it left the selection) */
void canvas_item_clear_text_selection(CanvasItem* it);
/* Its frame put at (x, y) with its size, turned by angle around its centre */
void canvas_item_place(CanvasItem* it);

/* A rectangle, an ellipse or a diamond: it can be turned (lines and strokes
 * turn by their points, text boxes, pictures and tables stay straight) */
gboolean canvas_item_rotatable(const CanvasItem* it);

/* Box holding the block on the page, turned if it is */
void canvas_item_bounds(const CanvasItem* it, CanvasRect* out);

/* Lock it in place (its grip and corner hidden), or unlock it */
void canvas_item_set_locked(CanvasItem* it, gboolean locked);

/* hit (a widget picked on the page) is in the label being typed in: the
 * pointer places the cursor there instead of moving the shape */
gboolean canvas_item_in_label(CanvasItem* it, GtkWidget* hit);

/* w is a resize handle of a block, on side (*sx, *sy) (-1: left or top,
 * 0: middle, 1: right or bottom) */
gboolean canvas_item_resize_side(GtkWidget* w, gint* sx, gint* sy);

/* Label of a shape (free it); NULL when it has none */
gchar* canvas_item_label_text(const CanvasItem* it);

/* The note moved to dir: pictures are read from there */
void canvas_item_set_dir(CanvasItem* it, const gchar* dir);

CanvasItem* canvas_item_add(CanvasPage* p, GtkWidget* surface, const gchar* dir,
                            const CanvasBlock* b, CanvasItemNotify on_change,
                            gboolean loading);

/* The document of the blocks on surface, in drawing order (free it) */
CanvasDoc* canvas_items_to_doc(GtkWidget* surface);

#endif /* CANVAS_ITEM_H */
