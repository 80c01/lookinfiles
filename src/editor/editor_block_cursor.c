/* Emacs block cursor (Emacs and Org-mode tabs).
 *
 * GtkTextView only draws a block cursor in overwrite mode, which would
 * replace text while typing: the character under the cursor gets an
 * inverted-colour tag instead, and a box overlay stands in at line ends.
 * The box is placed from the view's layout, which keeps moving for a few
 * frames after a jump, a zoom or a resize: it is placed again on each of
 * those frames so it never stays behind. */
#include "editor/editor_modes_private.h"
#include "theme/text_tags.h"

#define BLOCK_CURSOR_TAG "emacs_block_cursor"
#define BLOCK_TAB_KEY "emacs-block-cursor-tab"
#define SETTLE_FRAMES 12

/* The tab drawn in view, or NULL once the tab is freed */
static EditorTab* tab_of_view(GtkWidget* view) {
    EditorTab* tab = g_object_get_data(G_OBJECT(view), BLOCK_TAB_KEY);
    return (tab != NULL && tab->mode_state != NULL) ? tab : NULL;
}

static void hide_eol_box(EditorModeState* st) {
    if (st->emacs_block_eol != NULL) gtk_widget_set_visible(st->emacs_block_eol, FALSE);
}

/* Place the line-end box at the cursor (hidden mid-line); TRUE if shown */
static gboolean place_eol_box(EditorTab* tab) {
    EditorModeState* st = tab->mode_state;
    GtkTextBuffer* b = tab->buffer;
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(b, &it, gtk_text_buffer_get_insert(b));
    if (!st->emacs_block_on || tab->view_passive ||
        (!gtk_text_iter_is_end(&it) && !gtk_text_iter_ends_line(&it))) {
        hide_eol_box(st);
        return FALSE;
    }
    GdkRectangle r;
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(tab->view), &it, &r);
    PangoLayout* layout = gtk_widget_create_pango_layout(tab->view, "M");
    gint cw = 8, ch = 16;
    pango_layout_get_pixel_size(layout, &cw, &ch);
    g_object_unref(layout);
    if (st->emacs_block_eol == NULL) {
        st->emacs_block_eol = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(st->emacs_block_eol, "emacs-block-cursor-eol");
        gtk_widget_set_can_target(st->emacs_block_eol, FALSE);
        gtk_text_view_add_overlay(GTK_TEXT_VIEW(tab->view), st->emacs_block_eol, r.x, r.y);
    } else {
        gtk_text_view_move_overlay(GTK_TEXT_VIEW(tab->view), st->emacs_block_eol, r.x, r.y);
    }
    gtk_widget_set_size_request(st->emacs_block_eol, MAX(cw, 2), MAX(r.height, ch));
    gtk_widget_set_visible(st->emacs_block_eol, TRUE);
    return TRUE;
}

