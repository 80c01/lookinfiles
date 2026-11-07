#include "canvas_item.h"
#include "canvas_crop.h"
#include "canvas_rough_draw.h"
#include "canvas_table.h"
#include "canvas_text.h"

#include <string.h>

CanvasItem* canvas_item_of(GtkWidget* w) {
    for (; w != NULL; w = gtk_widget_get_parent(w)) {
        CanvasItem* it = g_object_get_data(G_OBJECT(w), ITEM_KEY);
        if (it != NULL) return it;
    }
    return NULL;
}

static void item_free(gpointer data) {
    CanvasItem* it = data;
    g_free(it->src);
    g_free(it->stroke_color);
    if (it->points != NULL) g_array_unref(it->points);
    if (it->table_block != NULL) {
        if (it->table_block->cells != NULL) g_ptr_array_unref(it->table_block->cells);
        if (it->table_block->col_widths != NULL) g_array_unref(it->table_block->col_widths);
        g_free(it->table_block);
    }
    g_free(it);
}

void canvas_item_select(CanvasItem* it) {
    if (it == NULL || it->frame == NULL) return;
    GtkRoot* root = gtk_widget_get_root(it->frame);
    if (root != NULL) gtk_root_set_focus(root, it->frame);
}

void canvas_item_edit(CanvasItem* it) {
    if (it == NULL) return;
    if (it->kind == CANVAS_BLOCK_TEXT && it->content != NULL) {
        gtk_widget_grab_focus(it->content);
    } else {
        canvas_item_select(it);
    }
}

void canvas_item_place(CanvasItem* it) {
    if (it == NULL || it->frame == NULL) return;
    it->x = MAX(it->x, 0);
    it->y = MAX(it->y, 0);
    GtkWidget* surface = gtk_widget_get_parent(it->frame);
    if (surface != NULL) {
        gtk_fixed_move(GTK_FIXED(surface), it->frame, it->x, it->y);
    }
    if (it->kind == CANVAS_BLOCK_TEXT) {
        gtk_widget_set_size_request(it->frame, it->w, -1);
    } else if (it->kind == CANVAS_BLOCK_TABLE) {
        gtk_widget_set_size_request(it->frame, it->w, it->h);
    } else {
        gtk_widget_set_size_request(it->content, it->w, it->h);
    }
}

void canvas_item_raise(CanvasItem* it) {
    if (it == NULL || it->frame == NULL) return;
    GtkWidget* surface = gtk_widget_get_parent(it->frame);
    if (surface != NULL && gtk_widget_get_last_child(surface) != it->frame) {
        gtk_widget_insert_before(it->frame, surface, NULL);
    }
}

#define DIR_KEY "note-dir"

void canvas_item_set_dir(CanvasItem* it, const gchar* dir) {
    if (it == NULL || it->kind != CANVAS_BLOCK_IMAGE || it->frame == NULL) return;
    g_object_set_data_full(G_OBJECT(it->frame), DIR_KEY, g_strdup(dir), g_free);
    if (it->content != NULL) gtk_widget_queue_draw(it->content);
}

/* Static key for change notify callback */
#define NOTIFY_KEY "canvas-item-notify"

static void trigger_notify(CanvasItem* it) {
    CanvasItemNotify fn = g_object_get_data(G_OBJECT(it->frame), NOTIFY_KEY);
    if (fn != NULL) fn(it->page);
}

static void on_insert_before(GtkTextBuffer* buffer, GtkTextIter* location, gchar* text, gint len, gpointer data) {
    (void)buffer;
    (void)location;
    (void)text;
    (void)len;
    CanvasItem* it = data;
    it->insert_styles = it->pending;
    it->pending = 0;
}

