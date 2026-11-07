/* The pointer on the Canvas page (see canvas_drag.h) */
#include "canvas_drag.h"
#include "canvas_bind.h"
#include "canvas_draw.h"
#include "canvas_geometry.h"
#include "canvas_history.h"
#include "canvas_item.h"
#include "canvas_line.h"
#include "canvas_media.h"
#include "canvas_page_private.h"
#include "canvas_rotate.h"
#include "canvas_toolbar.h"

#include <math.h>

/* ------------------------------------------------------------------------- */
/* Tools                                                                     */
/* ------------------------------------------------------------------------- */

/* Eraser: remove the block under (x, y) */
static void erase_at(CanvasPage* p, gdouble x, gdouble y) {
    CanvasItem* it = canvas_item_of(canvas_line_pick(p->surface, x, y));
    if (it == NULL || it->locked) return;
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
    if (canvas_draw_is_line(it)) canvas_bind_attach(p->surface, it);
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

#define BAND_THRESHOLD 4 /* pointer moves before a press on the page draws a band */

/* Rubber band: a box drawn on the page while the pointer is dragged */
static void band_update(CanvasPage* p, gdouble x, gdouble y) {
    if (p->band == NULL) {
        p->band = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(p->band, "canvas-band");
        gtk_widget_set_can_target(p->band, FALSE);
        gtk_fixed_put(GTK_FIXED(p->surface), p->band, p->band_x0, p->band_y0);
    }
    p->band_x1 = MAX(x, 0);
    p->band_y1 = MAX(y, 0);
    CanvasRect r;
    canvas_drag_box(p->band_x0, p->band_y0, p->band_x1, p->band_y1, FALSE, &r);
    gtk_fixed_move(GTK_FIXED(p->surface), p->band, r.x, r.y);
    gtk_widget_set_size_request(p->band, MAX(r.w, 1), MAX(r.h, 1));
}

/* Drop the band; select what it holds (select) */
static void band_end(CanvasPage* p, gboolean select) {
    p->band_pending = FALSE;
    if (p->band == NULL) return;
    CanvasRect r;
    canvas_drag_box(p->band_x0, p->band_y0, p->band_x1, p->band_y1, FALSE, &r);
    gtk_fixed_remove(GTK_FIXED(p->surface), p->band);
    p->band = NULL;
    if (select) canvas_page_select_box(p, r.x, r.y, r.w, r.h, p->band_extend);
}

static void group_clear(CanvasPage* p) {
    g_clear_pointer(&p->drag_group, g_ptr_array_unref);
    g_clear_pointer(&p->drag_starts, g_array_unref);
}

/* A move of a selected block takes the other selected blocks along */
static void group_begin(CanvasPage* p, CanvasItem* it) {
    group_clear(p);
    p->drag_group = canvas_page_is_selected(p, it) ? canvas_page_selected_unlocked(p) : g_ptr_array_new();
    if (p->drag_group->len == 0) g_ptr_array_add(p->drag_group, it);
    p->drag_starts = g_array_new(FALSE, FALSE, sizeof(CanvasRect));
    for (guint i = 0; i < p->drag_group->len; i++) {
        CanvasItem* m = g_ptr_array_index(p->drag_group, i);
        CanvasRect r;
        canvas_page_item_box(m, &r.x, &r.y, &r.w, &r.h);
        g_array_append_val(p->drag_starts, r);
    }
}

/* ------------------------------------------------------------------------- */
/* Points of a Line                                                          */
/* ------------------------------------------------------------------------- */

/* it is the only block selected: its points can be dragged */
static gboolean selected_alone(CanvasPage* p, CanvasItem* it) {
    GPtrArray* sel = canvas_page_selected(p);
    gboolean alone = sel->len == 1 && g_ptr_array_index(sel, 0) == it;
    g_ptr_array_unref(sel);
    return alone;
}

/* A press on a handle of the selected line it: drag that point, or a new
 * one made in the middle of a segment. FALSE when (x, y) is no handle. */
static gboolean point_begin(CanvasPage* p, CanvasItem* it, gdouble x, gdouble y) {
    if (!canvas_draw_is_line(it) || !selected_alone(p, it)) return FALSE;
    gboolean middle = FALSE;
    gint index = canvas_line_handle_at(it, x, y, &middle);
    if (index < 0) return FALSE;
    g_free(p->drag_before);
    p->drag_before = canvas_page_state(p);
    if (middle) index = canvas_line_insert_point(it, (guint)index);
    if (index < 0) return FALSE;
    p->drag_item = it;
    p->drag_resize = FALSE;
    p->drag_point = index;
    p->drag_point_added = middle;
    p->drag_points = canvas_draw_get_points(it);
    return p->drag_points != NULL;
}

static void point_update(CanvasPage* p, GtkGesture* gesture, gdouble dx, gdouble dy) {
    CanvasItem* it = p->drag_item;
    GArray* pts = g_array_copy(p->drag_points);
    guint n = pts->len;
    guint i = (guint)p->drag_point;
    CanvasPoint* pt = &g_array_index(pts, CanvasPoint, i);
    pt->x += dx;
    pt->y += dy;
    /* Shift: turned to a multiple of 45 degrees from the point before (the
     * next one for the first point) */
    GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));
    if ((state & GDK_SHIFT_MASK) != 0) {
        const CanvasPoint* from = &g_array_index(pts, CanvasPoint, i > 0 ? i - 1 : 1);
        canvas_snap_angle(from->x, from->y, &pt->x, &pt->y);
    }
    pt->x = MAX(pt->x, 0);
    pt->y = MAX(pt->y, 0);
    /* A dragged end lets go of its block (bound again on release) */
    if (dx != 0 || dy != 0) {
        if (i == 0) it->start_id = 0;
        if (i == n - 1) it->end_id = 0;
    }
    canvas_draw_set_points(it, pts);
    g_array_unref(pts);
    canvas_bind_follow(p->surface);
}

