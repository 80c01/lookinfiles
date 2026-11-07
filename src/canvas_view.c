/* The Canvas page of the preview notebook (see canvas_view.h).
 * Edits a Canvas note: text boxes, pictures, shapes and tables on a page. */
#include "canvas_view.h"
#include "canvas_crop.h"
#include "canvas_doc.h"
#include "canvas_draw.h"
#include "canvas_geometry.h"
#include "canvas_history.h"
#include "canvas_item.h"
#include "canvas_media.h"
#include "canvas_props.h"
#include "canvas_text.h"
#include "canvas_toolbar.h"
#include "canvas_viewport.h"
#include "focus_route.h"
#include "fonts.h"
#include "icons.h"
#include "theme.h"

#include <string.h>

#define PAGE_KEY "canvas-page"
#define SAVE_DELAY_MS 800   /* autosave after the last change */
#define PAGE_MARGIN 400     /* room around blocks */
#define NEW_OFFSET 24       /* offset from view corner or selection */
#define NUDGE 8             /* arrows move a selected block by this */
#define NUDGE_BIG 40        /* Shift+arrows */
#define MAX_NEW_IMAGE_WIDTH 640
#define HISTORY_LIMIT 100   /* undo steps kept */

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

static void schedule_save(CanvasPage* p);
static gboolean save_now(CanvasPage* p);
static void focus_next_item(CanvasPage* p, CanvasItem* from, gint step);
static void update_props(CanvasPage* p);
static void set_tool(CanvasPage* p, CanvasTool tool);

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".canvas-page { background-color: @bg_view; }"
                  ".canvas-page .canvas-header { padding: 6px 10px; color: @fg; }"
                  ".canvas-page .canvas-title { font-weight: bold; }"
                  ".canvas-surface { background-color: @bg_view; }"
                  ".canvas-block { border: 1px solid transparent; border-radius: 4px; }"
                  ".canvas-block:hover { border-color: @border; }"
                  ".canvas-block:focus-within { border-color: @border_strong; }"
                  ".canvas-block:focus { border-color: @focus_ring; box-shadow: 0 0 0 1px @focus_ring; }"
                  ".canvas-grip { min-height: 8px; border-radius: 3px 3px 0 0; }"
                  ".canvas-block:hover .canvas-grip, .canvas-block:focus-within .canvas-grip,"
                  " .canvas-block:focus .canvas-grip { background-color: @bg_hover; }"
                  ".canvas-corner { min-width: 10px; min-height: 10px; border-radius: 2px; }"
                  ".canvas-block:hover .canvas-corner, .canvas-block:focus-within .canvas-corner,"
                  " .canvas-block:focus .canvas-corner { background-color: @border_strong; }"
                  ".canvas-text, .canvas-text text { background-color: transparent; color: @fg_text;"
                  " font-family: @font_ui; }"
                  ".canvas-text { padding: 2px 6px 6px 6px; }");
}

static GtkWidget* focused_widget(CanvasPage* p) {
    GtkRoot* root = gtk_widget_get_root(p->root);
    return root != NULL ? gtk_root_get_focus(root) : NULL;
}

static CanvasItem* focused_item(CanvasPage* p) {
    GtkWidget* f = focused_widget(p);
    return (f != NULL && gtk_widget_is_ancestor(f, p->surface)) ? canvas_item_of(f) : NULL;
}

/* The focused block, or the one focused last if the focus is out of the page
 * (on the toolbar or one of its menus) */
static CanvasItem* current_item(CanvasPage* p) {
    CanvasItem* it = focused_item(p);
    if (it != NULL) return it;
    if (p->last_focus != NULL && gtk_widget_is_ancestor(p->last_focus, p->surface)) {
        return canvas_item_of(p->last_focus);
    }
    return NULL;
}

static CanvasItem* current_text_item(CanvasPage* p) {
    CanvasItem* it = current_item(p);
    return (it != NULL && it->kind == CANVAS_BLOCK_TEXT) ? it : NULL;
}

/* Give the focus back to the block after a toolbar action */
static void restore_focus(CanvasPage* p) {
    if (p->last_focus != NULL && gtk_widget_is_ancestor(p->last_focus, p->surface)) {
        gtk_widget_grab_focus(p->last_focus);
    }
}

static void on_window_focus(GObject* window, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    CanvasPage* p = g_object_get_data(G_OBJECT(data), PAGE_KEY);
    if (p == NULL || p->closed) return;
    GtkWidget* f = gtk_root_get_focus(GTK_ROOT(window));
    if (f == NULL || !gtk_widget_is_ancestor(f, p->root)) canvas_viewport_set_pan_key(p->viewport, FALSE);
    if (f == p->surface) {
        g_clear_weak_pointer(&p->last_focus);
    } else if (f != NULL && gtk_widget_is_ancestor(f, p->surface)) {
        g_set_weak_pointer(&p->last_focus, f);
    }
    update_props(p);
}

static void on_root_map(GtkWidget* widget, gpointer data) {
    CanvasPage* p = data;
    GtkRoot* window = gtk_widget_get_root(widget);
    if (p->focus_watched || window == NULL) return;
    p->focus_watched = TRUE;
    g_signal_connect_object(window, "notify::focus-widget", G_CALLBACK(on_window_focus), p->root, 0);
}

static void deselect(CanvasPage* p) {
    g_clear_weak_pointer(&p->last_focus);
    GtkRoot* root = gtk_widget_get_root(p->surface);
    if (root != NULL) gtk_root_set_focus(root, p->surface);
}

static void update_extent(CanvasPage* p) {
    gint w = 0, h = 0;
    for (GtkWidget* c = gtk_widget_get_first_child(p->surface); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        CanvasItem* it = canvas_item_of(c);
        if (it == NULL) continue;
        w = MAX(w, it->x + MAX(it->w, gtk_widget_get_width(c)));
        h = MAX(h, it->y + MAX(it->h, gtk_widget_get_height(c)));
    }
    gtk_widget_set_size_request(p->surface, w + PAGE_MARGIN, h + PAGE_MARGIN);
}

