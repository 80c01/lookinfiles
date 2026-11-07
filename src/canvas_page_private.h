#ifndef CANVAS_PAGE_PRIVATE_H
#define CANVAS_PAGE_PRIVATE_H

/* Internal to the Canvas page files (canvas_view.c, canvas_page.c,
 * canvas_keys.c, canvas_drag.c): the page state and its core helpers,
 * defined in canvas_page.c. */

#include "app_types.h"
#include "canvas_history.h"
#include "canvas_item.h"
#include "canvas_props.h"
#include "canvas_toolbar.h"
#include "canvas_viewport.h"

#define CANVAS_PAGE_KEY "canvas-page"
#define CANVAS_NEW_OFFSET 24 /* offset from view corner or selection */

struct CanvasPage {
    AppData* app;
    gchar* dir;        /* note directory, NULL when nothing is open */
    GtkWidget* root;   /* notebook page */
    GtkWidget* title;
    CanvasToolbar* toolbar;
    CanvasProps* props;
    CanvasViewport* viewport;
    GtkWidget* zoom_button; /* shows the scale, back to 100 % */
    GtkWidget* surface; /* GtkFixed */
    gboolean snap_grid;
    guint save_source;
    guint generation;  /* bumped on each note loaded */
    gboolean dirty;
    gboolean loading;
    gboolean closed;
    /* Tool, and the style of the next shapes */
    CanvasTool tool;
    CanvasShapeStyle style;
    /* Shape being drawn, from (draw_x0, draw_y0); eraser held down */
    CanvasItem* draw_item;
    gdouble draw_x0, draw_y0;
    gboolean erasing;
    /* Drag of a block */
    CanvasItem* drag_item;
    gboolean drag_resize;
    gint drag_x, drag_y, drag_w, drag_h;
    gchar* drag_before; /* page state when the drag began */
    CanvasHistory* history;
    /* Last focused widget inside a block (weak): what the toolbar acts on,
     * since its menus take the focus */
    GtkWidget* last_focus;
    gboolean focus_watched;
    /* Pointer coordinates over the page */
    gboolean pointer_in;
    gdouble pointer_x, pointer_y;
};

/* A new page (not yet built: canvas_view.c adds its widgets) */
CanvasPage* canvas_page_new(AppData* app);
void canvas_page_free(CanvasPage* p);

/* Focus */
GtkWidget* canvas_page_focused_widget(CanvasPage* p);
CanvasItem* canvas_page_focused_item(CanvasPage* p);
/* The focused block, or the one focused last if the focus is out of the
 * page (on the toolbar or one of its menus) */
CanvasItem* canvas_page_current_item(CanvasPage* p);
/* Give the focus back to the block after a toolbar action */
void canvas_page_restore_focus(CanvasPage* p);
void canvas_page_deselect(CanvasPage* p);
/* Select the next (step 1) or previous (-1) block after from */
void canvas_page_focus_next(CanvasPage* p, CanvasItem* from, gint step);

/* Changes: record the page for undo before, then save and resize after */
void canvas_page_remember(CanvasPage* p);
gchar* canvas_page_state(CanvasPage* p);
void canvas_page_update_extent(CanvasPage* p);
void canvas_page_schedule_save(CanvasPage* p);
gboolean canvas_page_save_now(CanvasPage* p);
void canvas_page_prune_media(CanvasPage* p);
void canvas_page_load(CanvasPage* p, const gchar* dir, const gchar* title);
/* Ctrl+Z (redo FALSE) / Ctrl+Shift+Z (TRUE) */
void canvas_page_undo(CanvasPage* p, gboolean redo);

/* Blocks */
void canvas_page_remove_item(CanvasItem* it);
void canvas_page_apply_style(CanvasItem* it, const CanvasShapeStyle* style);
/* Where a new block goes: the pointer (at_pointer), below the selection, or
 * the view corner */
void canvas_page_new_position(CanvasPage* p, gboolean at_pointer, gint* x, gint* y);
CanvasItem* canvas_page_new_text_box(CanvasPage* p, gint x, gint y, const gchar* text);
CanvasItem* canvas_page_new_shape(CanvasPage* p, gint x, gint y, gint w, gint h, CanvasShapeType type);
CanvasItem* canvas_page_new_table(CanvasPage* p, gint x, gint y, gint rows, gint cols);
CanvasItem* canvas_page_new_picture(CanvasPage* p, const gchar* src, gint x, gint y, gint width, gint height);
void canvas_page_open_crop(CanvasPage* p, CanvasItem* it);

/* Tool, and the properties panel showing what it can change */
void canvas_page_set_tool(CanvasPage* p, CanvasTool tool);
void canvas_page_update_props(CanvasPage* p);

#endif /* CANVAS_PAGE_PRIVATE_H */