static void point_end(CanvasPage* p, gdouble dx, gdouble dy) {
    CanvasItem* it = p->drag_item;
    guint n = p->drag_points->len;
    guint i = (guint)p->drag_point;
    if (dx != 0 || dy != 0) {
        if (i == 0 || i == n - 1) canvas_bind_attach_end(p->surface, it, i == n - 1);
        canvas_history_push(p->history, p->drag_before);
        canvas_page_update_extent(p);
        canvas_page_schedule_save(p);
    } else if (p->drag_point_added) {
        /* A click on a segment middle, no drag: no new point */
        canvas_line_remove_point(it, i);
    }
    g_clear_pointer(&p->drag_points, g_array_unref);
    p->drag_item = NULL;
    g_clear_pointer(&p->drag_before, g_free);
}

/* A double click on a point between the ends of the selected line it
 * removes it */
static gboolean point_remove_at(CanvasPage* p, CanvasItem* it, gdouble x, gdouble y) {
    if (!canvas_draw_is_line(it) || it->locked || !selected_alone(p, it)) return FALSE;
    gboolean middle = FALSE;
    gint index = canvas_line_handle_at(it, x, y, &middle);
    if (index <= 0 || middle) return FALSE;
    gchar* before = canvas_page_state(p);
    if (!canvas_line_remove_point(it, (guint)index)) {
        g_free(before);
        return FALSE;
    }
    canvas_history_push(p->history, before);
    canvas_bind_follow(p->surface);
    canvas_page_update_extent(p);
    canvas_page_schedule_save(p);
    return TRUE;
}

static void on_drag_begin(GtkGestureDrag* gesture, gdouble x, gdouble y, gpointer data) {
    CanvasPage* p = data;
    if (p->tool != CANVAS_TOOL_SELECT) {
        if (p->dir == NULL) return;
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
        tool_begin(p, x, y);
        return;
    }
    /* The handle above the selected shape: turn it */
    CanvasItem* turned = canvas_rotate_handle_at(p->rotate, x, y);
    if (turned != NULL) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
        g_free(p->drag_before);
        p->drag_before = canvas_page_state(p);
        p->drag_item = turned;
        p->drag_resize = FALSE;
        p->drag_rotate = TRUE;
        p->drag_angle = turned->angle;
        return;
    }
    GtkWidget* hit = canvas_line_pick(p->surface, x, y);
    CanvasItem* it = canvas_item_of(hit);
    GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));
    gboolean shift = (state & GDK_SHIFT_MASK) != 0;
    /* The empty page: a band once the pointer moves (a click deselects, a
     * double click adds a text box: on_page_pressed()) */
    if (it == NULL) {
        if (p->dir == NULL) return;
        p->band_pending = TRUE;
        p->band_extend = shift;
        p->band_x0 = x;
        p->band_y0 = y;
        return;
    }
    /* Typing in a shape's label: the pointer is for its text */
    if (canvas_item_in_label(it, hit)) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }
    /* Shift+click on a block: add it to the selection, or take it out */
    if (shift) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
        canvas_page_select_toggle(p, it);
        return;
    }
    /* A locked block: selected, never moved or resized (its text still edited) */
    if (it->locked) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        if (it->kind != CANVAS_BLOCK_TEXT && it->kind != CANVAS_BLOCK_TABLE) canvas_item_select(it);
        return;
    }
    /* A handle of the selected line: drag that point */
    if (point_begin(p, it, x, y)) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
        return;
    }
    gboolean grip = FALSE, corner = FALSE;
    for (GtkWidget* w = hit; w != NULL && w != it->frame; w = gtk_widget_get_parent(w)) {
        if (g_object_get_data(G_OBJECT(w), "canvas-grip") != NULL) grip = TRUE;
        if (g_object_get_data(G_OBJECT(w), "canvas-corner") != NULL) corner = TRUE;
    }
    gboolean move = grip || ((it->kind == CANVAS_BLOCK_IMAGE || it->kind == CANVAS_BLOCK_SHAPE) && !corner);
    if (!move && !corner) {
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
    if (!corner) group_begin(p, it);
    canvas_item_select(it);
}