static void on_insert_after(GtkTextBuffer* buffer, GtkTextIter* location, gchar* text, gint len, gpointer data) {
    CanvasItem* it = data;
    gint end = gtk_text_iter_get_offset(location);
    gint start = end - (gint)g_utf8_strlen(text, len);
    guint styles = 0;
    if (start > 0) {
        GtkTextIter prev;
        gtk_text_buffer_get_iter_at_offset(buffer, &prev, start - 1);
        if (gtk_text_iter_get_char(&prev) != '\n') styles = canvas_text_styles_at(buffer, &prev);
    }
    styles ^= it->insert_styles;
    it->insert_styles = 0;
    if (styles != 0) {
        canvas_text_apply_styles(buffer, start, end, styles);
    }
    gtk_text_buffer_get_iter_at_offset(buffer, location, end);
}

static void on_mark_set(GtkTextBuffer* buffer, const GtkTextIter* location, GtkTextMark* mark, gpointer data) {
    (void)location;
    CanvasItem* it = data;
    if (mark == gtk_text_buffer_get_insert(buffer)) it->pending = 0;
}

static void on_buffer_changed(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    trigger_notify(data);
}

static void on_tag_changed(GtkTextBuffer* buffer, GtkTextTag* tag, const GtkTextIter* s, const GtkTextIter* e,
                           gpointer data) {
    (void)buffer;
    (void)tag;
    (void)s;
    (void)e;
    trigger_notify(data);
}

static void on_text_view_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    CanvasItem* it = data;
    if (canvas_text_click_checkbox(GTK_TEXT_VIEW(it->content), x, y)) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
        trigger_notify(it);
    }
}

static CanvasItem* item_base_new(CanvasPage* p, GtkWidget* surface, CanvasBlockKind kind, GtkWidget* content) {
    CanvasItem* it = g_new0(CanvasItem, 1);
    it->page = p;
    it->kind = kind;
    it->content = content;

    GtkWidget* frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(frame, "canvas-block");
    gtk_widget_set_focusable(frame, TRUE);
    /* A shape is moved by dragging it: no bar above it */
    if (kind != CANVAS_BLOCK_SHAPE) {
        GtkWidget* grip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(grip, "canvas-grip");
        gtk_widget_set_cursor_from_name(grip, "move");
        gtk_widget_set_tooltip_text(grip, "Drag to Move (Esc, then Arrows)");
        g_object_set_data(G_OBJECT(grip), "canvas-grip", GINT_TO_POINTER(1));
        gtk_box_append(GTK_BOX(frame), grip);
    }

    GtkWidget* overlay = gtk_overlay_new();
    if (content != NULL) gtk_overlay_set_child(GTK_OVERLAY(overlay), content);

    GtkWidget* corner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(corner, "canvas-corner");
    gtk_widget_set_halign(corner, GTK_ALIGN_END);
    gtk_widget_set_valign(corner, GTK_ALIGN_END);
    gtk_widget_set_cursor_from_name(corner, "nwse-resize");
    gtk_widget_set_tooltip_text(corner, "Drag to Resize");
    g_object_set_data(G_OBJECT(corner), "canvas-corner", GINT_TO_POINTER(1));
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), corner);
    gtk_box_append(GTK_BOX(frame), overlay);

    it->frame = frame;
    g_object_set_data_full(G_OBJECT(frame), ITEM_KEY, it, item_free);
    gtk_fixed_put(GTK_FIXED(surface), frame, 0, 0);
    return it;
}