/* Page state, as page.xml (free it) */
static gchar* page_state(CanvasPage* p) {
    g_autoptr(CanvasDoc) doc = canvas_items_to_doc(p->surface);
    return canvas_doc_to_xml(doc);
}

/* Record the page before a change, for undo */
static void remember(CanvasPage* p) {
    if (p->loading || p->dir == NULL) return;
    gchar* state = page_state(p);
    canvas_history_push(p->history, state);
    g_free(state);
}

/* Text typed or a table cell edited: their own widgets undo it */
static void on_item_changed_notify(CanvasPage* p) {
    if (!p->loading) canvas_history_clear_redo(p->history);
    update_extent(p);
    schedule_save(p);
}

static CanvasItem* add_item(CanvasPage* p, const CanvasBlock* b) {
    return canvas_item_add(p, p->surface, p->dir, b, on_item_changed_notify, p->loading);
}

static void remove_item(CanvasItem* it) {
    CanvasPage* p = it->page;
    remember(p);
    gtk_fixed_remove(GTK_FIXED(p->surface), it->frame);
    schedule_save(p);
}

static void new_block_position(CanvasPage* p, gboolean at_pointer, gint* x, gint* y) {
    CanvasItem* sel = focused_item(p);
    if (at_pointer && p->pointer_in) {
        *x = (gint)p->pointer_x;
        *y = (gint)p->pointer_y;
    } else if (sel != NULL) {
        *x = sel->x;
        *y = sel->y + MAX(sel->h, gtk_widget_get_height(sel->frame)) + NEW_OFFSET;
    } else {
        canvas_viewport_origin(p->viewport, x, y);
        *x += NEW_OFFSET;
        *y += NEW_OFFSET;
    }
}

static CanvasItem* new_text_box(CanvasPage* p, gint x, gint y, const gchar* text) {
    remember(p);
    g_autoptr(CanvasDoc) tmp = canvas_doc_new();
    CanvasItem* it = add_item(p, canvas_doc_add_text(tmp, x, y, 0, text));
    update_extent(p);
    schedule_save(p);
    canvas_item_edit(it);
    return it;
}

static void apply_style(CanvasItem* it, const CanvasShapeStyle* style) {
    g_free(it->stroke_color);
    it->stroke_color = g_strdup(style->stroke_color);
    it->fill_style = style->fill;
    it->stroke_width = style->stroke_width;
    it->rough = style->rough;
    if (it->content != NULL) gtk_widget_queue_draw(it->content);
}

static CanvasItem* new_shape(CanvasPage* p, gint x, gint y, gint w, gint h, CanvasShapeType type) {
    remember(p);
    g_autoptr(CanvasDoc) tmp = canvas_doc_new();
    CanvasBlock* b = canvas_doc_add_shape(tmp, x, y, w, h, type);
    CanvasItem* it = add_item(p, b);
    apply_style(it, &p->style);
    update_extent(p);
    schedule_save(p);
    canvas_item_select(it);
    return it;
}

static CanvasItem* new_table(CanvasPage* p, gint x, gint y, gint rows, gint cols) {
    remember(p);
    g_autoptr(CanvasDoc) tmp = canvas_doc_new();
    CanvasBlock* b = canvas_doc_add_table(tmp, x, y, rows, cols);
    CanvasItem* it = add_item(p, b);
    update_extent(p);
    schedule_save(p);
    canvas_item_select(it);
    return it;
}

static CanvasItem* new_picture(CanvasPage* p, const gchar* src, gint x, gint y, gint width, gint height) {
    if (width <= 0 || height <= 0) return NULL;
    if (width > MAX_NEW_IMAGE_WIDTH) {
        height = (gint)((gdouble)height * MAX_NEW_IMAGE_WIDTH / width);
        width = MAX_NEW_IMAGE_WIDTH;
    }
    remember(p);
    g_autoptr(CanvasDoc) tmp = canvas_doc_new();
    CanvasBlock* b = canvas_doc_add_image(tmp, x, y, width, height, src);
    CanvasItem* it = add_item(p, b);
    update_extent(p);
    schedule_save(p);
    canvas_item_select(it);
    return it;
}

/* ------------------------------------------------------------------------- */
/* Document Sync                                                             */
/* ------------------------------------------------------------------------- */

static gboolean save_now(CanvasPage* p) {
    if (p->save_source != 0) {
        g_source_remove(p->save_source);
        p->save_source = 0;
    }
    if (!p->dirty || p->dir == NULL || p->closed) return TRUE;
    g_autoptr(CanvasDoc) doc = canvas_items_to_doc(p->surface);
    g_autoptr(GError) error = NULL;
    if (!canvas_doc_save(doc, p->dir, &error)) {
        g_warning("canvas: cannot save %s: %s", p->dir, error->message);
        return FALSE;
    }
    p->dirty = FALSE;
    update_extent(p);
    return TRUE;
}

static gboolean on_save_timeout(gpointer data) {
    CanvasPage* p = data;
    p->save_source = 0;
    save_now(p);
    return G_SOURCE_REMOVE;
}

static void schedule_save(CanvasPage* p) {
    if (p->loading || p->dir == NULL) return;
    p->dirty = TRUE;
    if (p->save_source != 0) g_source_remove(p->save_source);
    p->save_source = g_timeout_add(SAVE_DELAY_MS, on_save_timeout, p);
}

/* Delete the pictures no block uses, once the history is dropped */
static void prune_media(CanvasPage* p) {
    if (p->dir == NULL || p->closed) return;
    g_autoptr(CanvasDoc) doc = canvas_items_to_doc(p->surface);
    canvas_doc_prune_media(doc, p->dir);
}

static void clear_surface(CanvasPage* p) {
    g_clear_weak_pointer(&p->last_focus);
    GtkWidget* c;
    while ((c = gtk_widget_get_first_child(p->surface)) != NULL) {
        gtk_fixed_remove(GTK_FIXED(p->surface), c);
    }
}