/* Move the dragged blocks by (dx, dy), their box snapped to the grid and to
 * the other blocks */
static void group_move(CanvasPage* p, gdouble dx, gdouble dy) {
    GPtrArray* group = p->drag_group;
    GArray* starts = p->drag_starts;
    gint x0 = G_MAXINT, y0 = G_MAXINT, x1 = G_MININT, y1 = G_MININT;
    for (guint i = 0; i < starts->len; i++) {
        const CanvasRect* r = &g_array_index(starts, CanvasRect, i);
        x0 = MIN(x0, r->x);
        y0 = MIN(y0, r->y);
        x1 = MAX(x1, r->x + r->w);
        y1 = MAX(y1, r->y + r->h);
    }
    gint new_x = x0 + (gint)dx;
    gint new_y = y0 + (gint)dy;

    GArray* others = g_array_new(FALSE, FALSE, sizeof(CanvasRect));
    for (GtkWidget* c = gtk_widget_get_first_child(p->surface); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        CanvasItem* o = canvas_item_of(c);
        if (o == NULL || g_ptr_array_find(group, o, NULL)) continue;
        CanvasRect r = { o->x, o->y, o->w, MAX(o->h, gtk_widget_get_height(o->frame)) };
        g_array_append_val(others, r);
    }
    CanvasGuides guides = {0};
    canvas_snap_box(&new_x, &new_y, x1 - x0, y1 - y0, (const CanvasRect*)others->data, others->len,
                    p->snap_grid, CANVAS_DEFAULT_GRID, &guides);
    g_array_unref(others);

    /* The blocks keep their places relative to each other at the page edge */
    new_x = MAX(new_x, 0);
    new_y = MAX(new_y, 0);
    for (guint i = 0; i < group->len; i++) {
        CanvasItem* m = g_ptr_array_index(group, i);
        const CanvasRect* r = &g_array_index(starts, CanvasRect, i);
        m->x = r->x + new_x - x0;
        m->y = r->y + new_y - y0;
        canvas_item_place(m);
    }
    /* Lines moved without their blocks let go of them; the others follow */
    if (dx != 0 || dy != 0) canvas_bind_release(group);
    canvas_bind_follow(p->surface);
}

static void on_drag_update(GtkGestureDrag* gesture, gdouble dx, gdouble dy, gpointer data) {
    CanvasPage* p = data;
    gdouble x0, y0;
    gtk_gesture_drag_get_start_point(gesture, &x0, &y0);
    if (p->draw_item != NULL || p->erasing) {
        tool_update(p, GTK_GESTURE(gesture), x0 + dx, y0 + dy);
        return;
    }
    if (p->band_pending) {
        if (p->band == NULL && ABS(dx) < BAND_THRESHOLD && ABS(dy) < BAND_THRESHOLD) return;
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
        band_update(p, x0 + dx, y0 + dy);
        return;
    }
    CanvasItem* it = p->drag_item;
    if (it == NULL) return;
    if (p->drag_points != NULL) {
        point_update(p, GTK_GESTURE(gesture), dx, dy);
        return;
    }
    if (p->drag_rotate) {
        GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));
        canvas_rotate_drag(it, x0 + dx, y0 + dy, (state & GDK_SHIFT_MASK) != 0);
        canvas_bind_follow(p->surface);
        return;
    }

    if (!p->drag_resize) {
        group_move(p, dx, dy);
        return;
    }
    if (it->kind == CANVAS_BLOCK_TEXT || it->kind == CANVAS_BLOCK_TABLE) {
        it->w = MAX(p->drag_w + (gint)dx, MIN_TEXT_WIDTH);
    } else if (it->kind == CANVAS_BLOCK_SHAPE && it->angle != 0) {
        /* Turned: the pointer moves along its sides, the opposite corner stays */
        gdouble lx = dx, ly = dy;
        canvas_rotate_point(0, 0, -it->angle, &lx, &ly);
        it->w = MAX(p->drag_w + (gint)lround(lx), MIN_SHAPE_SIZE);
        it->h = MAX(p->drag_h + (gint)lround(ly), MIN_SHAPE_SIZE);
        CanvasRect start = {p->drag_x, p->drag_y, p->drag_w, p->drag_h};
        canvas_resize_rotated(&start, it->angle, it->w, it->h, &it->x, &it->y);
    } else if (it->kind == CANVAS_BLOCK_SHAPE) {
        it->w = MAX(p->drag_w + (gint)dx, MIN_SHAPE_SIZE);
        it->h = MAX(p->drag_h + (gint)dy, MIN_SHAPE_SIZE);
    } else {
        it->w = MAX(p->drag_w + (gint)dx, MIN_IMAGE_SIZE);
        it->h = MAX((gint)(it->w / it->aspect), MIN_IMAGE_SIZE);
    }
    canvas_item_place(it);
    if (canvas_draw_is_line(it) && (dx != 0 || dy != 0)) {
        it->start_id = 0;
        it->end_id = 0;
    }
    canvas_bind_follow(p->surface);
}

