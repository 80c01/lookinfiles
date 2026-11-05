/* The Canvas page of the preview notebook (see canvas_view.h).
 * Edits a Canvas note: text boxes, pictures, shapes and tables on a page. */
#include "canvas_view.h"
#include "canvas_crop.h"
#include "canvas_doc.h"
#include "canvas_geometry.h"
#include "canvas_item.h"
#include "canvas_media.h"
#include "canvas_text.h"
#include "canvas_toolbar.h"
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

struct CanvasPage {
    AppData* app;
    gchar* dir;        /* note directory, NULL when nothing is open */
    GtkWidget* root;   /* notebook page */
    GtkWidget* title;
    CanvasToolbar* toolbar;
    GtkWidget* scroll;
    GtkWidget* surface; /* GtkFixed */
    gboolean snap_grid;
    guint save_source;
    guint generation;  /* bumped on each note loaded */
    gboolean dirty;
    gboolean loading;
    gboolean closed;
    /* Drag of a block */
    CanvasItem* drag_item;
    gboolean drag_resize;
    gint drag_x, drag_y, drag_w, drag_h;
    /* Pointer coordinates over the page */
    gboolean pointer_in;
    gdouble pointer_x, pointer_y;
};

static void schedule_save(CanvasPage* p);
static gboolean save_now(CanvasPage* p);
static void focus_next_item(CanvasPage* p, CanvasItem* from, gint step);

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".canvas-page { background-color: @bg_view; }"
                  ".canvas-page .canvas-header { padding: 6px 10px; color: @fg; }"
                  ".canvas-page .canvas-title { font-weight: bold; }"
                  ".canvas-page .canvas-hint { color: @fg_subtle; }"
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

static CanvasItem* focused_text_item(CanvasPage* p) {
    CanvasItem* it = focused_item(p);
    return (it != NULL && it->kind == CANVAS_BLOCK_TEXT) ? it : NULL;
}

static void deselect(CanvasPage* p) {
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

static void on_item_changed_notify(CanvasPage* p) {
    update_extent(p);
    schedule_save(p);
}

static CanvasItem* add_item(CanvasPage* p, const CanvasBlock* b) {
    return canvas_item_add(p, p->surface, p->dir, b, on_item_changed_notify, p->loading);
}

static void remove_item(CanvasItem* it) {
    CanvasPage* p = it->page;
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
        GtkAdjustment* h = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(p->scroll));
        GtkAdjustment* v = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(p->scroll));
        *x = (gint)gtk_adjustment_get_value(h) + NEW_OFFSET;
        *y = (gint)gtk_adjustment_get_value(v) + NEW_OFFSET;
    }
}

static CanvasItem* new_text_box(CanvasPage* p, gint x, gint y, const gchar* text) {
    g_autoptr(CanvasDoc) tmp = canvas_doc_new();
    CanvasItem* it = add_item(p, canvas_doc_add_text(tmp, x, y, 0, text));
    update_extent(p);
    schedule_save(p);
    canvas_item_edit(it);
    return it;
}

static CanvasItem* new_shape(CanvasPage* p, gint x, gint y, gint w, gint h, CanvasShapeType type) {
    g_autoptr(CanvasDoc) tmp = canvas_doc_new();
    CanvasBlock* b = canvas_doc_add_shape(tmp, x, y, w, h, type);
    CanvasItem* it = add_item(p, b);
    update_extent(p);
    schedule_save(p);
    canvas_item_select(it);
    return it;
}