static void load(CanvasPage* p, const gchar* dir, const gchar* title) {
    save_now(p);
    prune_media(p);
    canvas_history_clear(p->history);
    p->generation++;
    p->loading = TRUE;
    clear_surface(p);
    g_free(p->dir);
    p->dir = g_strdup(dir);
    gtk_label_set_text(GTK_LABEL(p->title), title != NULL ? title : "");

    g_autoptr(GError) error = NULL;
    g_autoptr(CanvasDoc) doc = canvas_doc_load(dir, &error);
    if (doc == NULL) {
        g_warning("canvas: cannot read %s: %s", dir, error != NULL ? error->message : "");
    }
    for (guint i = 0; doc != NULL && i < doc->blocks->len; i++) {
        add_item(p, g_ptr_array_index(doc->blocks, i));
    }
    p->loading = FALSE;
    p->dirty = FALSE;
    update_extent(p);
    canvas_viewport_reset(p->viewport);
    set_tool(p, CANVAS_TOOL_SELECT);
}

/* Put the page back in a state from the history (state freed) */
static void restore(CanvasPage* p, gchar* state) {
    if (state == NULL) return;
    g_autoptr(GError) error = NULL;
    g_autoptr(CanvasDoc) doc = canvas_doc_parse(state, -1, &error);
    g_free(state);
    if (doc == NULL) {
        g_warning("canvas: cannot restore the page: %s", error != NULL ? error->message : "");
        return;
    }
    p->loading = TRUE;
    clear_surface(p);
    for (guint i = 0; i < doc->blocks->len; i++) {
        add_item(p, g_ptr_array_index(doc->blocks, i));
    }
    p->loading = FALSE;
    update_extent(p);
    schedule_save(p);
    deselect(p);
    update_props(p);
}

static void undo(CanvasPage* p, gboolean redo) {
    if (p->drag_item != NULL || p->draw_item != NULL || p->erasing) return;
    if (!(redo ? canvas_history_can_redo(p->history) : canvas_history_can_undo(p->history))) return;
    gchar* now = page_state(p);
    restore(p, redo ? canvas_history_redo(p->history, now) : canvas_history_undo(p->history, now));
    g_free(now);
}

/* ------------------------------------------------------------------------- */
/* Tools                                                                     */
/* ------------------------------------------------------------------------- */

static gboolean closed_shape(CanvasShapeType type) {
    return type != CANVAS_SHAPE_LINE && type != CANVAS_SHAPE_ARROW && type != CANVAS_SHAPE_FREEHAND;
}

/* The panel shows the style of the next shapes while drawing, else what the
 * current block can change */
static void update_props(CanvasPage* p) {
    if (p->props == NULL) return;
    if (canvas_tool_draws(p->tool)) {
        canvas_props_show_shape(p->props, &p->style, closed_shape(canvas_tool_shape(p->tool)));
        return;
    }
    CanvasItem* it = p->tool == CANVAS_TOOL_SELECT ? current_item(p) : NULL;
    if (it != NULL && it->kind == CANVAS_BLOCK_SHAPE) {
        CanvasShapeStyle style = {it->stroke_color, it->fill_style, it->stroke_width, it->rough};
        canvas_props_show_shape(p->props, &style, closed_shape(it->shape_type));
    } else if (it != NULL && it->kind == CANVAS_BLOCK_TEXT) {
        canvas_props_show_text(p->props);
    } else {
        canvas_props_hide(p->props);
    }
}

static void set_tool(CanvasPage* p, CanvasTool tool) {
    p->tool = tool;
    canvas_toolbar_set_tool(p->toolbar, tool);
    const gchar* cursor = NULL;
    if (canvas_tool_draws(tool) || tool == CANVAS_TOOL_ERASER || tool == CANVAS_TOOL_TABLE) {
        cursor = "crosshair";
    } else if (tool == CANVAS_TOOL_TEXT) {
        cursor = "text";
    }
    gtk_widget_set_cursor_from_name(p->surface, cursor);
    update_props(p);
}

/* Eraser: remove the block under (x, y) */
static void erase_at(CanvasPage* p, gdouble x, gdouble y) {
    CanvasItem* it = canvas_item_of(gtk_widget_pick(p->surface, x, y, GTK_PICK_DEFAULT));
    if (it == NULL) return;
    remove_item(it);
    update_extent(p);
}

/* A press on the page with another tool than Select: start a shape, place
 * a text box or a table, erase */
static void tool_begin(CanvasPage* p, gdouble x, gdouble y) {
    if (canvas_tool_draws(p->tool)) {
        CanvasItem* it = new_shape(p, (gint)x, (gint)y, MIN_SHAPE_SIZE, MIN_SHAPE_SIZE, canvas_tool_shape(p->tool));
        p->draw_item = it;
        p->draw_x0 = x;
        p->draw_y0 = y;
        if (p->tool == CANVAS_TOOL_PENCIL) {
            canvas_draw_add_point(it, x, y);
        } else {
            canvas_draw_update(it, x, y, x, y, FALSE);
        }
    } else if (p->tool == CANVAS_TOOL_TEXT) {
        set_tool(p, CANVAS_TOOL_SELECT);
        new_text_box(p, (gint)x, (gint)y, "");
    } else if (p->tool == CANVAS_TOOL_TABLE) {
        set_tool(p, CANVAS_TOOL_SELECT);
        new_table(p, (gint)x, (gint)y, 3, 3);
    } else if (p->tool == CANVAS_TOOL_ERASER) {
        p->erasing = TRUE;
        erase_at(p, x, y);
    }
}

static void tool_update(CanvasPage* p, GtkGesture* gesture, gdouble x, gdouble y) {
    if (p->erasing) {
        erase_at(p, x, y);
    } else if (p->draw_item != NULL && p->tool == CANVAS_TOOL_PENCIL) {
        canvas_draw_add_point(p->draw_item, x, y);
    } else if (p->draw_item != NULL) {
        GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));
        canvas_draw_update(p->draw_item, p->draw_x0, p->draw_y0, x, y, (state & GDK_SHIFT_MASK) != 0);
    }
}

