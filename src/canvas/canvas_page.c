/* Core of the Canvas page (see canvas_page_private.h): focus, undo history,
 * saving, new blocks, the tool. */
#include "canvas/canvas_page_private.h"
#include "core/app_events.h"
#include "canvas/canvas_bind.h"
#include "canvas/canvas_crop.h"
#include "canvas/canvas_doc.h"
#include "canvas/canvas_format_bar.h"
#include "canvas/canvas_table_bar.h"
#include "canvas/canvas_history.h"
#include "canvas/canvas_item.h"
#include "canvas/canvas_props.h"
#include "canvas/canvas_text.h"
#include "canvas/canvas_toolbar.h"
#include "canvas/canvas_viewport.h"
#include "explorer/explorer_row.h"
#include "explorer/tree_nav.h"

#include <string.h>

#define SAVE_DELAY_MS 800   /* autosave after the last change */
#define PAGE_MARGIN 400     /* room around blocks */
#define MAX_NEW_IMAGE_WIDTH 640
#define HISTORY_LIMIT 100   /* undo steps kept */

CanvasPage* canvas_page_new(AppData* app) {
    CanvasPage* p = g_new0(CanvasPage, 1);
    p->app = app;
    p->snap_grid = TRUE;
    p->history = canvas_history_new(HISTORY_LIMIT);
    p->selection = g_ptr_array_new_with_free_func(g_object_unref);
    return p;
}