static CanvasItem* new_table(CanvasPage* p, gint x, gint y, gint rows, gint cols) {
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

static CanvasDoc* doc_from_widgets(CanvasPage* p) {
    CanvasDoc* doc = canvas_doc_new();
    for (GtkWidget* c = gtk_widget_get_first_child(p->surface); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        CanvasItem* it = canvas_item_of(c);
        if (it == NULL) continue;
        if (it->kind == CANVAS_BLOCK_IMAGE) {
            CanvasBlock* b = canvas_doc_add_image(doc, it->x, it->y, it->w, it->h, it->src);
            b->crop_x = it->crop_x;
            b->crop_y = it->crop_y;
            b->crop_w = it->crop_w;
            b->crop_h = it->crop_h;
            continue;
        }
        if (it->kind == CANVAS_BLOCK_SHAPE) {
            CanvasBlock* b = canvas_doc_add_shape(doc, it->x, it->y, it->w, it->h, it->shape_type);
            g_free(b->stroke_color);
            b->stroke_color = g_strdup(it->stroke_color);
            b->fill_style = it->fill_style;
            b->stroke_width = it->stroke_width;
            b->rough = it->rough;
            if (it->points != NULL) {
                g_array_append_vals(b->points, it->points->data, it->points->len);
            }
            continue;
        }
        if (it->kind == CANVAS_BLOCK_TABLE && it->table_block != NULL) {
            CanvasBlock* b = canvas_doc_add_table(doc, it->x, it->y,
                                                  it->table_block->table_rows, it->table_block->table_cols);
            b->w = it->w;
            b->h = it->h;
            for (gint r = 0; r < b->table_rows; r++) {
                for (gint col = 0; col < b->table_cols; col++) {
                    canvas_table_set_cell(b, r, col, canvas_table_get_cell(it->table_block, r, col));
                }
            }
            for (gint col = 0; col < b->table_cols; col++) {
                canvas_table_set_col_width(b, col, canvas_table_get_col_width(it->table_block, col));
            }
            continue;
        }
        GtkTextIter s, e;
        gtk_text_buffer_get_bounds(it->buffer, &s, &e);
        gchar* text = gtk_text_buffer_get_text(it->buffer, &s, &e, TRUE);
        CanvasBlock* b = canvas_doc_add_text(doc, it->x, it->y, it->w, text);
        g_free(text);
        GtkTextIter i = s;
        while (!gtk_text_iter_is_end(&i)) {
            GtkTextIter next = i;
            gtk_text_iter_forward_to_tag_toggle(&next, NULL);
            canvas_block_add_run(b, gtk_text_iter_get_offset(&i), gtk_text_iter_get_offset(&next),
                                 canvas_text_styles_at(it->buffer, &i));
            i = next;
        }
    }
    return doc;
}

static gboolean save_now(CanvasPage* p) {
    if (p->save_source != 0) {
        g_source_remove(p->save_source);
        p->save_source = 0;
    }
    if (!p->dirty || p->dir == NULL || p->closed) return TRUE;
    g_autoptr(CanvasDoc) doc = doc_from_widgets(p);
    g_autoptr(GError) error = NULL;
    if (!canvas_doc_save(doc, p->dir, &error)) {
        g_warning("canvas: cannot save %s: %s", p->dir, error->message);
        return FALSE;
    }
    canvas_doc_prune_media(doc, p->dir);
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

static void clear_surface(CanvasPage* p) {
    GtkWidget* c;
    while ((c = gtk_widget_get_first_child(p->surface)) != NULL) {
        gtk_fixed_remove(GTK_FIXED(p->surface), c);
    }
}

static void load(CanvasPage* p, const gchar* dir, const gchar* title) {
    save_now(p);
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
}

/* ------------------------------------------------------------------------- */
/* Drag and Magnetism                                                        */
/* ------------------------------------------------------------------------- */

static void on_drag_begin(GtkGestureDrag* gesture, gdouble x, gdouble y, gpointer data) {
    CanvasPage* p = data;
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
    (void)gesture;
    CanvasPage* p = data;
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
    } else {
        it->w = MAX(p->drag_w + (gint)dx, MIN_IMAGE_SIZE);
        it->h = MAX((gint)(it->w / it->aspect), MIN_IMAGE_SIZE);
    }
    canvas_item_place(it);
}

static void on_drag_end(GtkGestureDrag* gesture, gdouble dx, gdouble dy, gpointer data) {
    (void)gesture;
    CanvasPage* p = data;
    if (p->drag_item == NULL) return;
    if (dx != 0 || dy != 0) {
        update_extent(p);
        schedule_save(p);
    }
    p->drag_item = NULL;
}

/* user_data: a reference on the picture's frame, so the item stays alive
 * while the dialog is open; ignored if the picture left the page meanwhile */
static void on_crop_applied(double x, double y, double w, double h, gpointer user_data) {
    GtkWidget* frame = user_data;
    CanvasItem* it = canvas_item_of(frame);
    if (it == NULL || gtk_widget_get_parent(frame) == NULL) return;
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
    gboolean shift = (state & GDK_SHIFT_MASK) != 0;
    gboolean alt = (state & GDK_ALT_MASK) != 0;

    if (keyval == GDK_KEY_Escape && !ctrl && !alt) {
        canvas_item_select(it);
        return TRUE;
    }
    if ((keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab) && !ctrl && !alt) {
        focus_next_item(it->page, it, keyval == GDK_KEY_Tab ? 1 : -1);
        return TRUE;
    }
    if ((keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) && !ctrl && !alt) {
        if (canvas_text_handle_enter(GTK_TEXT_VIEW(it->content))) return TRUE;
    }
    if (ctrl && shift && (keyval == GDK_KEY_c || keyval == GDK_KEY_C)) {
        canvas_text_toggle_checkbox(it->buffer);
        schedule_save(it->page);
        return TRUE;
    }
    if (ctrl && alt) {
        if (keyval == GDK_KEY_1 || keyval == GDK_KEY_KP_1) {
            canvas_text_apply_preset(it->buffer, CANVAS_PRESET_H1);
            schedule_save(it->page);
            return TRUE;
        }
        if (keyval == GDK_KEY_2 || keyval == GDK_KEY_KP_2) {
            canvas_text_apply_preset(it->buffer, CANVAS_PRESET_H2);
            schedule_save(it->page);
            return TRUE;
        }
        if (keyval == GDK_KEY_0 || keyval == GDK_KEY_KP_0) {
            canvas_text_apply_preset(it->buffer, CANVAS_PRESET_BODY);
            schedule_save(it->page);
            return TRUE;
        }
        if (keyval == GDK_KEY_q || keyval == GDK_KEY_Q) {
            canvas_text_apply_preset(it->buffer, CANVAS_PRESET_QUOTE);
            schedule_save(it->page);
            return TRUE;
        }
    }
    if (!ctrl) return FALSE;

    guint style = 0;
    switch (keyval) {
    case GDK_KEY_b: case GDK_KEY_B: style = (shift || alt) ? 0 : CANVAS_STYLE_BOLD; break;
    case GDK_KEY_i: case GDK_KEY_I: style = (shift || alt) ? 0 : CANVAS_STYLE_ITALIC; break;
    case GDK_KEY_u: case GDK_KEY_U: style = (shift || alt) ? 0 : CANVAS_STYLE_UNDERLINE; break;
    case GDK_KEY_s: case GDK_KEY_S: style = (shift && !alt) ? CANVAS_STYLE_STRIKE : 0; break;
    case GDK_KEY_e: case GDK_KEY_E: style = (shift || alt) ? 0 : CANVAS_STYLE_CODE; break;
    case GDK_KEY_h: case GDK_KEY_H: style = CANVAS_STYLE_HIGHLIGHT; break;
    default: break;
    }
    if (style == 0) return FALSE;
    canvas_text_toggle_style(it->buffer, &it->pending, style);
    return TRUE;
}

static gboolean block_key(CanvasPage* p, CanvasItem* it, guint keyval, GdkModifierType state) {
    if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) != 0) return FALSE;
    gint step = (state & GDK_SHIFT_MASK) ? NUDGE_BIG : NUDGE;
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