static void tool_end(CanvasPage* p) {
    p->erasing = FALSE;
    CanvasItem* it = p->draw_item;
    if (it == NULL) return;
    p->draw_item = NULL;
    canvas_draw_finish(it);
    update_extent(p);
    schedule_save(p);
    /* The pencil stays, to go on drawing; the other tools give the new shape
     * to Select */
    if (p->tool != CANVAS_TOOL_PENCIL) set_tool(p, CANVAS_TOOL_SELECT);
    canvas_item_select(it);
}

/* ------------------------------------------------------------------------- */
/* Drag and Magnetism                                                        */
/* ------------------------------------------------------------------------- */

static void on_drag_begin(GtkGestureDrag* gesture, gdouble x, gdouble y, gpointer data) {
    CanvasPage* p = data;
    if (p->tool != CANVAS_TOOL_SELECT) {
        if (p->dir == NULL) return;
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
        tool_begin(p, x, y);
        return;
    }
    GtkWidget* hit = gtk_widget_pick(p->surface, x, y, GTK_PICK_DEFAULT);
    CanvasItem* it = canvas_item_of(hit);
    gboolean grip = FALSE, corner = FALSE;
    for (GtkWidget* w = hit; w != NULL && it != NULL && w != it->frame; w = gtk_widget_get_parent(w)) {
        if (g_object_get_data(G_OBJECT(w), "canvas-grip") != NULL) grip = TRUE;
        if (g_object_get_data(G_OBJECT(w), "canvas-corner") != NULL) corner = TRUE;
    }
    gboolean move = grip || (it != NULL && (it->kind == CANVAS_BLOCK_IMAGE || it->kind == CANVAS_BLOCK_SHAPE) && !corner);
    if (it == NULL || (!move && !corner)) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    g_free(p->drag_before);
    p->drag_before = page_state(p);
    p->drag_item = it;
    p->drag_resize = corner;
    p->drag_x = it->x;
    p->drag_y = it->y;
    p->drag_w = (it->kind == CANVAS_BLOCK_TEXT || it->kind == CANVAS_BLOCK_TABLE) ?
                MAX(it->w, gtk_widget_get_width(it->frame)) : it->w;
    p->drag_h = it->h;
    canvas_item_raise(it);
    canvas_item_select(it);
}

static void on_drag_update(GtkGestureDrag* gesture, gdouble dx, gdouble dy, gpointer data) {
    CanvasPage* p = data;
    if (p->draw_item != NULL || p->erasing) {
        gdouble x0, y0;
        gtk_gesture_drag_get_start_point(gesture, &x0, &y0);
        tool_update(p, GTK_GESTURE(gesture), x0 + dx, y0 + dy);
        return;
    }
    CanvasItem* it = p->drag_item;
    if (it == NULL) return;

    if (!p->drag_resize) {
        gint new_x = p->drag_x + (gint)dx;
        gint new_y = p->drag_y + (gint)dy;

        GArray* others = g_array_new(FALSE, FALSE, sizeof(CanvasRect));
        for (GtkWidget* c = gtk_widget_get_first_child(p->surface); c != NULL; c = gtk_widget_get_next_sibling(c)) {
            CanvasItem* o = canvas_item_of(c);
            if (o != NULL && o != it) {
                CanvasRect r = { o->x, o->y, o->w, MAX(o->h, gtk_widget_get_height(o->frame)) };
                g_array_append_val(others, r);
            }
        }
        CanvasGuides guides = {0};
        canvas_snap_box(&new_x, &new_y, it->w, MAX(it->h, gtk_widget_get_height(it->frame)),
                        (const CanvasRect*)others->data, others->len,
                        p->snap_grid, CANVAS_DEFAULT_GRID, &guides);
        g_array_unref(others);

        it->x = new_x;
        it->y = new_y;
    } else if (it->kind == CANVAS_BLOCK_TEXT || it->kind == CANVAS_BLOCK_TABLE) {
        it->w = MAX(p->drag_w + (gint)dx, MIN_TEXT_WIDTH);
    } else if (it->kind == CANVAS_BLOCK_SHAPE) {
        it->w = MAX(p->drag_w + (gint)dx, MIN_SHAPE_SIZE);
        it->h = MAX(p->drag_h + (gint)dy, MIN_SHAPE_SIZE);
    } else {
        it->w = MAX(p->drag_w + (gint)dx, MIN_IMAGE_SIZE);
        it->h = MAX((gint)(it->w / it->aspect), MIN_IMAGE_SIZE);
    }
    canvas_item_place(it);
}

static void on_drag_end(GtkGestureDrag* gesture, gdouble dx, gdouble dy, gpointer data) {
    (void)gesture;
    CanvasPage* p = data;
    if (p->draw_item != NULL || p->erasing) {
        tool_end(p);
        return;
    }
    if (p->drag_item == NULL) return;
    if (dx != 0 || dy != 0) {
        canvas_history_push(p->history, p->drag_before);
        update_extent(p);
        schedule_save(p);
    }
    p->drag_item = NULL;
    g_clear_pointer(&p->drag_before, g_free);
}

/* user_data: a reference on the picture's frame, so the item stays alive
 * while the dialog is open; ignored if the picture left the page meanwhile */
static void on_crop_applied(double x, double y, double w, double h, gpointer user_data) {
    GtkWidget* frame = user_data;
    CanvasItem* it = canvas_item_of(frame);
    if (it == NULL || gtk_widget_get_parent(frame) == NULL) return;
    remember(it->page);
    it->crop_x = x;
    it->crop_y = y;
    it->crop_w = w;
    it->crop_h = h;
    gtk_widget_queue_draw(it->content);
    update_extent(it->page);
    schedule_save(it->page);
}

static void open_crop_dialog(CanvasPage* p, CanvasItem* it) {
    if (it == NULL || it->kind != CANVAS_BLOCK_IMAGE || it->src == NULL || p->dir == NULL) return;
    GtkRoot* root = gtk_widget_get_root(p->root);
    if (root == NULL) return;
    gchar* full = g_build_filename(p->dir, it->src, NULL);
    canvas_crop_dialog_show(GTK_WINDOW(root), full, it->crop_x, it->crop_y, it->crop_w, it->crop_h,
                            on_crop_applied, g_object_ref(it->frame), g_object_unref);
    g_free(full);
}

