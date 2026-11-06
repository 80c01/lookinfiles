#ifndef CANVAS_TOOLBAR_H
#define CANVAS_TOOLBAR_H

/* Tool bar of the Canvas page: a floating row of tool icons (select, shapes,
 * pencil, text, table, eraser) and a menu (snap to grid, align, crop). */
#include "canvas_doc.h"
#include "canvas_geometry.h"
#include <gtk/gtk.h>

typedef enum {
    CANVAS_TOOL_SELECT,
    CANVAS_TOOL_RECT,
    CANVAS_TOOL_DIAMOND,
    CANVAS_TOOL_ELLIPSE,
    CANVAS_TOOL_ARROW,
    CANVAS_TOOL_LINE,
    CANVAS_TOOL_PENCIL,
    CANVAS_TOOL_TEXT,
    CANVAS_TOOL_TABLE,
    CANVAS_TOOL_ERASER,
    CANVAS_TOOL_COUNT
} CanvasTool;

typedef struct CanvasToolbar CanvasToolbar;

typedef struct {
    void (*on_tool)(CanvasTool tool, gpointer udata);
    void (*on_crop_image)(gpointer udata);
    void (*on_toggle_grid)(gboolean enabled, gpointer udata);
    void (*on_align)(CanvasAlignOp op, gpointer udata);
} CanvasToolbarCallbacks;

CanvasToolbar* canvas_toolbar_new(const CanvasToolbarCallbacks* cbs, gpointer user_data);
GtkWidget* canvas_toolbar_get_widget(CanvasToolbar* tb);
void canvas_toolbar_free(CanvasToolbar* tb);

/* Show tool as the current one (no on_tool call) */
void canvas_toolbar_set_tool(CanvasToolbar* tb, CanvasTool tool);

/* Tool of a key typed on the page (no modifier), or -1 */
gint canvas_tool_for_key(guint keyval);

/* The tool draws a shape (shape tools and the pencil) */
gboolean canvas_tool_draws(CanvasTool tool);

/* Shape type drawn by a shape tool or the pencil */
CanvasShapeType canvas_tool_shape(CanvasTool tool);

#endif /* CANVAS_TOOLBAR_H */