static gboolean on_paste_picture(CanvasPage* p) {
    gint x, y;
    new_block_position(p, TRUE, &x, &y);
    return canvas_media_paste_picture(p->root, p->dir, x, y, on_new_picture_cb, p);
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
    CanvasItem* it = focused_item(p);
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
/* Toolbar Callbacks                                                         */
/* ------------------------------------------------------------------------- */

static void on_toolbar_add_text(gpointer udata) {
    CanvasPage* p = udata;
    gint x, y;
    new_block_position(p, FALSE, &x, &y);
    new_text_box(p, x, y, "");
}

static void on_toolbar_add_shape(CanvasShapeType type, gpointer udata) {
    CanvasPage* p = udata;
    gint x, y;
    new_block_position(p, FALSE, &x, &y);
    new_shape(p, x, y, 160, 100, type);
}

static void on_toolbar_add_table(gint rows, gint cols, gpointer udata) {
    CanvasPage* p = udata;
    gint x, y;
    new_block_position(p, FALSE, &x, &y);
    new_table(p, x, y, rows, cols);
}

static void on_toolbar_crop(gpointer udata) {
    CanvasPage* p = udata;
    open_crop_dialog(p, focused_item(p));
}

static void on_toolbar_format_style(guint style_flag, gpointer udata) {
    CanvasItem* it = focused_text_item(udata);
    if (it != NULL) {
        canvas_text_toggle_style(it->buffer, &it->pending, style_flag);
        schedule_save(it->page);
    }
}

static void on_toolbar_format_preset(CanvasPreset preset, gpointer udata) {
    CanvasItem* it = focused_text_item(udata);
    if (it != NULL) {
        canvas_text_apply_preset(it->buffer, preset);
        schedule_save(it->page);
    }
}

static void on_toolbar_format_list(const gchar* prefix, gpointer udata) {
    CanvasItem* it = focused_text_item(udata);
    if (it != NULL) {
        canvas_text_insert_list(it->buffer, prefix);
        schedule_save(it->page);
    }
}

static void on_toolbar_format_indent(gint step, gpointer udata) {
    CanvasItem* it = focused_text_item(udata);
    if (it != NULL) {
        canvas_text_indent(it->buffer, step);
        schedule_save(it->page);
    }
}

static void on_toolbar_format_checkbox(gpointer udata) {
    CanvasItem* it = focused_text_item(udata);
    if (it != NULL) {
        canvas_text_toggle_checkbox(it->buffer);
        schedule_save(it->page);
    }
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
    GtkNotebook* nb = GTK_NOTEBOOK(p->app->notebook);
    gint page = gtk_notebook_page_num(nb, p->root);
    if (page >= 0) gtk_notebook_remove_page(nb, page);
}

static void on_close_clicked(GtkButton* button, gpointer data) {
    (void)button;
    close_page(data);
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
    if (p->toolbar != NULL) canvas_toolbar_free(p->toolbar);
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
    GtkWidget* hint = gtk_label_new("Excalidraw shapes · Tables · Magnetic grid & align · Typography & Checklists");
    gtk_widget_add_css_class(hint, "canvas-hint");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(hint), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(hint, TRUE);
    gtk_box_append(GTK_BOX(bar), hint);

    GtkWidget* close = icon_button_new("window-close", "Close Canvas (Ctrl+W)");
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), p);
    gtk_box_append(GTK_BOX(bar), close);
    gtk_box_append(GTK_BOX(root), bar);

    /* Toolbar */
    CanvasToolbarCallbacks cbs = {
        .on_add_text = on_toolbar_add_text,
        .on_add_shape = on_toolbar_add_shape,
        .on_add_table = on_toolbar_add_table,
        .on_crop_image = on_toolbar_crop,
        .on_format_style = on_toolbar_format_style,
        .on_format_preset = on_toolbar_format_preset,
        .on_format_list = on_toolbar_format_list,
        .on_format_indent = on_toolbar_format_indent,
        .on_format_checkbox = on_toolbar_format_checkbox,
        .on_toggle_grid = on_toolbar_toggle_grid,
        .on_align = on_toolbar_align
    };
    p->toolbar = canvas_toolbar_new(&cbs, p);
    gtk_box_append(GTK_BOX(root), canvas_toolbar_get_widget(p->toolbar));

    p->surface = gtk_fixed_new();
    gtk_widget_add_css_class(p->surface, "canvas-surface");
    gtk_widget_set_focusable(p->surface, TRUE);
    gtk_widget_set_hexpand(p->surface, TRUE);
    gtk_widget_set_vexpand(p->surface, TRUE);
    p->scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(p->scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(p->scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(p->scroll), p->surface);
    gtk_box_append(GTK_BOX(root), p->scroll);

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
    gtk_widget_add_controller(root, keys);

    g_object_set_data_full(G_OBJECT(root), PAGE_KEY, p, page_free);
    g_signal_connect(root, "destroy", G_CALLBACK(on_root_destroy), p);
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
