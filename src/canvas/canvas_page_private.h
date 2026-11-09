#ifndef CANVAS_PAGE_PRIVATE_H
#define CANVAS_PAGE_PRIVATE_H

/* Internal to the Canvas page files (canvas_view.c, canvas_page.c,
 * canvas_keys.c, canvas_drag.c, canvas_clip.c, canvas_menu.c, canvas_rotate.c): the page state and its core helpers,
 * defined in canvas_page.c. */

#include "core/app_types.h"
#include "canvas/canvas_format_bar.h"
#include "canvas/canvas_history.h"
#include "canvas/canvas_item.h"
#include "canvas/canvas_props.h"
#include "canvas/canvas_rotate.h"
#include "canvas/canvas_table_bar.h"
#include "canvas/canvas_toolbar.h"
#include "canvas/canvas_viewport.h"

#define CANVAS_PAGE_KEY "canvas-page"
#define CANVAS_NEW_OFFSET 24 /* offset from view corner or selection */

struct CanvasPage {
    AppData* app;
    gchar* dir;        /* note directory, NULL when nothing is open */
    GtkWidget* root;   /* notebook page */
    GtkWidget* title;     /* status bar label */
    GtkWidget* tab_label; /* notebook tab (referenced: kept while the workspace is hidden) */
    CanvasToolbar* toolbar;
    CanvasProps* props;
    CanvasFormatBar* format_bar;
    CanvasTableBar* table_bar;
    CanvasRotate* rotate; /* the handle that turns the selected shape */
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
    gint drag_sx, drag_sy; /* side of the resize handle (canvas_resize_box()) */
    gboolean drag_rotate; /* drag_item turned by its handle, from drag_angle */
    gdouble drag_angle;
    gint drag_x, drag_y, drag_w, drag_h;
    gchar* drag_before; /* page state when the drag began */
    GPtrArray* drag_group; /* CanvasItem*: the blocks moved together */
    GArray* drag_starts;   /* CanvasRect: their boxes when the drag began */
    /* Drag of a point of a selected line (drag_item): its index, the points
     * when the drag began (CanvasPoint, page coordinates; NULL: no point
     * dragged), added: a new point made by the drag of a segment middle */
    gint drag_point;
    GArray* drag_points;
    gboolean drag_point_added;
    /* The click gesture acted on this press (a double click): the drag
     * gesture, which sees it next, leaves it alone */
    gboolean press_taken;
    /* Blocks selected together (frames, referenced; Shift+click, rubber
     * band); the focused block is selected too */
    GPtrArray* selection;
    /* Rubber band: a press on the empty page (band_pending), then a drag
     * from (band_x0, band_y0) to (band_x1, band_y1); band: its widget on
     * the surface */
    gboolean band_pending;
    gboolean band_extend; /* Shift held: add to the selection */
    GtkWidget* band;
    gdouble band_x0, band_y0, band_x1, band_y1;
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

/* Selection: the focused block and the ones selected with it */
/* The selected blocks, in drawing order (free with g_ptr_array_unref) */
GPtrArray* canvas_page_selected(CanvasPage* p);
gboolean canvas_page_is_selected(CanvasPage* p, CanvasItem* it);
/* The selected blocks that are not locked (free with g_ptr_array_unref) */
GPtrArray* canvas_page_selected_unlocked(CanvasPage* p);
/* Ctrl+Shift+K: lock the selected blocks, or unlock them all if they are */
void canvas_page_toggle_lock(CanvasPage* p);
/* Groups: blocks selected and moved together. Ctrl+G: the selected blocks
 * make one group (the groups among them merged); Ctrl+Shift+G: they leave
 * their groups. can_group: 2 blocks or more, not all one group already;
 * can_ungroup: a selected block is in a group. */
gboolean canvas_page_can_group(CanvasPage* p);
gboolean canvas_page_can_ungroup(CanvasPage* p);
void canvas_page_group_selected(CanvasPage* p);
void canvas_page_ungroup_selected(CanvasPage* p);
void canvas_page_selection_clear(CanvasPage* p);
/* Shift+click on a block: add it to the selection, or take it out */
void canvas_page_select_toggle(CanvasPage* p, CanvasItem* it);
/* Rubber band: select the blocks inside (x, y, w, h); extend: keep the
 * blocks already selected */
void canvas_page_select_box(CanvasPage* p, gint x, gint y, gint w, gint h, gboolean extend);
/* Ctrl+A: select every block not locked */
void canvas_page_select_all(CanvasPage* p);
/* The focus moved to f: a block out of the selection ends it */
void canvas_page_focus_changed(CanvasPage* p, GtkWidget* f);
/* Box of a block on the page */
void canvas_page_item_box(CanvasItem* it, gint* x, gint* y, gint* w, gint* h);

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
/* The selected blocks as a document (free it), in drawing order */
CanvasDoc* canvas_page_selected_doc(CanvasPage* p);
/* Add copies of the blocks of doc, moved by (dx, dy), and select them:
 * new ids, the lines bound between them bound between the copies; pictures
 * of another note (from_dir) copied into this one. Returns how many. */
guint canvas_page_add_copies(CanvasPage* p, CanvasDoc* doc, gint dx, gint dy, const gchar* from_dir);

/* Delete / the arrows on a selected block: every selected block not locked */
void canvas_page_remove_selected(CanvasPage* p);
void canvas_page_move_selected(CanvasPage* p, gint dx, gint dy);
/* Drawing order of the selected blocks: 2 to the front, 1 one step up,
 * -1 one step down, -2 to the back (their own order kept) */
void canvas_page_reorder_selected(CanvasPage* p, gint where);
void canvas_page_apply_style(CanvasItem* it, const CanvasShapeStyle* style);
/* Where a new block goes: the pointer (at_pointer), below the selection, or
 * the view corner */
void canvas_page_new_position(CanvasPage* p, gboolean at_pointer, gint* x, gint* y);
CanvasItem* canvas_page_new_text_box(CanvasPage* p, gint x, gint y, const gchar* text);
CanvasItem* canvas_page_new_shape(CanvasPage* p, gint x, gint y, gint w, gint h, CanvasShapeType type);
CanvasItem* canvas_page_new_table(CanvasPage* p, gint x, gint y, gint rows, gint cols);
CanvasItem* canvas_page_new_picture(CanvasPage* p, const gchar* src, gint x, gint y, gint width, gint height);
void canvas_page_open_crop(CanvasPage* p, CanvasItem* it);

/* Tool, and the panel (shape style) or the format bar (text) showing what
 * it can change */
void canvas_page_set_tool(CanvasPage* p, CanvasTool tool);
void canvas_page_update_props(CanvasPage* p);

/* The format bar shows the styles at the cursor of the current text box */
void canvas_page_update_format(CanvasPage* p);

#endif /* CANVAS_PAGE_PRIVATE_H */
