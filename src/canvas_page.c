/* Core of the Canvas page (see canvas_page_private.h): focus, undo history,
 * saving, new blocks, the tool. */
#include "canvas_page_private.h"
#include "canvas_crop.h"
#include "canvas_doc.h"
#include "canvas_history.h"
#include "canvas_item.h"
#include "canvas_props.h"
#include "canvas_toolbar.h"
#include "canvas_viewport.h"

#define SAVE_DELAY_MS 800   /* autosave after the last change */
#define PAGE_MARGIN 400     /* room around blocks */
#define MAX_NEW_IMAGE_WIDTH 640
#define HISTORY_LIMIT 100   /* undo steps kept */

CanvasPage* canvas_page_new(AppData* app) {
    CanvasPage* p = g_new0(CanvasPage, 1);
    p->app = app;
    p->snap_grid = TRUE;
    p->history = canvas_history_new(HISTORY_LIMIT);
    return p;
}

void canvas_page_free(CanvasPage* p) {
    if (p->save_source != 0) g_source_remove(p->save_source);
    g_clear_weak_pointer(&p->last_focus);
    if (p->toolbar != NULL) canvas_toolbar_free(p->toolbar);
    if (p->props != NULL) canvas_props_free(p->props);
    canvas_history_free(p->history);
    if (p->viewport != NULL) canvas_viewport_free(p->viewport);
    g_free(p->drag_before);
    g_free(p->dir);
    g_free(p);
}

/* ------------------------------------------------------------------------- */
/* Focus                                                                     */
/* ------------------------------------------------------------------------- */

GtkWidget* canvas_page_focused_widget(CanvasPage* p) {
    GtkRoot* root = gtk_widget_get_root(p->root);
    return root != NULL ? gtk_root_get_focus(root) : NULL;
}

CanvasItem* canvas_page_focused_item(CanvasPage* p) {
    GtkWidget* f = canvas_page_focused_widget(p);
    return (f != NULL && gtk_widget_is_ancestor(f, p->surface)) ? canvas_item_of(f) : NULL;
}

CanvasItem* canvas_page_current_item(CanvasPage* p) {
    CanvasItem* it = canvas_page_focused_item(p);
    if (it != NULL) return it;
    if (p->last_focus != NULL && gtk_widget_is_ancestor(p->last_focus, p->surface)) {
        return canvas_item_of(p->last_focus);
    }
    return NULL;
}

void canvas_page_restore_focus(CanvasPage* p) {
    if (p->last_focus != NULL && gtk_widget_is_ancestor(p->last_focus, p->surface)) {
        gtk_widget_grab_focus(p->last_focus);
    }
}

void canvas_page_deselect(CanvasPage* p) {
    g_clear_weak_pointer(&p->last_focus);
    GtkRoot* root = gtk_widget_get_root(p->surface);
    if (root != NULL) gtk_root_set_focus(root, p->surface);
}