void canvas_page_free(CanvasPage* p) {
    if (p->save_source != 0) g_source_remove(p->save_source);
    g_clear_weak_pointer(&p->last_focus);
    if (p->toolbar != NULL) canvas_toolbar_free(p->toolbar);
    if (p->props != NULL) canvas_props_free(p->props);
    canvas_format_bar_free(p->format_bar);
    canvas_table_bar_free(p->table_bar);
    canvas_rotate_free(p->rotate);
    canvas_history_free(p->history);
    if (p->viewport != NULL) canvas_viewport_free(p->viewport);
    g_free(p->drag_before);
    if (p->drag_group != NULL) g_ptr_array_unref(p->drag_group);
    if (p->drag_starts != NULL) g_array_unref(p->drag_starts);
    if (p->drag_points != NULL) g_array_unref(p->drag_points);
    g_ptr_array_unref(p->selection);
    g_clear_object(&p->tab_label);
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
    canvas_page_selection_clear(p);
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
/* Selection                                                                 */
/* ------------------------------------------------------------------------- */

#define SELECTED_CLASS CANVAS_SELECTED_CLASS

static gboolean in_set(CanvasPage* p, CanvasItem* it) {
    return it != NULL && g_ptr_array_find(p->selection, it->frame, NULL);
}

static void set_add(CanvasPage* p, CanvasItem* it) {
    if (it == NULL || in_set(p, it)) return;
    g_ptr_array_add(p->selection, g_object_ref(it->frame));
    gtk_widget_add_css_class(it->frame, SELECTED_CLASS);
}

static void set_remove(CanvasPage* p, CanvasItem* it) {
    if (it == NULL) return;
    gtk_widget_remove_css_class(it->frame, SELECTED_CLASS);
    g_ptr_array_remove(p->selection, it->frame);
}

void canvas_page_selection_clear(CanvasPage* p) {
    for (guint i = 0; i < p->selection->len; i++) {
        gtk_widget_remove_css_class(g_ptr_array_index(p->selection, i), SELECTED_CLASS);
    }
    g_ptr_array_set_size(p->selection, 0);
}

gboolean canvas_page_is_selected(CanvasPage* p, CanvasItem* it) {
    return it != NULL && (in_set(p, it) || it == canvas_page_current_item(p));
}

GPtrArray* canvas_page_selected(CanvasPage* p) {
    GPtrArray* items = g_ptr_array_new();
    CanvasItem* current = canvas_page_current_item(p);
    for (GtkWidget* c = gtk_widget_get_first_child(p->surface); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        CanvasItem* it = canvas_item_of(c);
        if (it != NULL && (it == current || in_set(p, it))) g_ptr_array_add(items, it);
    }
    return items;
}

GPtrArray* canvas_page_selected_unlocked(CanvasPage* p) {
    GPtrArray* items = canvas_page_selected(p);
    for (guint i = items->len; i > 0; i--) {
        if (((CanvasItem*)g_ptr_array_index(items, i - 1))->locked) g_ptr_array_remove_index(items, i - 1);
    }
    return items;
}

void canvas_page_toggle_lock(CanvasPage* p) {
    GPtrArray* items = canvas_page_selected(p);
    gboolean lock = FALSE;
    for (guint i = 0; i < items->len; i++) {
        if (!((CanvasItem*)g_ptr_array_index(items, i))->locked) lock = TRUE;
    }
    if (items->len > 0) canvas_page_remember(p);
    for (guint i = 0; i < items->len; i++) canvas_item_set_locked(g_ptr_array_index(items, i), lock);
    g_ptr_array_unref(items);
    canvas_page_schedule_save(p);
    canvas_page_update_props(p);
}

void canvas_page_select_toggle(CanvasPage* p, CanvasItem* it) {
    if (it == NULL) return;
    CanvasItem* current = canvas_page_current_item(p);
    if (!canvas_page_is_selected(p, it)) {
        set_add(p, current);
        set_add(p, it);
        canvas_item_select(it);
        canvas_page_update_props(p);
        return;
    }
    set_remove(p, it);
    if (it != current) {
        canvas_page_update_props(p);
        return;
    }
    /* The focused block left the selection: focus another one */
    g_clear_weak_pointer(&p->last_focus);
    if (p->selection->len > 0) {
        canvas_item_select(canvas_item_of(g_ptr_array_index(p->selection, p->selection->len - 1)));
    } else {
        GtkRoot* root = gtk_widget_get_root(p->surface);
        if (root != NULL) gtk_root_set_focus(root, p->surface);
    }
}

void canvas_page_item_box(CanvasItem* it, gint* x, gint* y, gint* w, gint* h) {
    *x = it->x;
    *y = it->y;
    *w = MAX(it->w, gtk_widget_get_width(it->frame));
    *h = MAX(it->h, gtk_widget_get_height(it->frame));
}

void canvas_page_select_box(CanvasPage* p, gint x, gint y, gint w, gint h, gboolean extend) {
    if (extend) {
        set_add(p, canvas_page_current_item(p));
    } else {
        canvas_page_selection_clear(p);
    }
    CanvasItem* last = NULL;
    for (GtkWidget* c = gtk_widget_get_first_child(p->surface); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        CanvasItem* it = canvas_item_of(c);
        if (it == NULL) continue;
        if (it->locked) continue; /* a band does not take locked blocks */
        CanvasRect b;
        canvas_item_bounds(it, &b);
        if (b.x >= x && b.y >= y && b.x + b.w <= x + w && b.y + b.h <= y + h) {
            set_add(p, it);
            last = it;
        }
    }
    /* Focus the top block, so the keys act on the selection */
    if (last != NULL) {
        canvas_item_select(last);
    } else if (p->selection->len > 0) {
        canvas_item_select(canvas_item_of(g_ptr_array_index(p->selection, p->selection->len - 1)));
    }
    canvas_page_update_props(p);
}

void canvas_page_select_all(CanvasPage* p) {
    canvas_page_selection_clear(p);
    CanvasItem* last = NULL;
    for (GtkWidget* c = gtk_widget_get_first_child(p->surface); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        CanvasItem* it = canvas_item_of(c);
        if (it == NULL || it->locked) continue;
        set_add(p, it);
        last = it;
    }
    /* Focus the top block, so the keys act on the selection */
    if (last != NULL) canvas_item_select(last);
    canvas_page_update_props(p);
}

void canvas_page_focus_changed(CanvasPage* p, GtkWidget* f) {
    if (f == NULL || !gtk_widget_is_ancestor(f, p->surface)) return;
    CanvasItem* it = canvas_item_of(f);
    if (it != NULL && !in_set(p, it)) canvas_page_selection_clear(p);
}

/* ------------------------------------------------------------------------- */
/* Changes and Saving                                                        */
/* ------------------------------------------------------------------------- */

void canvas_page_update_extent(CanvasPage* p) {
    /* After any change: the bound arrows follow their blocks */
    canvas_bind_follow(p->surface);
    gint w = 0, h = 0;
    for (GtkWidget* c = gtk_widget_get_first_child(p->surface); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        CanvasItem* it = canvas_item_of(c);
        if (it == NULL) continue;
        CanvasRect b;
        canvas_item_bounds(it, &b);
        w = MAX(w, b.x + b.w);
        h = MAX(h, b.y + b.h);
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
    /* The preview of this note shows the saved page (the file watcher only
     * sees the note's folder, not page.xml in it) */
    ExplorerRow* row = p->app != NULL ? tree_nav_selected(p->app) : NULL;
    if (row != NULL && g_strcmp0(row->path, p->dir) == 0) app_emit(p->app, APP_EVENT_PREVIEW_STALE);
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

/* The id after the highest one on the page */
static gint next_id(CanvasPage* p) {
    gint id = 0;
    for (GtkWidget* c = gtk_widget_get_first_child(p->surface); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        CanvasItem* it = canvas_item_of(c);
        if (it != NULL) id = MAX(id, it->id);
    }
    return id + 1;
}

/* The cursor moved or the text changed in a text box (after its own
 * handlers, which clear the pending styles) */
static void on_text_moved(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    CanvasPage* p = g_object_get_data(G_OBJECT(data), CANVAS_PAGE_KEY);
    if (p != NULL && !p->closed) canvas_page_update_format(p);
}

static void on_text_mark_set(GtkTextBuffer* buffer, const GtkTextIter* location, GtkTextMark* mark, gpointer data) {
    (void)location;
    if (mark == gtk_text_buffer_get_insert(buffer) || mark == gtk_text_buffer_get_selection_bound(buffer)) {
        on_text_moved(buffer, data);
    }
}

static void on_text_tag(GtkTextBuffer* buffer, GtkTextTag* tag, const GtkTextIter* s, const GtkTextIter* e,
                        gpointer data) {
    (void)tag;
    (void)s;
    (void)e;
    on_text_moved(buffer, data);
}

/* A block of the note (loading), or a new one: given a new id */
static CanvasItem* add_item(CanvasPage* p, const CanvasBlock* b) {
    gint id = p->loading ? 0 : next_id(p);
    CanvasItem* it = canvas_item_add(p, p->surface, p->dir, b, on_item_changed_notify, p->loading);
    if (it != NULL && id > 0) it->id = id;
    if (it != NULL && it->kind == CANVAS_BLOCK_TEXT) {
        g_signal_connect_object(it->buffer, "mark-set", G_CALLBACK(on_text_mark_set), p->root, G_CONNECT_AFTER);
        g_signal_connect_object(it->buffer, "changed", G_CALLBACK(on_text_moved), p->root, G_CONNECT_AFTER);
        g_signal_connect_object(it->buffer, "apply-tag", G_CALLBACK(on_text_tag), p->root, G_CONNECT_AFTER);
        g_signal_connect_object(it->buffer, "remove-tag", G_CALLBACK(on_text_tag), p->root, G_CONNECT_AFTER);
    }
    return it;
}

static void clear_surface(CanvasPage* p) {
    canvas_rotate_hide(p->rotate);
    canvas_page_selection_clear(p);
    g_clear_weak_pointer(&p->last_focus);
    p->band = NULL;
    p->band_pending = FALSE;
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
    if (p->drag_item != NULL || p->draw_item != NULL || p->erasing || p->band != NULL) return;
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
    set_remove(p, it);
    canvas_bind_forget(p->surface, it->id);
    gtk_fixed_remove(GTK_FIXED(p->surface), it->frame);
    canvas_page_schedule_save(p);
}

CanvasDoc* canvas_page_selected_doc(CanvasPage* p) {
    GPtrArray* items = canvas_page_selected(p);
    CanvasDoc* all = canvas_items_to_doc(p->surface);
    CanvasDoc* doc = canvas_doc_new();
    /* The blocks of the page document, taken in its order */
    for (guint i = 0; i < all->blocks->len;) {
        CanvasBlock* b = g_ptr_array_index(all->blocks, i);
        gboolean keep = FALSE;
        for (guint k = 0; k < items->len && !keep; k++) {
            keep = ((CanvasItem*)g_ptr_array_index(items, k))->id == b->id;
        }
        if (keep) {
            g_ptr_array_add(doc->blocks, g_ptr_array_steal_index(all->blocks, i));
        } else {
            i++;
        }
    }
    canvas_doc_free(all);
    g_ptr_array_unref(items);
    return doc;
}

/* A picture of the note in from_dir copied into this one: the new src
 * (free it), or NULL if it cannot be copied */
static gchar* copy_picture(CanvasPage* p, const gchar* from_dir, const gchar* src) {
    const gchar* dot = strrchr(src, '.');
    gchar* dest_src = canvas_doc_new_media_path(p->dir, dot != NULL ? dot + 1 : "png");
    gchar* from = g_build_filename(from_dir, src, NULL);
    gchar* to = g_build_filename(p->dir, dest_src, NULL);
    gchar* media = g_path_get_dirname(to);
    g_mkdir_with_parents(media, 0755);
    GFile* a = g_file_new_for_path(from);
    GFile* b = g_file_new_for_path(to);
    g_autoptr(GError) error = NULL;
    if (!g_file_copy(a, b, G_FILE_COPY_NONE, NULL, NULL, NULL, &error)) {
        g_warning("canvas: cannot copy the picture %s: %s", from, error->message);
        g_clear_pointer(&dest_src, g_free);
    }
    g_object_unref(a);
    g_object_unref(b);
    g_free(media);
    g_free(to);
    g_free(from);
    return dest_src;
}

guint canvas_page_add_copies(CanvasPage* p, CanvasDoc* doc, gint dx, gint dy, const gchar* from_dir) {
    if (p->dir == NULL || doc == NULL || doc->blocks->len == 0) return 0;
    gboolean other_note = from_dir != NULL && g_strcmp0(from_dir, p->dir) != 0;
    canvas_page_remember(p);
    canvas_page_selection_clear(p);
    GHashTable* ids = g_hash_table_new(g_direct_hash, g_direct_equal); /* old id -> new id */
    GPtrArray* added = g_ptr_array_new();
    for (guint i = 0; i < doc->blocks->len; i++) {
        CanvasBlock* b = g_ptr_array_index(doc->blocks, i);
        if (b->kind == CANVAS_BLOCK_IMAGE && other_note) {
            gchar* src = copy_picture(p, from_dir, b->src);
            if (src == NULL) continue;
            g_free(b->src);
            b->src = src;
        }
        b->x = MAX(b->x + dx, 0);
        b->y = MAX(b->y + dy, 0);
        CanvasItem* it = add_item(p, b);
        if (it == NULL) continue;
        g_hash_table_insert(ids, GINT_TO_POINTER(b->id), GINT_TO_POINTER(it->id));
        g_ptr_array_add(added, it);
    }
    /* Lines keep only the bindings to blocks copied with them */
    for (guint i = 0; i < added->len; i++) {
        CanvasItem* it = g_ptr_array_index(added, i);
        it->start_id = GPOINTER_TO_INT(g_hash_table_lookup(ids, GINT_TO_POINTER(it->start_id)));
        it->end_id = GPOINTER_TO_INT(g_hash_table_lookup(ids, GINT_TO_POINTER(it->end_id)));
        set_add(p, it);
    }
    guint n = added->len;
    if (n > 0) canvas_item_select(g_ptr_array_index(added, n - 1));
    g_ptr_array_unref(added);
    g_hash_table_unref(ids);
    canvas_page_update_extent(p);
    canvas_page_schedule_save(p);
    return n;
}

void canvas_page_remove_selected(CanvasPage* p) {
    GPtrArray* items = canvas_page_selected_unlocked(p);
    if (items->len == 0) {
        g_ptr_array_unref(items);
        return;
    }
    canvas_page_remember(p);
    canvas_page_selection_clear(p);
    for (guint i = 0; i < items->len; i++) {
        CanvasItem* it = g_ptr_array_index(items, i);
        canvas_bind_forget(p->surface, it->id);
        gtk_fixed_remove(GTK_FIXED(p->surface), it->frame);
    }
    g_ptr_array_unref(items);
    canvas_page_deselect(p);
    canvas_page_update_extent(p);
    canvas_page_schedule_save(p);
}

/* The next (step 1) or previous (-1) block on the page after w */
static GtkWidget* sibling_block(GtkWidget* w, gint step) {
    do {
        w = step > 0 ? gtk_widget_get_next_sibling(w) : gtk_widget_get_prev_sibling(w);
    } while (w != NULL && canvas_item_of(w) == NULL);
    return w;
}

void canvas_page_reorder_selected(CanvasPage* p, gint where) {
    GPtrArray* items = canvas_page_selected(p);
    if (items->len > 0) canvas_page_remember(p);
    guint n = items->len;
    for (guint k = 0; k < n; k++) {
        /* Going up: the top block first, so each one can pass the next */
        CanvasItem* it = g_ptr_array_index(items, where > 0 ? n - 1 - k : k);
        GtkWidget* f = it->frame;
        if (where == 2) {
            /* After the blocks above it moved to the front */
            GtkWidget* before = k == 0 ? NULL : ((CanvasItem*)g_ptr_array_index(items, n - k))->frame;
            gtk_widget_insert_before(f, p->surface, before);
        } else if (where == -2) {
            GtkWidget* after = k == 0 ? NULL : ((CanvasItem*)g_ptr_array_index(items, k - 1))->frame;
            gtk_widget_insert_after(f, p->surface, after);
        } else {
            GtkWidget* s = sibling_block(f, where);
            if (s == NULL || canvas_page_is_selected(p, canvas_item_of(s))) continue;
            if (where > 0) {
                gtk_widget_insert_after(f, p->surface, s);
            } else {
                gtk_widget_insert_before(f, p->surface, s);
            }
        }
    }
    g_ptr_array_unref(items);
    canvas_page_schedule_save(p);
}

void canvas_page_move_selected(CanvasPage* p, gint dx, gint dy) {
    GPtrArray* items = canvas_page_selected_unlocked(p);
    if (items->len > 0) canvas_page_remember(p);
    /* The blocks keep their places relative to each other at the page edge */
    for (guint i = 0; i < items->len; i++) {
        CanvasItem* it = g_ptr_array_index(items, i);
        dx = MAX(dx, -it->x);
        dy = MAX(dy, -it->y);
    }
    for (guint i = 0; i < items->len; i++) {
        CanvasItem* it = g_ptr_array_index(items, i);
        it->x += dx;
        it->y += dy;
        canvas_item_place(it);
    }
    canvas_bind_release(items);
    g_ptr_array_unref(items);
    canvas_page_update_extent(p);
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

void canvas_page_update_format(CanvasPage* p) {
    CanvasItem* it = canvas_page_current_item(p);
    if (it == NULL || it->kind != CANVAS_BLOCK_TEXT || it->buffer == NULL) return;
    canvas_format_bar_set_styles(p->format_bar, canvas_text_active_styles(it->buffer, it->pending));
}

/* The panel shows the style of the next shapes while drawing, or of the
 * current shape; the format bar floats above the current text box, the
 * table bar above the current table */
void canvas_page_update_props(CanvasPage* p) {
    if (p->props == NULL) return;
    CanvasItem* it = p->tool == CANVAS_TOOL_SELECT ? canvas_page_current_item(p) : NULL;
    canvas_format_bar_show_for(p->format_bar, it != NULL && it->kind == CANVAS_BLOCK_TEXT ? it->frame : NULL);
    canvas_page_update_format(p);
    gboolean table = it != NULL && it->kind == CANVAS_BLOCK_TABLE;
    canvas_table_bar_show_for(p->table_bar, table ? it->frame : NULL, table ? it->content : NULL);
    canvas_rotate_update(p->rotate);
    if (canvas_tool_draws(p->tool)) {
        canvas_props_show_shape(p->props, &p->style, closed_shape(canvas_tool_shape(p->tool)));
    } else if (it != NULL && it->kind == CANVAS_BLOCK_SHAPE) {
        CanvasShapeStyle style = {it->stroke_color, it->fill_style, it->stroke_width, it->rough};
        canvas_props_show_shape(p->props, &style, closed_shape(it->shape_type));
    } else {
        canvas_props_hide_shape(p->props);
    }
    /* Actions on the selection: not while a block is edited (its text or
     * label has the focus), unless others are selected with it */
    CanvasPropsSelection sel = {0};
    if (it != NULL && (p->last_focus == it->frame || p->selection->len > 1)) {
        GPtrArray* items = canvas_page_selected(p);
        sel.selected = items->len;
        sel.locked = items->len > 0;
        for (guint i = 0; i < items->len; i++) {
            if (((CanvasItem*)g_ptr_array_index(items, i))->locked) {
                continue;
            }
            sel.movable++;
            sel.locked = FALSE;
        }
        sel.picture = items->len == 1 && it->kind == CANVAS_BLOCK_IMAGE;
        g_ptr_array_unref(items);
    }
    canvas_props_show_selection(p->props, &sel);
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
