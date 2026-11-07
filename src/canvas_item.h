#ifndef CANVAS_ITEM_H
#define CANVAS_ITEM_H

#include "canvas_doc.h"
#include <gtk/gtk.h>

#define ITEM_KEY "canvas-item"
#define MIN_TEXT_WIDTH 80
#define MIN_IMAGE_SIZE 24
#define MIN_SHAPE_SIZE 16

typedef struct CanvasPage CanvasPage;

typedef struct {
    CanvasPage* page;
    CanvasBlockKind kind;
    gint id;            /* unique on the page (canvas_doc.h) */
    gint start_id, end_id; /* line or arrow: blocks its ends are bound to */
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
    GtkWidget* label;   /* text view over a shape (not a line or a stroke) */
    /* Table */
    CanvasBlock* table_block;
    gint x, y, w, h;
    gdouble aspect;     /* width / height */
    guint pending;
    guint insert_styles;
} CanvasItem;

typedef void (*CanvasItemNotify)(CanvasPage* p);

CanvasItem* canvas_item_of(GtkWidget* w);
void canvas_item_select(CanvasItem* it);
/* Type in it: a text box, or the label of a shape */
void canvas_item_edit(CanvasItem* it);
void canvas_item_place(CanvasItem* it);

/* hit (a widget picked on the page) is in the label being typed in: the
 * pointer places the cursor there instead of moving the shape */
gboolean canvas_item_in_label(CanvasItem* it, GtkWidget* hit);

/* Label of a shape (free it); NULL when it has none */
gchar* canvas_item_label_text(const CanvasItem* it);
void canvas_item_raise(CanvasItem* it);

/* The note moved to dir: pictures are read from there */
void canvas_item_set_dir(CanvasItem* it, const gchar* dir);

CanvasItem* canvas_item_add(CanvasPage* p, GtkWidget* surface, const gchar* dir,
                            const CanvasBlock* b, CanvasItemNotify on_change,
                            gboolean loading);

/* The document of the blocks on surface, in drawing order (free it) */
CanvasDoc* canvas_items_to_doc(GtkWidget* surface);

#endif /* CANVAS_ITEM_H */