static void on_page_pressed(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    CanvasPage* p = data;
    if (p->tool != CANVAS_TOOL_SELECT) return; /* the tool took the press */
    GtkWidget* hit = gtk_widget_pick(p->surface, x, y, GTK_PICK_DEFAULT);
    CanvasItem* it = canvas_item_of(hit);
    if (it != NULL) {
        if (n_press == 2 && it->kind == CANVAS_BLOCK_IMAGE) {
            open_crop_dialog(p, it);
        }
        return;
    }
    if (p->dir == NULL) return;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    if (n_press == 2) {
        new_text_box(p, (gint)x, (gint)y, "");
    } else {
        deselect(p);
    }
}

static void on_motion(GtkEventControllerMotion* ctrl, gdouble x, gdouble y, gpointer data) {
    (void)ctrl;
    CanvasPage* p = data;
    p->pointer_in = TRUE;
    p->pointer_x = x;
    p->pointer_y = y;
}

static void on_leave(GtkEventControllerMotion* ctrl, gpointer data) {
    (void)ctrl;
    ((CanvasPage*)data)->pointer_in = FALSE;
}

static void on_new_picture_cb(CanvasPage* p, const gchar* src, gint x, gint y, gint w, gint h) {
    new_picture(p, src, x, y, w, h);
}

static gboolean on_drop(GtkDropTarget* target, const GValue* value, gdouble x, gdouble y, gpointer data) {
    (void)target;
    CanvasPage* p = data;
    return canvas_media_drop_files(value, p->dir, (gint)x, (gint)y, NEW_OFFSET,
                                   on_new_picture_cb, p);
}

/* ------------------------------------------------------------------------- */
/* Keys                                                                      */
/* ------------------------------------------------------------------------- */

static void focus_next_item(CanvasPage* p, CanvasItem* from, gint step) {
    GtkWidget* start = from != NULL ? from->frame : NULL;
    GtkWidget* w = start;
    do {
        if (w == NULL) {
            w = step > 0 ? gtk_widget_get_first_child(p->surface) : gtk_widget_get_last_child(p->surface);
        } else {
            w = step > 0 ? gtk_widget_get_next_sibling(w) : gtk_widget_get_prev_sibling(w);
        }
        if (w != NULL && canvas_item_of(w) != NULL) {
            canvas_item_select(canvas_item_of(w));
            return;
        }
    } while (w != start && !(w == NULL && start == NULL));
}

static gboolean text_key(CanvasItem* it, guint keyval, GdkModifierType state) {
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    gboolean alt = (state & GDK_ALT_MASK) != 0;

    if (keyval == GDK_KEY_Escape && !ctrl && !alt) {
        canvas_item_select(it);
        return TRUE;
    }
    if ((keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab) && !ctrl && !alt) {
        focus_next_item(it->page, it, keyval == GDK_KEY_Tab ? 1 : -1);
        return TRUE;
    }
    return canvas_text_key(GTK_TEXT_VIEW(it->content), &it->pending, keyval, state);
}

static gboolean block_key(CanvasPage* p, CanvasItem* it, guint keyval, GdkModifierType state) {
    if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) != 0) return FALSE;
    gint step = (state & GDK_SHIFT_MASK) ? NUDGE_BIG : NUDGE;
    if (keyval == GDK_KEY_Left || keyval == GDK_KEY_Right || keyval == GDK_KEY_Up || keyval == GDK_KEY_Down) {
        remember(p);
    }
    switch (keyval) {
    case GDK_KEY_Delete:
    case GDK_KEY_KP_Delete:
    case GDK_KEY_BackSpace:
        remove_item(it);
        deselect(p);
        return TRUE;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
        canvas_item_edit(it);
        return TRUE;
    case GDK_KEY_Escape:
        deselect(p);
        return TRUE;
    case GDK_KEY_Tab:
        focus_next_item(p, it, 1);
        return TRUE;
    case GDK_KEY_ISO_Left_Tab:
        focus_next_item(p, it, -1);
        return TRUE;
    case GDK_KEY_Left: it->x -= step; break;
    case GDK_KEY_Right: it->x += step; break;
    case GDK_KEY_Up: it->y -= step; break;
    case GDK_KEY_Down: it->y += step; break;
    default: return FALSE;
    }
    canvas_item_place(it);
    update_extent(p);
    schedule_save(p);
    return TRUE;
}

/* A picture being read from the clipboard; the page root is kept alive
 * meanwhile, and the picture dropped if another note was opened */
typedef struct {
    GtkWidget* root;
    guint generation;
    gint x, y;
} PasteJob;

static void on_paste_read(GObject* source, GAsyncResult* result, gpointer data) {
    PasteJob* job = data;
    g_autoptr(GError) error = NULL;
    GdkTexture* texture = gdk_clipboard_read_texture_finish(GDK_CLIPBOARD(source), result, &error);
    CanvasPage* p = g_object_get_data(G_OBJECT(job->root), PAGE_KEY);
    if (texture != NULL && p != NULL && !p->closed && p->dir != NULL && p->generation == job->generation) {
        gchar* src = canvas_media_save_texture(texture, p->dir);
        if (src != NULL) {
            new_picture(p, src, job->x, job->y, gdk_texture_get_width(texture), gdk_texture_get_height(texture));
        } else {
            g_warning("canvas: cannot save the pasted picture in %s", p->dir);
        }
        g_free(src);
    }
    if (texture != NULL) g_object_unref(texture);
    g_object_unref(job->root);
    g_free(job);
}

/* Ctrl+V with a picture in the clipboard: new picture. Text pasted in a
 * text box or a table cell stays text. */
static gboolean on_paste_picture(CanvasPage* p) {
    GdkClipboard* clip = gtk_widget_get_clipboard(p->root);
    if (!canvas_media_clipboard_has_picture(clip)) return FALSE;
    CanvasItem* it = focused_item(p);
    if (it != NULL && focused_widget(p) != it->frame && canvas_media_clipboard_has_text(clip)) return FALSE;
    PasteJob* job = g_new0(PasteJob, 1);
    job->root = g_object_ref(p->root);
    job->generation = p->generation;
    new_block_position(p, TRUE, &job->x, &job->y);
    gdk_clipboard_read_texture_async(clip, NULL, on_paste_read, job);
    return TRUE;
}

