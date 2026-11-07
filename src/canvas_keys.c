/* Keys of the Canvas page (see canvas_keys.h) */
#include "canvas_keys.h"
#include "canvas_clip.h"
#include "canvas_item.h"
#include "canvas_media.h"
#include "canvas_page_private.h"
#include "canvas_text.h"
#include "canvas_toolbar.h"
#include "canvas_viewport.h"

#define NUDGE 8      /* arrows move a selected block by this */
#define NUDGE_BIG 40 /* Shift+arrows */

static gboolean text_key(CanvasItem* it, guint keyval, GdkModifierType state) {
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    gboolean alt = (state & GDK_ALT_MASK) != 0;

    if (keyval == GDK_KEY_Escape && !ctrl && !alt) {
        canvas_item_select(it);
        return TRUE;
    }
    if ((keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab) && !ctrl && !alt) {
        canvas_page_focus_next(it->page, it, keyval == GDK_KEY_Tab ? 1 : -1);
        return TRUE;
    }
    return canvas_text_key(GTK_TEXT_VIEW(it->content), &it->pending, keyval, state);
}

/* Keys on a selected block: Delete and the arrows act on every selected
 * block */
static gboolean block_key(CanvasPage* p, CanvasItem* it, guint keyval, GdkModifierType state) {
    if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) != 0) return FALSE;
    gint step = (state & GDK_SHIFT_MASK) ? NUDGE_BIG : NUDGE;
    switch (keyval) {
    case GDK_KEY_Delete:
    case GDK_KEY_KP_Delete:
    case GDK_KEY_BackSpace:
        canvas_page_remove_selected(p);
        return TRUE;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
        canvas_item_edit(it);
        return TRUE;
    case GDK_KEY_Escape:
        canvas_page_deselect(p);
        return TRUE;
    case GDK_KEY_Tab:
        canvas_page_focus_next(p, it, 1);
        return TRUE;
    case GDK_KEY_ISO_Left_Tab:
        canvas_page_focus_next(p, it, -1);
        return TRUE;
    case GDK_KEY_Left: canvas_page_move_selected(p, -step, 0); return TRUE;
    case GDK_KEY_Right: canvas_page_move_selected(p, step, 0); return TRUE;
    case GDK_KEY_Up: canvas_page_move_selected(p, 0, -step); return TRUE;
    case GDK_KEY_Down: canvas_page_move_selected(p, 0, step); return TRUE;
    default: return FALSE;
    }
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
    CanvasPage* p = g_object_get_data(G_OBJECT(job->root), CANVAS_PAGE_KEY);
    if (texture != NULL && p != NULL && !p->closed && p->dir != NULL && p->generation == job->generation) {
        gchar* src = canvas_media_save_texture(texture, p->dir);
        if (src != NULL) {
            canvas_page_new_picture(p, src, job->x, job->y, gdk_texture_get_width(texture),
                                    gdk_texture_get_height(texture));
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
static gboolean paste_picture(CanvasPage* p) {
    GdkClipboard* clip = gtk_widget_get_clipboard(p->root);
    if (!canvas_media_clipboard_has_picture(clip)) return FALSE;
    CanvasItem* it = canvas_page_focused_item(p);
    if (it != NULL && canvas_page_focused_widget(p) != it->frame && canvas_media_clipboard_has_text(clip)) {
        return FALSE;
    }
    PasteJob* job = g_new0(PasteJob, 1);
    job->root = g_object_ref(p->root);
    job->generation = p->generation;
    canvas_page_new_position(p, TRUE, &job->x, &job->y);
    gdk_clipboard_read_texture_async(clip, NULL, on_paste_read, job);
    return TRUE;
}

/* Ctrl+= / Ctrl+- / Ctrl+0 (the window's font sizes elsewhere:
 * canvas_view_claims_key()) */
gint canvas_keys_zoom_step(guint keyval, GdkModifierType state) {
    if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) != GDK_CONTROL_MASK) return G_MININT;
    gboolean shift = (state & GDK_SHIFT_MASK) != 0;
    if (keyval == GDK_KEY_equal || keyval == GDK_KEY_plus || keyval == GDK_KEY_KP_Add) return 1;
    if (!shift && (keyval == GDK_KEY_minus || keyval == GDK_KEY_KP_Subtract)) return -1;
    if (!shift && (keyval == GDK_KEY_0 || keyval == GDK_KEY_KP_0)) return 0;
    return G_MININT;
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
    GtkWidget* f = canvas_page_focused_widget(p);
    CanvasItem* it = canvas_page_focused_item(p);
    /* The page or a block focused, not typing in it */
    gboolean on_blocks = f == p->surface || (it != NULL && f == it->frame);
    if (control && !other && (keyval == GDK_KEY_v || keyval == GDK_KEY_V)) {
        if (on_blocks && canvas_clip_paste(p)) return TRUE;
        return paste_picture(p);
    }
    /* Ctrl+Shift+K: lock / unlock the selected blocks (Ctrl+Shift+L is the
     * window layout) */
    if (control && (state & GDK_ALT_MASK) == 0 && (state & GDK_SHIFT_MASK) != 0 && on_blocks && it != NULL &&
        (keyval == GDK_KEY_k || keyval == GDK_KEY_K)) {
        canvas_page_toggle_lock(p);
        return TRUE;
    }
    /* Ctrl+] / Ctrl+[: one step up / down; with Shift: to the front / back */
    if (control && (state & GDK_ALT_MASK) == 0 && on_blocks && it != NULL) {
        gboolean shift = (state & GDK_SHIFT_MASK) != 0;
        if (keyval == GDK_KEY_bracketright || keyval == GDK_KEY_braceright) {
            canvas_page_reorder_selected(p, shift ? 2 : 1);
            return TRUE;
        }
        if (keyval == GDK_KEY_bracketleft || keyval == GDK_KEY_braceleft) {
            canvas_page_reorder_selected(p, shift ? -2 : -1);
            return TRUE;
        }
    }
    /* Ctrl+C / Ctrl+X / Ctrl+D: copy, cut, duplicate the selected blocks */
    if (control && !other && on_blocks && it != NULL) {
        if (keyval == GDK_KEY_c || keyval == GDK_KEY_C) return canvas_clip_copy(p, FALSE);
        if (keyval == GDK_KEY_x || keyval == GDK_KEY_X) return canvas_clip_copy(p, TRUE);
        if (keyval == GDK_KEY_d || keyval == GDK_KEY_D) {
            canvas_clip_duplicate(p);
            return TRUE;
        }
    }
    if (control && !other && (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter)) {
        gint x, y;
        canvas_page_new_position(p, FALSE, &x, &y);
        canvas_page_new_text_box(p, x, y, "");
        return TRUE;
    }
    /* Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y: undo / redo on the page; while typing,
     * the text box or the table cell undoes its own text */
    gboolean typing = f != NULL && (GTK_IS_EDITABLE(f) || GTK_IS_TEXT_VIEW(f));
    if (control && (state & GDK_ALT_MASK) == 0 && !typing &&
        (keyval == GDK_KEY_z || keyval == GDK_KEY_Z || keyval == GDK_KEY_y || keyval == GDK_KEY_Y)) {
        gboolean shift = (state & GDK_SHIFT_MASK) != 0;
        if (keyval == GDK_KEY_y || keyval == GDK_KEY_Y) {
            if (shift) return FALSE;
            shift = TRUE;
        }
        canvas_page_undo(p, shift);
        return TRUE;
    }
    gint zoom = canvas_keys_zoom_step(keyval, state);
    if (zoom != G_MININT) {
        canvas_viewport_zoom_step(p->viewport, zoom);
        return TRUE;
    }
    /* Space held on the page or a block: drag to move the view */
    if (keyval == GDK_KEY_space && (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK)) == 0 &&
        (f == p->surface || (it != NULL && f == it->frame))) {
        canvas_viewport_set_pan_key(p->viewport, TRUE);
        return TRUE;
    }
    /* Not typing (the page or a block focused): a letter picks a tool, Esc
     * goes back to Select */
    if ((it == NULL || f == it->frame) && (state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) == 0 &&
        (it != NULL || f == p->surface)) {
        if (keyval == GDK_KEY_Escape && p->tool != CANVAS_TOOL_SELECT) {
            canvas_page_set_tool(p, CANVAS_TOOL_SELECT);
            return TRUE;
        }
        gint tool = canvas_tool_for_key(keyval);
        if (tool >= 0) {
            canvas_page_set_tool(p, (CanvasTool)tool);
            return TRUE;
        }
    }
    if (it == NULL) return FALSE;
    if (f == it->frame) return block_key(p, it, keyval, state);
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

void canvas_keys_attach(CanvasPage* p, GtkWidget* root) {
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), p);
    g_signal_connect(keys, "key-released", G_CALLBACK(on_key_released), p);
    gtk_widget_add_controller(root, keys);
}