static CanvasItem* add_text(CanvasPage* p, GtkWidget* surface, const CanvasBlock* b,
                            CanvasItemNotify on_change, gboolean loading) {
    GtkWidget* view = gtk_text_view_new();
    gtk_widget_add_css_class(view, "canvas-text");
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(view), FALSE);
    CanvasItem* it = item_base_new(p, surface, CANVAS_BLOCK_TEXT, view);
    g_object_set_data(G_OBJECT(it->frame), NOTIFY_KEY, on_change);
    it->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    it->x = b->x;
    it->y = b->y;
    it->w = MAX(b->w, MIN_TEXT_WIDTH);
    canvas_text_ensure_tags(it->buffer);

    gchar* valid = g_utf8_make_valid(b->text != NULL ? b->text : "", -1);
    gtk_text_buffer_set_text(it->buffer, valid, -1);
    g_free(valid);
    for (guint i = 0; b->runs != NULL && i < b->runs->len; i++) {
        const CanvasRun* r = &g_array_index(b->runs, CanvasRun, i);
        canvas_text_apply_styles(it->buffer, r->start, r->end, r->styles);
    }
    (void)loading;

    g_signal_connect(it->buffer, "insert-text", G_CALLBACK(on_insert_before), it);
    g_signal_connect_after(it->buffer, "insert-text", G_CALLBACK(on_insert_after), it);
    g_signal_connect(it->buffer, "mark-set", G_CALLBACK(on_mark_set), it);
    g_signal_connect(it->buffer, "changed", G_CALLBACK(on_buffer_changed), it);
    g_signal_connect_after(it->buffer, "apply-tag", G_CALLBACK(on_tag_changed), it);
    g_signal_connect_after(it->buffer, "remove-tag", G_CALLBACK(on_tag_changed), it);

    GtkGesture* click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_text_view_click), it);
    gtk_widget_add_controller(view, GTK_EVENT_CONTROLLER(click));

    canvas_item_place(it);
    return it;
}

static void on_img_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)area;
    CanvasItem* it = user_data;
    const gchar* dir = g_object_get_data(G_OBJECT(it->frame), DIR_KEY);
    if (it == NULL || it->src == NULL || dir == NULL) return;
    gchar* full = g_build_filename(dir, it->src, NULL);
    double cx = (it->crop_w > 0) ? it->crop_x : 0.0;
    double cy = (it->crop_h > 0) ? it->crop_y : 0.0;
    double cw = (it->crop_w > 0) ? it->crop_w : 1.0;
    double ch = (it->crop_h > 0) ? it->crop_h : 1.0;
    canvas_draw_cropped_image(cr, full, cx, cy, cw, ch, width, height);
    g_free(full);
}

static CanvasItem* add_image(CanvasPage* p, GtkWidget* surface, const gchar* dir,
                             const CanvasBlock* b, CanvasItemNotify on_change) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_cursor_from_name(area, "move");
    CanvasItem* it = item_base_new(p, surface, CANVAS_BLOCK_IMAGE, area);
    g_object_set_data(G_OBJECT(it->frame), NOTIFY_KEY, on_change);
    canvas_item_set_dir(it, dir);
    it->src = g_strdup(b->src);
    it->crop_x = b->crop_x;
    it->crop_y = b->crop_y;
    it->crop_w = (b->crop_w > 0) ? b->crop_w : 1.0;
    it->crop_h = (b->crop_h > 0) ? b->crop_h : 1.0;
    it->x = b->x;
    it->y = b->y;
    it->w = MAX(b->w, MIN_IMAGE_SIZE);
    it->h = MAX(b->h, MIN_IMAGE_SIZE);
    it->aspect = (gdouble)it->w / it->h;
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_img_draw, it, NULL);
    canvas_item_place(it);
    return it;
}

/* Draws from the item's own fields: the block it was made from is gone */
static void on_shape_draw(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)area;
    const CanvasItem* it = user_data;
    CanvasBlock view = {0};
    view.kind = CANVAS_BLOCK_SHAPE;
    view.shape_type = it->shape_type;
    view.stroke_color = it->stroke_color;
    view.fill_style = it->fill_style;
    view.stroke_width = it->stroke_width;
    view.rough = it->rough;
    view.points = it->points;
    canvas_rough_draw_shape(cr, &view, width, height);
}