/* Ctrl+= / Ctrl+- / Ctrl+0: zoom in / out / back to 100 % (the window's
 * font sizes elsewhere: canvas_view_claims_key()) */
static gint zoom_key_step(guint keyval, GdkModifierType state) {
    if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) != GDK_CONTROL_MASK) return G_MININT;
    gboolean shift = (state & GDK_SHIFT_MASK) != 0;
    if (keyval == GDK_KEY_equal || keyval == GDK_KEY_plus || keyval == GDK_KEY_KP_Add) return 1;
    if (!shift && (keyval == GDK_KEY_minus || keyval == GDK_KEY_KP_Subtract)) return -1;
    if (!shift && (keyval == GDK_KEY_0 || keyval == GDK_KEY_KP_0)) return 0;
    return G_MININT;
}

static gboolean zoom_key(CanvasPage* p, guint keyval, GdkModifierType state) {
    gint step = zoom_key_step(keyval, state);
    if (step == G_MININT) return FALSE;
    canvas_viewport_zoom_step(p->viewport, step);
    return TRUE;
}

static void on_key_released(GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType state,
                            gpointer data) {
    (void)ctrl;
    (void)keycode;
    (void)state;
    if (keyval == GDK_KEY_space) canvas_viewport_set_pan_key(((CanvasPage*)data)->viewport, FALSE);
}

static gboolean on_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType state, gpointer data) {
    (void)ctrl;
    (void)keycode;
    CanvasPage* p = data;
    if (p->dir == NULL) return FALSE;
    gboolean control = (state & GDK_CONTROL_MASK) != 0;
    gboolean other = (state & (GDK_SHIFT_MASK | GDK_ALT_MASK)) != 0;
    if (control && !other && (keyval == GDK_KEY_v || keyval == GDK_KEY_V)) {
        return on_paste_picture(p);
    }
    if (control && !other && (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter)) {
        gint x, y;
        new_block_position(p, FALSE, &x, &y);
        new_text_box(p, x, y, "");
        return TRUE;
    }
    /* Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y: undo / redo on the page; while typing,
     * the text box or the table cell undoes its own text */
    GtkWidget* f = focused_widget(p);
    gboolean typing = f != NULL && (GTK_IS_EDITABLE(f) || GTK_IS_TEXT_VIEW(f));
    if (control && (state & GDK_ALT_MASK) == 0 && !typing &&
        (keyval == GDK_KEY_z || keyval == GDK_KEY_Z || keyval == GDK_KEY_y || keyval == GDK_KEY_Y)) {
        gboolean shift = (state & GDK_SHIFT_MASK) != 0;
        if (keyval == GDK_KEY_y || keyval == GDK_KEY_Y) {
            if (shift) return FALSE;
            shift = TRUE;
        }
        undo(p, shift);
        return TRUE;
    }
    if (zoom_key(p, keyval, state)) return TRUE;
    CanvasItem* it = focused_item(p);
    /* Space held on the page or a block: drag to move the view */
    if (keyval == GDK_KEY_space && (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK)) == 0 &&
        (f == p->surface || (it != NULL && f == it->frame))) {
        canvas_viewport_set_pan_key(p->viewport, TRUE);
        return TRUE;
    }
    /* Not typing (the page or a block focused): a letter picks a tool, Esc
     * goes back to Select */
    if ((it == NULL || focused_widget(p) == it->frame) && (state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) == 0 &&
        (it != NULL || focused_widget(p) == p->surface)) {
        if (keyval == GDK_KEY_Escape && p->tool != CANVAS_TOOL_SELECT) {
            set_tool(p, CANVAS_TOOL_SELECT);
            return TRUE;
        }
        gint tool = canvas_tool_for_key(keyval);
        if (tool >= 0) {
            set_tool(p, (CanvasTool)tool);
            return TRUE;
        }
    }
    if (it == NULL) return FALSE;
    if (focused_widget(p) == it->frame) return block_key(p, it, keyval, state);
    if (it->kind == CANVAS_BLOCK_TEXT && gtk_widget_has_focus(it->content)) {
        return text_key(it, keyval, state);
    }
    /* Typing inside a block (a table cell): keys are its own, Esc selects it */
    if (keyval == GDK_KEY_Escape && (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK)) == 0) {
        canvas_item_select(it);
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------------- */
/* Toolbar Callbacks (a text change saves the page by itself)                */
/* ------------------------------------------------------------------------- */

static void on_toolbar_tool(CanvasTool tool, gpointer udata) {
    set_tool(udata, tool);
}

static void on_toolbar_crop(gpointer udata) {
    CanvasPage* p = udata;
    open_crop_dialog(p, current_item(p));
}

static void on_props_format_style(guint style_flag, gpointer udata) {
    CanvasItem* it = current_text_item(udata);
    if (it != NULL) canvas_text_toggle_style(it->buffer, &it->pending, style_flag);
    restore_focus(udata);
}

static void on_props_format_preset(CanvasPreset preset, gpointer udata) {
    CanvasItem* it = current_text_item(udata);
    if (it != NULL) canvas_text_apply_preset(it->buffer, preset);
    restore_focus(udata);
}

static void on_props_format_list(const gchar* prefix, gpointer udata) {
    CanvasItem* it = current_text_item(udata);
    if (it != NULL) canvas_text_insert_list(it->buffer, prefix);
    restore_focus(udata);
}

static void on_props_format_indent(gint step, gpointer udata) {
    CanvasItem* it = current_text_item(udata);
    if (it != NULL) canvas_text_indent(it->buffer, step);
    restore_focus(udata);
}

static void on_props_format_checkbox(gpointer udata) {
    CanvasItem* it = current_text_item(udata);
    if (it != NULL) canvas_text_toggle_checkbox(it->buffer);
    restore_focus(udata);
}

/* A style picked in the panel: for the next shapes, and the selected one */
static void on_props_shape_style(const CanvasShapeStyle* style, gpointer udata) {
    CanvasPage* p = udata;
    p->style = *style;
    if (canvas_tool_draws(p->tool)) return;
    CanvasItem* it = current_item(p);
    if (it == NULL || it->kind != CANVAS_BLOCK_SHAPE) return;
    remember(p);
    apply_style(it, style);
    schedule_save(p);
    restore_focus(p);
}

static void on_toolbar_toggle_grid(gboolean enabled, gpointer udata) {
    ((CanvasPage*)udata)->snap_grid = enabled;
}

static void on_toolbar_align(CanvasAlignOp op, gpointer udata) {
    CanvasPage* p = udata;
    GArray* items = g_array_new(FALSE, FALSE, sizeof(CanvasItem*));
    GArray* rects = g_array_new(FALSE, FALSE, sizeof(CanvasRect));
    for (GtkWidget* c = gtk_widget_get_first_child(p->surface); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        CanvasItem* it = canvas_item_of(c);
        if (it == NULL) continue;
        CanvasRect r = { it->x, it->y, it->w, MAX(it->h, gtk_widget_get_height(it->frame)) };
        g_array_append_val(items, it);
        g_array_append_val(rects, r);
    }
    if (rects->len >= 2) {
        remember(p);
        canvas_align_boxes((CanvasRect*)rects->data, rects->len, op);
        for (guint i = 0; i < rects->len; i++) {
            CanvasItem* it = g_array_index(items, CanvasItem*, i);
            const CanvasRect* r = &g_array_index(rects, CanvasRect, i);
            it->x = r->x;
            it->y = r->y;
            canvas_item_place(it);
        }
        update_extent(p);
        schedule_save(p);
    }
    g_array_unref(items);
    g_array_unref(rects);
}

/* ------------------------------------------------------------------------- */
/* Page Setup and API                                                        */
/* ------------------------------------------------------------------------- */

static void close_page(CanvasPage* p) {
    save_now(p);
    prune_media(p);
    GtkNotebook* nb = GTK_NOTEBOOK(p->app->notebook);
    gint page = gtk_notebook_page_num(nb, p->root);
    if (page >= 0) gtk_notebook_remove_page(nb, page);
}

static void on_close_clicked(GtkButton* button, gpointer data) {
    (void)button;
    close_page(data);
}

static void on_zoom_changed(gdouble scale, gpointer data) {
    CanvasPage* p = data;
    gchar* text = g_strdup_printf("%d%%", (gint)(scale * 100 + 0.5));
    gtk_button_set_label(GTK_BUTTON(p->zoom_button), text);
    g_free(text);
}

static void on_zoom_clicked(GtkButton* button, gpointer data) {
    (void)button;
    CanvasPage* p = data;
    canvas_viewport_zoom_step(p->viewport, 0);
    gtk_widget_grab_focus(p->surface);
}

static void on_label_middle_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    close_page(data);
}