static gboolean settle_tick(GtkWidget* view, GdkFrameClock* clock, gpointer data) {
    (void)clock;
    (void)data;
    EditorTab* tab = tab_of_view(view);
    if (tab == NULL) return G_SOURCE_REMOVE;
    EditorModeState* st = tab->mode_state;
    if (!place_eol_box(tab) || ++st->emacs_block_frames >= SETTLE_FRAMES) {
        st->emacs_block_tick = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* Follow the layout for the next frames */
static void settle(EditorTab* tab) {
    EditorModeState* st = tab->mode_state;
    st->emacs_block_frames = 0;
    if (st->emacs_block_tick == 0 && st->emacs_block_eol != NULL &&
        gtk_widget_get_visible(st->emacs_block_eol)) {
        st->emacs_block_tick = gtk_widget_add_tick_callback(tab->view, settle_tick, NULL, NULL);
    }
}

static void block_cursor_update(EditorTab* tab) {
    EditorModeState* st = tab->mode_state;
    if (st == NULL || tab->view == NULL) return;
    /* The tag is the buffer's: drawn by the view of a split file that has the cursor */
    if (tab->view_passive) {
        hide_eol_box(st);
        return;
    }
    GtkTextBuffer* b = tab->buffer;
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(b);
    GtkTextTag* tag = gtk_text_tag_table_lookup(table, BLOCK_CURSOR_TAG);
    GtkTextIter s, e;
    gtk_text_buffer_get_bounds(b, &s, &e);
    if (tag != NULL) gtk_text_buffer_remove_tag(b, tag, &s, &e);
    if (!place_eol_box(tab)) {
        if (!st->emacs_block_on) return;
        if (tag == NULL) {
            tag = text_tag_create(b, BLOCK_CURSOR_TAG, "background", "@editor_cursor",
                                  "foreground", "@bg_view", NULL);
        }
        gtk_text_tag_set_priority(tag, gtk_text_tag_table_get_size(table) - 1);
        GtkTextIter it, next;
        gtk_text_buffer_get_iter_at_mark(b, &it, gtk_text_buffer_get_insert(b));
        next = it;
        gtk_text_iter_forward_char(&next);
        gtk_text_buffer_apply_tag(b, tag, &it, &next);
        return;
    }
    settle(tab);
}

static void on_block_cursor_moved(GObject* obj, GParamSpec* pspec, gpointer data) {
    (void)obj;
    (void)pspec;
    block_cursor_update(data);
}

static void on_block_buffer_changed(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    block_cursor_update(data);
}

/* The view's height or its lines' heights changed (resize, zoom, wrap) */
static void on_block_vadj_changed(GtkAdjustment* adj, gpointer view) {
    (void)adj;
    EditorTab* tab = tab_of_view(view);
    if (tab != NULL && place_eol_box(tab)) settle(tab);
}

static void on_block_vadj_set(GObject* view, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    (void)data;
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
    if (adj != NULL) {
        g_signal_connect_object(adj, "changed", G_CALLBACK(on_block_vadj_changed), view, 0);
    }
}

void emacs_block_cursor_set(EditorTab* tab, gboolean on) {
    EditorModeState* st = tab->mode_state;
    if (st == NULL || tab->view == NULL) return;
    /* Once per view: the views of a split file share the buffer */
    if (g_object_get_data(G_OBJECT(tab->view), BLOCK_TAB_KEY) == NULL) {
        g_object_set_data(G_OBJECT(tab->view), BLOCK_TAB_KEY, tab);
        g_signal_connect(tab->buffer, "notify::cursor-position", G_CALLBACK(on_block_cursor_moved), tab);
        g_signal_connect_after(tab->buffer, "changed", G_CALLBACK(on_block_buffer_changed), tab);
        g_signal_connect(tab->view, "notify::vadjustment", G_CALLBACK(on_block_vadj_set), NULL);
        on_block_vadj_set(G_OBJECT(tab->view), NULL, NULL);
        st->emacs_block_view = tab->view;
        g_object_add_weak_pointer(G_OBJECT(tab->view), (gpointer*)&st->emacs_block_view);
    }
    st->emacs_block_on = on;
    if (on) gtk_widget_add_css_class(tab->view, "emacs-block-cursor");
    else gtk_widget_remove_css_class(tab->view, "emacs-block-cursor");
    block_cursor_update(tab);
}

void emacs_block_cursor_free(EditorTab* tab) {
    EditorModeState* st = tab->mode_state;
    if (st == NULL || st->emacs_block_view == NULL) return; /* the view is gone with its callbacks */
    GtkWidget* view = st->emacs_block_view;
    if (st->emacs_block_tick != 0) gtk_widget_remove_tick_callback(view, st->emacs_block_tick);
    st->emacs_block_tick = 0;
    g_object_set_data(G_OBJECT(view), BLOCK_TAB_KEY, NULL);
    g_object_remove_weak_pointer(G_OBJECT(view), (gpointer*)&st->emacs_block_view);
    st->emacs_block_view = NULL;
}