void canvas_page_focus_next(CanvasPage* p, CanvasItem* from, gint step) {
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

/* ------------------------------------------------------------------------- */
/* Changes and Saving                                                        */
/* ------------------------------------------------------------------------- */

void canvas_page_update_extent(CanvasPage* p) {
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
gchar* canvas_page_state(CanvasPage* p) {
    g_autoptr(CanvasDoc) doc = canvas_items_to_doc(p->surface);
    return canvas_doc_to_xml(doc);
}

void canvas_page_remember(CanvasPage* p) {
    if (p->loading || p->dir == NULL) return;
    gchar* state = canvas_page_state(p);
    canvas_history_push(p->history, state);
    g_free(state);
}

gboolean canvas_page_save_now(CanvasPage* p) {
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
    canvas_page_update_extent(p);
    return TRUE;
}

static gboolean on_save_timeout(gpointer data) {
    CanvasPage* p = data;
    p->save_source = 0;
    canvas_page_save_now(p);
    return G_SOURCE_REMOVE;
}

void canvas_page_schedule_save(CanvasPage* p) {
    if (p->loading || p->dir == NULL) return;
    p->dirty = TRUE;
    if (p->save_source != 0) g_source_remove(p->save_source);
    p->save_source = g_timeout_add(SAVE_DELAY_MS, on_save_timeout, p);
}

/* Delete the pictures no block uses, once the history is dropped */
void canvas_page_prune_media(CanvasPage* p) {
    if (p->dir == NULL || p->closed) return;
    g_autoptr(CanvasDoc) doc = canvas_items_to_doc(p->surface);
    canvas_doc_prune_media(doc, p->dir);
}

/* Text typed or a table cell edited: their own widgets undo it */
static void on_item_changed_notify(CanvasPage* p) {
    if (!p->loading) canvas_history_clear_redo(p->history);
    canvas_page_update_extent(p);
    canvas_page_schedule_save(p);
}

static CanvasItem* add_item(CanvasPage* p, const CanvasBlock* b) {
    return canvas_item_add(p, p->surface, p->dir, b, on_item_changed_notify, p->loading);
}

static void clear_surface(CanvasPage* p) {
    g_clear_weak_pointer(&p->last_focus);
    GtkWidget* c;
    while ((c = gtk_widget_get_first_child(p->surface)) != NULL) {
        gtk_fixed_remove(GTK_FIXED(p->surface), c);
    }
}

void canvas_page_load(CanvasPage* p, const gchar* dir, const gchar* title) {
    canvas_page_save_now(p);
    canvas_page_prune_media(p);
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
    canvas_page_update_extent(p);
    canvas_viewport_reset(p->viewport);
    canvas_page_set_tool(p, CANVAS_TOOL_SELECT);
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
    canvas_page_update_extent(p);
    canvas_page_schedule_save(p);
    canvas_page_deselect(p);
    canvas_page_update_props(p);
}

void canvas_page_undo(CanvasPage* p, gboolean redo) {
    if (p->drag_item != NULL || p->draw_item != NULL || p->erasing) return;
    if (!(redo ? canvas_history_can_redo(p->history) : canvas_history_can_undo(p->history))) return;
    gchar* now = canvas_page_state(p);
    restore(p, redo ? canvas_history_redo(p->history, now) : canvas_history_undo(p->history, now));
    g_free(now);
}

/* ------------------------------------------------------------------------- */
/* Blocks                                                                    */
/* ------------------------------------------------------------------------- */

void canvas_page_remove_item(CanvasItem* it) {
    CanvasPage* p = it->page;
    canvas_page_remember(p);
    gtk_fixed_remove(GTK_FIXED(p->surface), it->frame);
    canvas_page_schedule_save(p);
}

void canvas_page_new_position(CanvasPage* p, gboolean at_pointer, gint* x, gint* y) {
    CanvasItem* sel = canvas_page_focused_item(p);
    if (at_pointer && p->pointer_in) {
        *x = (gint)p->pointer_x;
        *y = (gint)p->pointer_y;
    } else if (sel != NULL) {
        *x = sel->x;
        *y = sel->y + MAX(sel->h, gtk_widget_get_height(sel->frame)) + CANVAS_NEW_OFFSET;
    } else {
        canvas_viewport_origin(p->viewport, x, y);
        *x += CANVAS_NEW_OFFSET;
        *y += CANVAS_NEW_OFFSET;
    }
}

/* A new block (from a temporary document) added to the page */
static CanvasItem* add_new(CanvasPage* p, const CanvasBlock* b) {
    CanvasItem* it = add_item(p, b);
    canvas_page_update_extent(p);
    canvas_page_schedule_save(p);
    return it;
}

CanvasItem* canvas_page_new_text_box(CanvasPage* p, gint x, gint y, const gchar* text) {
    canvas_page_remember(p);
    g_autoptr(CanvasDoc) tmp = canvas_doc_new();
    CanvasItem* it = add_new(p, canvas_doc_add_text(tmp, x, y, 0, text));
    canvas_item_edit(it);
    return it;
}

void canvas_page_apply_style(CanvasItem* it, const CanvasShapeStyle* style) {
    g_free(it->stroke_color);
    it->stroke_color = g_strdup(style->stroke_color);
    it->fill_style = style->fill;
    it->stroke_width = style->stroke_width;
    it->rough = style->rough;
    if (it->content != NULL) gtk_widget_queue_draw(it->content);
}

CanvasItem* canvas_page_new_shape(CanvasPage* p, gint x, gint y, gint w, gint h, CanvasShapeType type) {
    canvas_page_remember(p);
    g_autoptr(CanvasDoc) tmp = canvas_doc_new();
    CanvasItem* it = add_item(p, canvas_doc_add_shape(tmp, x, y, w, h, type));
    canvas_page_apply_style(it, &p->style);
    canvas_page_update_extent(p);
    canvas_page_schedule_save(p);
    canvas_item_select(it);
    return it;
}

CanvasItem* canvas_page_new_table(CanvasPage* p, gint x, gint y, gint rows, gint cols) {
    canvas_page_remember(p);
    g_autoptr(CanvasDoc) tmp = canvas_doc_new();
    CanvasItem* it = add_new(p, canvas_doc_add_table(tmp, x, y, rows, cols));
    canvas_item_select(it);
    return it;
}

CanvasItem* canvas_page_new_picture(CanvasPage* p, const gchar* src, gint x, gint y, gint width, gint height) {
    if (width <= 0 || height <= 0) return NULL;
    if (width > MAX_NEW_IMAGE_WIDTH) {
        height = (gint)((gdouble)height * MAX_NEW_IMAGE_WIDTH / width);
        width = MAX_NEW_IMAGE_WIDTH;
    }
    canvas_page_remember(p);
    g_autoptr(CanvasDoc) tmp = canvas_doc_new();
    CanvasItem* it = add_new(p, canvas_doc_add_image(tmp, x, y, width, height, src));
    canvas_item_select(it);
    return it;
}

/* user_data: a reference on the picture's frame, so the item stays alive
 * while the dialog is open; ignored if the picture left the page meanwhile */
static void on_crop_applied(double x, double y, double w, double h, gpointer user_data) {
    GtkWidget* frame = user_data;
    CanvasItem* it = canvas_item_of(frame);
    if (it == NULL || gtk_widget_get_parent(frame) == NULL) return;
    canvas_page_remember(it->page);
    it->crop_x = x;
    it->crop_y = y;
    it->crop_w = w;
    it->crop_h = h;
    gtk_widget_queue_draw(it->content);
    canvas_page_update_extent(it->page);
    canvas_page_schedule_save(it->page);
}

void canvas_page_open_crop(CanvasPage* p, CanvasItem* it) {
    if (it == NULL || it->kind != CANVAS_BLOCK_IMAGE || it->src == NULL || p->dir == NULL) return;
    GtkRoot* root = gtk_widget_get_root(p->root);
    if (root == NULL) return;
    gchar* full = g_build_filename(p->dir, it->src, NULL);
    canvas_crop_dialog_show(GTK_WINDOW(root), full, it->crop_x, it->crop_y, it->crop_w, it->crop_h,
                            on_crop_applied, g_object_ref(it->frame), g_object_unref);
    g_free(full);
}

/* ------------------------------------------------------------------------- */
/* Tool and Properties                                                       */
/* ------------------------------------------------------------------------- */

static gboolean closed_shape(CanvasShapeType type) {
    return type != CANVAS_SHAPE_LINE && type != CANVAS_SHAPE_ARROW && type != CANVAS_SHAPE_FREEHAND;
}

/* The panel shows the style of the next shapes while drawing, else what the
 * current block can change */
void canvas_page_update_props(CanvasPage* p) {
    if (p->props == NULL) return;
    if (canvas_tool_draws(p->tool)) {
        canvas_props_show_shape(p->props, &p->style, closed_shape(canvas_tool_shape(p->tool)));
        return;
    }
    CanvasItem* it = p->tool == CANVAS_TOOL_SELECT ? canvas_page_current_item(p) : NULL;
    if (it != NULL && it->kind == CANVAS_BLOCK_SHAPE) {
        CanvasShapeStyle style = {it->stroke_color, it->fill_style, it->stroke_width, it->rough};
        canvas_props_show_shape(p->props, &style, closed_shape(it->shape_type));
    } else if (it != NULL && it->kind == CANVAS_BLOCK_TEXT) {
        canvas_props_show_text(p->props);
    } else {
        canvas_props_hide(p->props);
    }
}

void canvas_page_set_tool(CanvasPage* p, CanvasTool tool) {
    p->tool = tool;
    canvas_toolbar_set_tool(p->toolbar, tool);
    const gchar* cursor = NULL;
    if (canvas_tool_draws(tool) || tool == CANVAS_TOOL_ERASER || tool == CANVAS_TOOL_TABLE) {
        cursor = "crosshair";
    } else if (tool == CANVAS_TOOL_TEXT) {
        cursor = "text";
    }
    gtk_widget_set_cursor_from_name(p->surface, cursor);
    canvas_page_update_props(p);
}