static void on_root_destroy(GtkWidget* widget, gpointer data) {
    (void)widget;
    CanvasPage* p = data;
    p->closed = TRUE;
    if (p->save_source != 0) {
        g_source_remove(p->save_source);
        p->save_source = 0;
    }
    if (p->app->canvas_page == p->root) p->app->canvas_page = NULL;
}

static void page_free(gpointer data) {
    CanvasPage* p = data;
    if (p->save_source != 0) g_source_remove(p->save_source);
    g_clear_weak_pointer(&p->last_focus);
    if (p->toolbar != NULL) canvas_toolbar_free(p->toolbar);
    if (p->props != NULL) canvas_props_free(p->props);
    canvas_history_free(p->history);
    canvas_viewport_free(p->viewport);
    g_free(p->drag_before);
    g_free(p->dir);
    g_free(p);
}

static CanvasPage* page_of(AppData* app) {
    return app->canvas_page != NULL ? g_object_get_data(G_OBJECT(app->canvas_page), PAGE_KEY) : NULL;
}

static CanvasPage* ensure_page(AppData* app) {
    CanvasPage* p = page_of(app);
    if (p != NULL || app->notebook == NULL) return p;
    ensure_css();
    p = g_new0(CanvasPage, 1);
    p->app = app;
    p->snap_grid = TRUE;
    p->history = canvas_history_new(HISTORY_LIMIT);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(root, "canvas-page");
    gtk_widget_set_hexpand(root, TRUE);
    gtk_widget_set_vexpand(root, TRUE);
    p->root = root;

    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(bar, "canvas-header");
    p->title = gtk_label_new(NULL);
    gtk_widget_add_css_class(p->title, "canvas-title");
    gtk_label_set_ellipsize(GTK_LABEL(p->title), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(bar), p->title);
    gtk_label_set_xalign(GTK_LABEL(p->title), 0.0f);
    gtk_widget_set_hexpand(p->title, TRUE);

    p->zoom_button = gtk_button_new_with_label("100%");
    gtk_widget_add_css_class(p->zoom_button, "flat");
    gtk_widget_set_tooltip_text(p->zoom_button, "Zoom: Ctrl+Wheel, Ctrl+= / Ctrl+-; Click: 100% (Ctrl+0)");
    g_signal_connect(p->zoom_button, "clicked", G_CALLBACK(on_zoom_clicked), p);
    gtk_box_append(GTK_BOX(bar), p->zoom_button);

    GtkWidget* close = icon_button_new("window-close", "Close Canvas (Ctrl+W)");
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), p);
    gtk_box_append(GTK_BOX(bar), close);
    gtk_box_append(GTK_BOX(root), bar);

    /* Floating tool bar (top) and properties panel (left) over the page */
    CanvasToolbarCallbacks cbs = {
        .on_tool = on_toolbar_tool,
        .on_crop_image = on_toolbar_crop,
        .on_toggle_grid = on_toolbar_toggle_grid,
        .on_align = on_toolbar_align
    };
    p->toolbar = canvas_toolbar_new(&cbs, p);
    CanvasPropsCallbacks pcbs = {
        .on_shape_style = on_props_shape_style,
        .on_format_style = on_props_format_style,
        .on_format_preset = on_props_format_preset,
        .on_format_list = on_props_format_list,
        .on_format_indent = on_props_format_indent,
        .on_format_checkbox = on_props_format_checkbox
    };
    p->props = canvas_props_new(&pcbs, p);
    canvas_props_default_style(&p->style);

    p->surface = gtk_fixed_new();
    gtk_widget_add_css_class(p->surface, "canvas-surface");
    gtk_widget_set_focusable(p->surface, TRUE);
    gtk_widget_set_hexpand(p->surface, TRUE);
    gtk_widget_set_vexpand(p->surface, TRUE);
    p->viewport = canvas_viewport_new(p->surface, on_zoom_changed, p);
    GtkWidget* overlay = gtk_overlay_new();
    gtk_widget_set_vexpand(overlay, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), canvas_viewport_get_widget(p->viewport));
    GtkWidget* bar_widget = canvas_toolbar_get_widget(p->toolbar);
    gtk_widget_set_margin_top(bar_widget, 10);
    gtk_widget_set_margin_start(bar_widget, 12);
    gtk_widget_set_margin_end(bar_widget, 12);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), bar_widget);
    GtkWidget* props_widget = canvas_props_get_widget(p->props);
    gtk_widget_set_margin_top(props_widget, 64);
    gtk_widget_set_margin_start(props_widget, 12);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), props_widget);
    gtk_box_append(GTK_BOX(root), overlay);

    GtkGesture* drag = gtk_gesture_drag_new();
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(drag), GTK_PHASE_CAPTURE);
    g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), p);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), p);
    g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), p);
    gtk_widget_add_controller(p->surface, GTK_EVENT_CONTROLLER(drag));

    GtkGesture* click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_page_pressed), p);
    gtk_widget_add_controller(p->surface, GTK_EVENT_CONTROLLER(click));

    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), p);
    g_signal_connect(motion, "leave", G_CALLBACK(on_leave), p);
    gtk_widget_add_controller(p->surface, motion);

    GtkDropTarget* drop = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    g_signal_connect(drop, "drop", G_CALLBACK(on_drop), p);
    gtk_widget_add_controller(p->surface, GTK_EVENT_CONTROLLER(drop));

    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), p);
    g_signal_connect(keys, "key-released", G_CALLBACK(on_key_released), p);
    gtk_widget_add_controller(root, keys);

    g_object_set_data_full(G_OBJECT(root), PAGE_KEY, p, page_free);
    g_signal_connect(root, "destroy", G_CALLBACK(on_root_destroy), p);
    g_signal_connect(root, "map", G_CALLBACK(on_root_map), p);
    app->canvas_page = root;

    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gint after = app->git_tab_box != NULL ? gtk_notebook_page_num(nb, app->git_tab_box) : 0;
    if (app->refs_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->refs_page));
    if (app->search_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->search_page));
    GtkWidget* label = icon_label_new("document-edit", "Canvas");
    GtkGesture* mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(mclick, "released", G_CALLBACK(on_label_middle_click), p);
    gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(mclick));
    gtk_notebook_insert_page(nb, root, label, after + 1);
    return p;
}