static CanvasItem* add_shape(CanvasPage* p, GtkWidget* surface, const CanvasBlock* b,
                             CanvasItemNotify on_change) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_cursor_from_name(area, "move");
    CanvasItem* it = item_base_new(p, surface, CANVAS_BLOCK_SHAPE, area);
    g_object_set_data(G_OBJECT(it->frame), NOTIFY_KEY, on_change);
    it->shape_type = b->shape_type;
    it->stroke_color = g_strdup(b->stroke_color != NULL ? b->stroke_color : "fg");
    it->fill_style = b->fill_style;
    it->stroke_width = (b->stroke_width > 0) ? b->stroke_width : 2;
    it->rough = b->rough;
    if (b->points != NULL) {
        it->points = g_array_sized_new(FALSE, FALSE, sizeof(CanvasPoint), b->points->len);
        g_array_append_vals(it->points, b->points->data, b->points->len);
    }
    it->x = b->x;
    it->y = b->y;
    it->w = MAX(b->w, MIN_SHAPE_SIZE);
    it->h = MAX(b->h, MIN_SHAPE_SIZE);
    it->aspect = (gdouble)it->w / it->h;
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_shape_draw, it, NULL);
    canvas_item_place(it);
    return it;
}

static void on_tbl_change(CanvasBlock* b, gpointer user_data) {
    (void)b;
    CanvasItem* it = user_data;
    canvas_item_place(it);
    trigger_notify(it);
}

static CanvasItem* add_table(CanvasPage* p, GtkWidget* surface, const CanvasBlock* b,
                             CanvasItemNotify on_change) {
    CanvasItem* it = item_base_new(p, surface, CANVAS_BLOCK_TABLE, NULL);
    g_object_set_data(G_OBJECT(it->frame), NOTIFY_KEY, on_change);
    it->table_block = g_new0(CanvasBlock, 1);
    it->table_block->kind = CANVAS_BLOCK_TABLE;
    it->table_block->table_rows = b->table_rows;
    it->table_block->table_cols = b->table_cols;
    it->table_block->cells = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; b->cells != NULL && i < b->cells->len; i++) {
        g_ptr_array_add(it->table_block->cells, g_strdup(g_ptr_array_index(b->cells, i)));
    }
    it->table_block->col_widths = g_array_new(FALSE, TRUE, sizeof(gint));
    for (guint i = 0; b->col_widths != NULL && i < b->col_widths->len; i++) {
        gint cw = g_array_index(b->col_widths, gint, i);
        g_array_append_val(it->table_block->col_widths, cw);
    }
    GtkWidget* tbl = canvas_table_widget_new(it->table_block, on_tbl_change, it);
    it->content = tbl;
    /* Under the resize corner, like the content of the other blocks */
    gtk_overlay_set_child(GTK_OVERLAY(gtk_widget_get_last_child(it->frame)), tbl);
    it->x = b->x;
    it->y = b->y;
    it->w = MAX(b->w, 80);
    it->h = MAX(b->h, 40);
    canvas_item_place(it);
    return it;
}

CanvasItem* canvas_item_add(CanvasPage* p, GtkWidget* surface, const gchar* dir,
                            const CanvasBlock* b, CanvasItemNotify on_change,
                            gboolean loading) {
    if (b == NULL || surface == NULL) return NULL;
    switch (b->kind) {
    case CANVAS_BLOCK_TEXT:
        return add_text(p, surface, b, on_change, loading);
    case CANVAS_BLOCK_IMAGE:
        return add_image(p, surface, dir, b, on_change);
    case CANVAS_BLOCK_SHAPE:
        return add_shape(p, surface, b, on_change);
    case CANVAS_BLOCK_TABLE:
        return add_table(p, surface, b, on_change);
    default:
        return NULL;
    }
}

CanvasDoc* canvas_items_to_doc(GtkWidget* surface) {
    CanvasDoc* doc = canvas_doc_new();
    for (GtkWidget* c = gtk_widget_get_first_child(surface); c != NULL; c = gtk_widget_get_next_sibling(c)) {
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