static void on_drag_end(GtkGestureDrag* gesture, gdouble dx, gdouble dy, gpointer data) {
    (void)gesture;
    CanvasPage* p = data;
    if (p->draw_item != NULL || p->erasing) {
        tool_end(p);
        return;
    }
    if (p->band_pending) {
        band_end(p, TRUE);
        return;
    }
    if (p->drag_item == NULL) return;
    if (p->drag_points != NULL) {
        point_end(p, dx, dy);
        return;
    }
    if (p->drag_rotate) {
        CanvasItem* it = p->drag_item;
        p->drag_rotate = FALSE;
        p->drag_item = NULL;
        if (it->angle != p->drag_angle) {
            canvas_history_push(p->history, p->drag_before);
            canvas_page_update_extent(p);
            canvas_page_schedule_save(p);
        }
        g_clear_pointer(&p->drag_before, g_free);
        return;
    }
    if (dx != 0 || dy != 0) {
        canvas_history_push(p->history, p->drag_before);
        canvas_page_update_extent(p);
        canvas_page_schedule_save(p);
    } else if (!p->drag_resize) {
        /* A click on a selected block, no move: that block alone */
        canvas_page_selection_clear(p);
    }
    p->drag_item = NULL;
    group_clear(p);
    g_clear_pointer(&p->drag_before, g_free);
}

/* Another gesture took the press (a double click): no band */
static void on_drag_cancel(GtkGesture* gesture, GdkEventSequence* sequence, gpointer data) {
    (void)gesture;
    (void)sequence;
    band_end(data, FALSE);
}

/* ------------------------------------------------------------------------- */
/* Clicks, Pointer and Drops                                                 */
/* ------------------------------------------------------------------------- */

static void on_page_pressed(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    CanvasPage* p = data;
    if (p->tool != CANVAS_TOOL_SELECT) return; /* the tool took the press */
    if (canvas_rotate_handle_at(p->rotate, x, y) != NULL) return; /* the drag turns the shape */
    GtkWidget* hit = canvas_line_pick(p->surface, x, y);
    CanvasItem* it = canvas_item_of(hit);
    if (it != NULL) {
        if (n_press == 2 && point_remove_at(p, it, x, y)) {
            gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
        } else if (n_press == 2 && it->kind == CANVAS_BLOCK_IMAGE) {
            canvas_page_open_crop(p, it);
        } else if (n_press == 2 && it->label != NULL && !canvas_item_in_label(it, hit)) {
            canvas_item_edit(it); /* type in the shape */
        }
        return;
    }
    if (p->dir == NULL) return;
    if (n_press == 2) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
        canvas_page_new_text_box(p, (gint)x, (gint)y, "");
        return;
    }
    /* A single press is left to the drag gesture: a band may follow;
     * Shift keeps the selection, for a band adding to it */
    GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));
    if ((state & GDK_SHIFT_MASK) == 0) canvas_page_deselect(p);
}

static void on_motion(GtkEventControllerMotion* ctrl, gdouble x, gdouble y, gpointer data) {
    (void)ctrl;
    CanvasPage* p = data;
    p->pointer_in = TRUE;
    p->pointer_x = x;
    p->pointer_y = y;
    /* Over a line: a cursor on its stroke and its handles only */
    CanvasItem* it = canvas_item_of(gtk_widget_pick(p->surface, x, y, GTK_PICK_DEFAULT));
    if (canvas_draw_is_line(it)) canvas_line_update_cursor(it, x, y);
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
    g_signal_connect(drag, "cancel", G_CALLBACK(on_drag_cancel), p);
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