void canvas_view_open(AppData* app, const gchar* dir, const gchar* title) {
    if (app == NULL || dir == NULL) return;
    CanvasPage* p = ensure_page(app);
    if (p == NULL) return;
    if (g_strcmp0(p->dir, dir) != 0) {
        load(p, dir, title);
    } else if (title != NULL) {
        gtk_label_set_text(GTK_LABEL(p->title), title);
    }
    if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
        gtk_widget_set_visible(app->viewer_box, TRUE);
    }
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, p->root));
    GtkWidget* first = gtk_widget_get_first_child(p->surface);
    if (first != NULL && canvas_item_of(first) != NULL) {
        canvas_item_select(canvas_item_of(first));
    } else {
        gtk_widget_grab_focus(p->surface);
    }
}

void canvas_view_save(AppData* app) {
    CanvasPage* p = app != NULL ? page_of(app) : NULL;
    if (p != NULL) save_now(p);
}

gboolean canvas_view_close_path(AppData* app, const gchar* path) {
    CanvasPage* p = app != NULL ? page_of(app) : NULL;
    if (p == NULL || p->dir == NULL || path == NULL) return FALSE;
    gchar* under = g_strconcat(path, "/", NULL);
    gboolean inside = g_strcmp0(p->dir, path) == 0 || g_str_has_prefix(p->dir, under);
    g_free(under);
    if (!inside) return FALSE;
    close_page(p);
    return TRUE;
}

void canvas_view_path_moved(AppData* app, const gchar* old_path, const gchar* new_path, const gchar* title) {
    CanvasPage* p = app != NULL ? page_of(app) : NULL;
    if (p == NULL || p->dir == NULL || old_path == NULL || new_path == NULL) return;
    gsize n = strlen(old_path);
    if (strncmp(p->dir, old_path, n) != 0 || (p->dir[n] != '\0' && p->dir[n] != '/')) return;
    gchar* dir = g_strconcat(new_path, p->dir + n, NULL);
    g_free(p->dir);
    p->dir = dir;
    for (GtkWidget* c = gtk_widget_get_first_child(p->surface); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        canvas_item_set_dir(canvas_item_of(c), p->dir);
    }
    if (title != NULL && p->dir[strlen(new_path)] == '\0') gtk_label_set_text(GTK_LABEL(p->title), title);
}

gboolean canvas_view_close_shown(AppData* app) {
    CanvasPage* p = app != NULL ? page_of(app) : NULL;
    if (p == NULL || app->notebook == NULL || !gtk_widget_get_mapped(p->root)) return FALSE;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    if (gtk_notebook_get_current_page(nb) != gtk_notebook_page_num(nb, p->root)) return FALSE;
    close_page(p);
    return TRUE;
}

gboolean canvas_view_claims_key(AppData* app, guint keyval, GdkModifierType state) {
    CanvasPage* p = app != NULL ? page_of(app) : NULL;
    if (p == NULL || p->dir == NULL || zoom_key_step(keyval, state) == G_MININT) return FALSE;
    GtkWidget* f = focused_widget(p);
    return f != NULL && (f == p->root || gtk_widget_is_ancestor(f, p->root));
}
