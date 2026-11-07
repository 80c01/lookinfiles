/* The pointer on the Canvas page (see canvas_drag.h) */
#include "canvas_drag.h"
#include "canvas_draw.h"
#include "canvas_geometry.h"
#include "canvas_history.h"
#include "canvas_item.h"
#include "canvas_media.h"
#include "canvas_page_private.h"
#include "canvas_toolbar.h"

/* ------------------------------------------------------------------------- */
/* Tools                                                                     */
/* ------------------------------------------------------------------------- */

/* Eraser: remove the block under (x, y) */
static void erase_at(CanvasPage* p, gdouble x, gdouble y) {
    CanvasItem* it = canvas_item_of(gtk_widget_pick(p->surface, x, y, GTK_PICK_DEFAULT));
    if (it == NULL) return;
    canvas_page_remove_item(it);
    canvas_page_update_extent(p);
}

/* A press on the page with another tool than Select: start a shape, place
 * a text box or a table, erase */
static void tool_begin(CanvasPage* p, gdouble x, gdouble y) {
    if (canvas_tool_draws(p->tool)) {
        CanvasItem* it = canvas_page_new_shape(p, (gint)x, (gint)y, MIN_SHAPE_SIZE, MIN_SHAPE_SIZE,
                                               canvas_tool_shape(p->tool));
        p->draw_item = it;
        p->draw_x0 = x;
        p->draw_y0 = y;
        if (p->tool == CANVAS_TOOL_PENCIL) {
            canvas_draw_add_point(it, x, y);
        } else {
            canvas_draw_update(it, x, y, x, y, FALSE);
        }
    } else if (p->tool == CANVAS_TOOL_TEXT) {
        canvas_page_set_tool(p, CANVAS_TOOL_SELECT);
        canvas_page_new_text_box(p, (gint)x, (gint)y, "");
    } else if (p->tool == CANVAS_TOOL_TABLE) {
        canvas_page_set_tool(p, CANVAS_TOOL_SELECT);
        canvas_page_new_table(p, (gint)x, (gint)y, 3, 3);
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
    canvas_page_update_extent(p);
    canvas_page_schedule_save(p);
    /* The pencil stays, to go on drawing; the other tools give the new shape
     * to Select */
    if (p->tool != CANVAS_TOOL_PENCIL) canvas_page_set_tool(p, CANVAS_TOOL_SELECT);
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
    p->drag_before = canvas_page_state(p);
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
        canvas_page_update_extent(p);
        canvas_page_schedule_save(p);
    }
    p->drag_item = NULL;
    g_clear_pointer(&p->drag_before, g_free);
}

/* ------------------------------------------------------------------------- */
/* Clicks, Pointer and Drops                                                 */
/* ------------------------------------------------------------------------- */

static void on_page_pressed(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    CanvasPage* p = data;
    if (p->tool != CANVAS_TOOL_SELECT) return; /* the tool took the press */
    GtkWidget* hit = gtk_widget_pick(p->surface, x, y, GTK_PICK_DEFAULT);
    CanvasItem* it = canvas_item_of(hit);
    if (it != NULL) {
        if (n_press == 2 && it->kind == CANVAS_BLOCK_IMAGE) {
            canvas_page_open_crop(p, it);
        }
        return;
    }
    if (p->dir == NULL) return;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    if (n_press == 2) {
        canvas_page_new_text_box(p, (gint)x, (gint)y, "");
    } else {
        canvas_page_deselect(p);
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

static void on_new_picture(CanvasPage* p, const gchar* src, gint x, gint y, gint w, gint h) {
    canvas_page_new_picture(p, src, x, y, w, h);
}

static gboolean on_drop(GtkDropTarget* target, const GValue* value, gdouble x, gdouble y, gpointer data) {
    (void)target;
    CanvasPage* p = data;
    return canvas_media_drop_files(value, p->dir, (gint)x, (gint)y, CANVAS_NEW_OFFSET, on_new_picture, p);
}

void canvas_drag_attach(CanvasPage* p) {
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
}
