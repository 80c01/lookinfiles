/* The Canvas page of the preview notebook (see canvas_view.h). The page is a
 * GtkFixed in a scrolled window; each block is a frame (move bar, content,
 * resize corner) placed at its position, the last child drawn on top. The
 * widgets are the state: a CanvasDoc is built from them to save. */
#include "canvas_view.h"
#include "canvas_doc.h"
#include "focus_route.h"
#include "fonts.h"
#include "icons.h"
#include "text_tags.h"
#include "theme.h"

#include <string.h>

#define PAGE_KEY "canvas-page"
#define ITEM_KEY "canvas-item"
#define SAVE_DELAY_MS 800   /* autosave after the last change */
#define PAGE_MARGIN 400     /* room left of the blocks, right and below */
#define NEW_OFFSET 24       /* new block: from the view corner, or below the selection */
#define NUDGE 8             /* arrows move a selected block by this */
#define NUDGE_BIG 40        /* Shift+arrows */
#define MIN_TEXT_WIDTH 80
#define MIN_IMAGE_SIZE 24
#define MAX_NEW_IMAGE_WIDTH 640

/* Tag names of the styles, in CanvasStyle bit order */
static const gchar* const STYLE_TAGS[] = {"b", "i", "u", "s", "code", "h"};

typedef struct CanvasPage CanvasPage;

typedef struct {
    CanvasPage* page;
    CanvasBlockKind kind;
    GtkWidget* frame;  /* child of the surface */
    GtkWidget* content; /* text view or picture */
    GtkTextBuffer* buffer;
    gchar* src;        /* picture, relative to the note directory */
    gint x, y, w, h;
    gdouble aspect;    /* picture width / height */
    guint pending;     /* styles toggled with no selection, for the next text typed */
    guint insert_styles; /* pending styles of the insertion running */
} CanvasItem;

struct CanvasPage {
    AppData* app;
    gchar* dir;        /* note directory, NULL when nothing is open */
    GtkWidget* root;   /* notebook page */
    GtkWidget* title;
    GtkWidget* scroll;
    GtkWidget* surface; /* GtkFixed */
    guint save_source;
    guint generation;  /* bumped on each note loaded: late pastes are dropped */
    gboolean dirty;
    gboolean loading;
    gboolean closed;
    /* Drag of a block: moved or resized from these */
    CanvasItem* drag_item;
    gboolean drag_resize;
    gint drag_x, drag_y, drag_w, drag_h;
    /* Pointer over the page (pictures pasted there) */
    gboolean pointer_in;
    gdouble pointer_x, pointer_y;
};

static void schedule_save(CanvasPage* p);
static gboolean save_now(CanvasPage* p);
static void remove_item(CanvasItem* it);
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

/* ------------------------------------------------------------------------- */
/* Items                                                                     */
/* ------------------------------------------------------------------------- */

static CanvasItem* item_of(GtkWidget* w) {
    for (; w != NULL; w = gtk_widget_get_parent(w)) {
        CanvasItem* it = g_object_get_data(G_OBJECT(w), ITEM_KEY);
        if (it != NULL) return it;
    }
    return NULL;
}

static void item_free(gpointer data) {
    CanvasItem* it = data;
    g_free(it->src);
    g_free(it);
}

static GtkWidget* focused_widget(CanvasPage* p) {
    GtkRoot* root = gtk_widget_get_root(p->root);
    return root != NULL ? gtk_root_get_focus(root) : NULL;
}

/* The block with the focus (selected or being edited), or NULL */
static CanvasItem* focused_item(CanvasPage* p) {
    GtkWidget* f = focused_widget(p);
    return (f != NULL && gtk_widget_is_ancestor(f, p->surface)) ? item_of(f) : NULL;
}

static void select_item(CanvasItem* it) {
    GtkRoot* root = gtk_widget_get_root(it->frame);
    if (root != NULL) gtk_root_set_focus(root, it->frame);
}

static void edit_item(CanvasItem* it) {
    if (it->kind != CANVAS_BLOCK_TEXT) {
        select_item(it);
        return;
    }
    gtk_widget_grab_focus(it->content);
}

static void deselect(CanvasPage* p) {
    GtkRoot* root = gtk_widget_get_root(p->surface);
    if (root != NULL) gtk_root_set_focus(root, p->surface);
}

static void place_item(CanvasItem* it) {
    it->x = MAX(it->x, 0);
    it->y = MAX(it->y, 0);
    gtk_fixed_move(GTK_FIXED(it->page->surface), it->frame, it->x, it->y);
    if (it->kind == CANVAS_BLOCK_TEXT) {
        gtk_widget_set_size_request(it->frame, it->w, -1);
    } else {
        gtk_widget_set_size_request(it->content, it->w, it->h);
    }
}

/* Draw the block on top of the others */
static void raise_item(CanvasItem* it) {
    GtkWidget* surface = it->page->surface;
    if (gtk_widget_get_last_child(surface) != it->frame) {
        gtk_widget_insert_before(it->frame, surface, NULL);
    }
}

/* Size of the page: the blocks and some room around them */
static void update_extent(CanvasPage* p) {
    gint w = 0, h = 0;
    for (GtkWidget* c = gtk_widget_get_first_child(p->surface); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        CanvasItem* it = g_object_get_data(G_OBJECT(c), ITEM_KEY);
        if (it == NULL) continue;
        w = MAX(w, it->x + MAX(it->w, gtk_widget_get_width(c)));
        h = MAX(h, it->y + MAX(it->h, gtk_widget_get_height(c)));
    }
    gtk_widget_set_size_request(p->surface, w + PAGE_MARGIN, h + PAGE_MARGIN);
}

/* ---- Text styles ---- */

static GtkTextTag* style_tag(GtkTextBuffer* buffer, guint k) {
    switch (k) {
    case 0: return text_tag_ensure(buffer, STYLE_TAGS[k], "weight", PANGO_WEIGHT_BOLD, NULL);
    case 1: return text_tag_ensure(buffer, STYLE_TAGS[k], "style", PANGO_STYLE_ITALIC, NULL);
    case 2: return text_tag_ensure(buffer, STYLE_TAGS[k], "underline", PANGO_UNDERLINE_SINGLE, NULL);
    case 3: return text_tag_ensure(buffer, STYLE_TAGS[k], "strikethrough", TRUE, NULL);
    case 4:
        return text_tag_ensure(buffer, STYLE_TAGS[k], "family", "@font_code", "background", "@bg_code_inline",
                               "scale", FONT_SCALE_CODE, NULL);
    default:
        return text_tag_ensure(buffer, STYLE_TAGS[k], "weight", PANGO_WEIGHT_BOLD, "scale", FONT_SCALE_H2, NULL);
    }
}

static guint styles_at(GtkTextBuffer* buffer, const GtkTextIter* iter) {
    guint styles = 0;
    for (guint k = 0; k < G_N_ELEMENTS(STYLE_TAGS); k++) {
        if (gtk_text_iter_has_tag(iter, style_tag(buffer, k))) styles |= 1u << k;
    }
    return styles;
}

static void apply_styles(GtkTextBuffer* buffer, gint start, gint end, guint styles) {
    GtkTextIter s, e;
    for (guint k = 0; k < G_N_ELEMENTS(STYLE_TAGS); k++) {
        if ((styles & (1u << k)) == 0) continue;
        gtk_text_buffer_get_iter_at_offset(buffer, &s, start);
        gtk_text_buffer_get_iter_at_offset(buffer, &e, end);
        gtk_text_buffer_apply_tag(buffer, style_tag(buffer, k), &s, &e);
    }
}

/* Style on the selection (removed if all of it has it), or for the text
 * typed next when nothing is selected */
static void toggle_style(CanvasItem* it, guint style) {
    guint k = (guint)g_bit_nth_lsf(style, -1);
    GtkTextIter s, e;
    if (!gtk_text_buffer_get_selection_bounds(it->buffer, &s, &e)) {
        it->pending ^= style;
        return;
    }
    GtkTextTag* tag = style_tag(it->buffer, k);
    gboolean all = TRUE;
    for (GtkTextIter i = s; gtk_text_iter_compare(&i, &e) < 0; gtk_text_iter_forward_char(&i)) {
        if (!gtk_text_iter_has_tag(&i, tag)) {
            all = FALSE;
            break;
        }
    }
    if (all) {
        gtk_text_buffer_remove_tag(it->buffer, tag, &s, &e);
    } else {
        gtk_text_buffer_apply_tag(it->buffer, tag, &s, &e);
    }
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

/* Typed text takes the styles of the character before it (not across a line
 * break), changed by the styles toggled with no selection */
static void on_insert_after(GtkTextBuffer* buffer, GtkTextIter* location, gchar* text, gint len, gpointer data) {
    CanvasItem* it = data;
    if (it->page->loading) return;
    gint end = gtk_text_iter_get_offset(location);
    gint start = end - (gint)g_utf8_strlen(text, len);
    guint styles = 0;
    if (start > 0) {
        GtkTextIter prev;
        gtk_text_buffer_get_iter_at_offset(buffer, &prev, start - 1);
        if (gtk_text_iter_get_char(&prev) != '\n') styles = styles_at(buffer, &prev);
    }
    styles ^= it->insert_styles;
    it->insert_styles = 0;
    if (styles != 0) {
        /* Inserted text has no tags: only add the missing ones */
        apply_styles(buffer, start, end, styles);
    }
    gtk_text_buffer_get_iter_at_offset(buffer, location, end);
}

static void on_mark_set(GtkTextBuffer* buffer, const GtkTextIter* location, GtkTextMark* mark, gpointer data) {
    (void)location;
    CanvasItem* it = data;
    if (mark == gtk_text_buffer_get_insert(buffer)) it->pending = 0;
}

/* A text box left empty goes away (unless the focus moved to its frame) */
typedef struct {
    GtkWidget* root;  /* page widget (ref: the page state stays alive) */
    GtkWidget* frame; /* block (ref) */
} EmptyCheck;

static gboolean drop_if_empty(gpointer data) {
    EmptyCheck* check = data;
    GtkWidget* frame = check->frame;
    CanvasItem* it = g_object_get_data(G_OBJECT(frame), ITEM_KEY);
    if (it != NULL && !it->page->closed && gtk_widget_get_parent(frame) == it->page->surface &&
        gtk_text_buffer_get_char_count(it->buffer) == 0) {
        GtkWidget* f = focused_widget(it->page);
        if (f == NULL || (f != frame && !gtk_widget_is_ancestor(f, frame))) remove_item(it);
    }
    g_object_unref(frame);
    g_object_unref(check->root);
    g_free(check);
    return G_SOURCE_REMOVE;
}

static void on_text_focus_leave(GtkEventControllerFocus* ctrl, gpointer data) {
    (void)ctrl;
    CanvasItem* it = data;
    EmptyCheck* check = g_new0(EmptyCheck, 1);
    check->root = g_object_ref(it->page->root);
    check->frame = g_object_ref(it->frame);
    g_idle_add(drop_if_empty, check);
}

static void on_buffer_changed(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    schedule_save(((CanvasItem*)data)->page);
}

static void on_tag_changed(GtkTextBuffer* buffer, GtkTextTag* tag, const GtkTextIter* s, const GtkTextIter* e,
                           gpointer data) {
    (void)buffer;
    (void)tag;
    (void)s;
    (void)e;
    schedule_save(((CanvasItem*)data)->page);
}

/* ---- Blocks ---- */

static CanvasItem* item_new(CanvasPage* p, CanvasBlockKind kind, GtkWidget* content) {
    CanvasItem* it = g_new0(CanvasItem, 1);
    it->page = p;
    it->kind = kind;
    it->content = content;

    GtkWidget* frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(frame, "canvas-block");
    gtk_widget_set_focusable(frame, TRUE);
    GtkWidget* grip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(grip, "canvas-grip");
    gtk_widget_set_cursor_from_name(grip, "move");
    gtk_widget_set_tooltip_text(grip, "Drag to Move (Esc, then Arrows)");
    g_object_set_data(G_OBJECT(grip), "canvas-grip", GINT_TO_POINTER(1));
    gtk_box_append(GTK_BOX(frame), grip);

    GtkWidget* overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(overlay), content);
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
    gtk_fixed_put(GTK_FIXED(p->surface), frame, 0, 0);
    return it;
}

static CanvasItem* add_text_item(CanvasPage* p, const CanvasBlock* b) {
    GtkWidget* view = gtk_text_view_new();
    gtk_widget_add_css_class(view, "canvas-text");
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(view), FALSE);
    CanvasItem* it = item_new(p, CANVAS_BLOCK_TEXT, view);
    it->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    it->x = b->x;
    it->y = b->y;
    it->w = MAX(b->w, MIN_TEXT_WIDTH);
    for (guint k = 0; k < G_N_ELEMENTS(STYLE_TAGS); k++) style_tag(it->buffer, k);

    gboolean was_loading = p->loading;
    p->loading = TRUE;
    gchar* valid = g_utf8_make_valid(b->text != NULL ? b->text : "", -1);
    gtk_text_buffer_set_text(it->buffer, valid, -1);
    g_free(valid);
    for (guint i = 0; b->runs != NULL && i < b->runs->len; i++) {
        const CanvasRun* r = &g_array_index(b->runs, CanvasRun, i);
        apply_styles(it->buffer, r->start, r->end, r->styles);
    }
    p->loading = was_loading;

    g_signal_connect(it->buffer, "insert-text", G_CALLBACK(on_insert_before), it);
    g_signal_connect_after(it->buffer, "insert-text", G_CALLBACK(on_insert_after), it);
    g_signal_connect(it->buffer, "mark-set", G_CALLBACK(on_mark_set), it);
    g_signal_connect(it->buffer, "changed", G_CALLBACK(on_buffer_changed), it);
    g_signal_connect_after(it->buffer, "apply-tag", G_CALLBACK(on_tag_changed), it);
    g_signal_connect_after(it->buffer, "remove-tag", G_CALLBACK(on_tag_changed), it);
    GtkEventController* focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "leave", G_CALLBACK(on_text_focus_leave), it);
    gtk_widget_add_controller(view, focus);
    place_item(it);
    return it;
}

static CanvasItem* add_image_item(CanvasPage* p, const CanvasBlock* b) {
    gchar* full = g_build_filename(p->dir, b->src, NULL);
    GtkWidget* picture = gtk_picture_new_for_filename(full);
    g_free(full);
    gtk_picture_set_can_shrink(GTK_PICTURE(picture), TRUE);
    gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_FILL);
    gtk_widget_set_cursor_from_name(picture, "move");
    CanvasItem* it = item_new(p, CANVAS_BLOCK_IMAGE, picture);
    it->src = g_strdup(b->src);
    it->x = b->x;
    it->y = b->y;
    it->w = MAX(b->w, MIN_IMAGE_SIZE);
    it->h = MAX(b->h, MIN_IMAGE_SIZE);
    it->aspect = (gdouble)it->w / it->h;
    place_item(it);
    return it;
}

static CanvasItem* add_item(CanvasPage* p, const CanvasBlock* b) {
    return b->kind == CANVAS_BLOCK_TEXT ? add_text_item(p, b) : add_image_item(p, b);
}

static void remove_item(CanvasItem* it) {
    CanvasPage* p = it->page;
    gtk_fixed_remove(GTK_FIXED(p->surface), it->frame); /* frees it */
    schedule_save(p);
}

/* Where a new block goes: at the pointer, below the selected block, or near
 * the top-left corner of the visible part of the page */
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
    CanvasItem* it = add_text_item(p, canvas_doc_add_text(tmp, x, y, 0, text));
    update_extent(p);
    schedule_save(p);
    edit_item(it);
    return it;
}

/* A picture file already in the note directory (src relative to it) */
static CanvasItem* new_picture(CanvasPage* p, const gchar* src, gint x, gint y, gint width, gint height) {
    if (width <= 0 || height <= 0) return NULL;
    if (width > MAX_NEW_IMAGE_WIDTH) {
        height = (gint)((gdouble)height * MAX_NEW_IMAGE_WIDTH / width);
        width = MAX_NEW_IMAGE_WIDTH;
    }
    g_autoptr(CanvasDoc) tmp = canvas_doc_new();
    CanvasBlock* b = canvas_doc_add_image(tmp, x, y, width, height, src);
    CanvasItem* it = add_image_item(p, b);
    update_extent(p);
    schedule_save(p);
    select_item(it);
    return it;
}

/* ------------------------------------------------------------------------- */
/* Load and save                                                             */
/* ------------------------------------------------------------------------- */

static CanvasDoc* doc_from_widgets(CanvasPage* p) {
    CanvasDoc* doc = canvas_doc_new();
    for (GtkWidget* c = gtk_widget_get_first_child(p->surface); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        CanvasItem* it = g_object_get_data(G_OBJECT(c), ITEM_KEY);
        if (it == NULL) continue;
        if (it->kind == CANVAS_BLOCK_IMAGE) {
            canvas_doc_add_image(doc, it->x, it->y, it->w, it->h, it->src);
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
                                 styles_at(it->buffer, &i));
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
    /* Never from a page being destroyed: its blocks may be gone already */
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
/* Pictures: clipboard and drops                                             */
/* ------------------------------------------------------------------------- */

typedef struct {
    GtkWidget* root;  /* page widget (ref) */
    guint generation; /* note shown when the paste started */
    gint x, y;
} PasteJob;

/* The page of a job, if it still shows the same note */
static CanvasPage* job_page(PasteJob* job) {
    CanvasPage* p = g_object_get_data(G_OBJECT(job->root), PAGE_KEY);
    return (p != NULL && !p->closed && p->dir != NULL && p->generation == job->generation) ? p : NULL;
}

static void job_free(PasteJob* job) {
    g_object_unref(job->root);
    g_free(job);
}

static PasteJob* job_new(CanvasPage* p, gint x, gint y) {
    PasteJob* job = g_new0(PasteJob, 1);
    job->root = g_object_ref(p->root);
    job->generation = p->generation;
    job->x = x;
    job->y = y;
    return job;
}

static void on_texture_read(GObject* source, GAsyncResult* result, gpointer data) {
    PasteJob* job = data;
    g_autoptr(GError) error = NULL;
    GdkTexture* texture = gdk_clipboard_read_texture_finish(GDK_CLIPBOARD(source), result, &error);
    CanvasPage* p = job_page(job);
    if (texture != NULL && p != NULL) {
        gchar* src = canvas_doc_new_media_path(p->dir, "png");
        gchar* full = g_build_filename(p->dir, src, NULL);
        gchar* media = g_path_get_dirname(full);
        GBytes* png = gdk_texture_save_to_png_bytes(texture);
        if (g_mkdir_with_parents(media, 0755) == 0 &&
            g_file_set_contents(full, g_bytes_get_data(png, NULL), (gssize)g_bytes_get_size(png), &error)) {
            new_picture(p, src, job->x, job->y, gdk_texture_get_width(texture), gdk_texture_get_height(texture));
        } else {
            g_warning("canvas: cannot write %s: %s", full, error != NULL ? error->message : "");
        }
        g_bytes_unref(png);
        g_free(media);
        g_free(full);
        g_free(src);
    }
    if (texture != NULL) g_object_unref(texture);
    job_free(job);
}

static void on_text_read(GObject* source, GAsyncResult* result, gpointer data) {
    PasteJob* job = data;
    gchar* text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), result, NULL);
    CanvasPage* p = job_page(job);
    if (text != NULL && p != NULL && text[0] != '\0') {
        new_text_box(p, job->x, job->y, text);
    }
    g_free(text);
    job_free(job);
}

/* Ctrl+V: a picture becomes a new block; text pasted outside a text box
 * becomes a new text box. FALSE: let the text box paste it. */
static gboolean paste(CanvasPage* p) {
    if (p->dir == NULL) return FALSE;
    GdkClipboard* clipboard = gtk_widget_get_clipboard(p->surface);
    GdkContentFormats* formats = gdk_clipboard_get_formats(clipboard);
    gint x, y;
    if (gdk_content_formats_contain_gtype(formats, GDK_TYPE_TEXTURE)) {
        new_block_position(p, TRUE, &x, &y);
        gdk_clipboard_read_texture_async(clipboard, NULL, on_texture_read, job_new(p, x, y));
        return TRUE;
    }
    GtkWidget* f = focused_widget(p);
    if (f != NULL && GTK_IS_TEXT_VIEW(f)) return FALSE;
    if (gdk_content_formats_contain_gtype(formats, G_TYPE_STRING)) {
        new_block_position(p, TRUE, &x, &y);
        gdk_clipboard_read_text_async(clipboard, NULL, on_text_read, job_new(p, x, y));
        return TRUE;
    }
    return FALSE;
}

/* Copy an image file into media/ and add it; FALSE if it is not an image */
static gboolean add_picture_file(CanvasPage* p, GFile* file, gint x, gint y) {
    GdkTexture* texture = gdk_texture_new_from_file(file, NULL);
    if (texture == NULL) return FALSE;
    gchar* base = g_file_get_basename(file);
    const gchar* dot = base != NULL ? strrchr(base, '.') : NULL;
    gchar* src = canvas_doc_new_media_path(p->dir, dot != NULL ? dot + 1 : "png");
    gchar* full = g_build_filename(p->dir, src, NULL);
    gchar* media = g_path_get_dirname(full);
    GFile* dest = g_file_new_for_path(full);
    g_autoptr(GError) error = NULL;
    gboolean ok = g_mkdir_with_parents(media, 0755) == 0 &&
                  g_file_copy(file, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &error);
    if (ok) {
        new_picture(p, src, x, y, gdk_texture_get_width(texture), gdk_texture_get_height(texture));
    } else {
        g_warning("canvas: cannot copy the picture: %s", error != NULL ? error->message : "");
    }
    g_object_unref(dest);
    g_free(media);
    g_free(full);
    g_free(src);
    g_free(base);
    g_object_unref(texture);
    return ok;
}

static gboolean on_drop(GtkDropTarget* target, const GValue* value, gdouble x, gdouble y, gpointer data) {
    (void)target;
    CanvasPage* p = data;
    if (p->dir == NULL || !G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST)) return FALSE;
    gboolean any = FALSE;
    gint offset = 0;
    GSList* files = gdk_file_list_get_files(g_value_get_boxed(value));
    for (GSList* l = files; l != NULL; l = l->next) {
        if (add_picture_file(p, G_FILE(l->data), (gint)x + offset, (gint)y + offset)) {
            any = TRUE;
            offset += NEW_OFFSET;
        }
    }
    g_slist_free(files);
    return any;
}

/* ------------------------------------------------------------------------- */
/* Mouse                                                                     */
/* ------------------------------------------------------------------------- */

static void on_drag_begin(GtkGestureDrag* gesture, gdouble x, gdouble y, gpointer data) {
    CanvasPage* p = data;
    GtkWidget* hit = gtk_widget_pick(p->surface, x, y, GTK_PICK_DEFAULT);
    CanvasItem* it = item_of(hit);
    gboolean grip = FALSE, corner = FALSE;
    for (GtkWidget* w = hit; w != NULL && it != NULL && w != it->frame; w = gtk_widget_get_parent(w)) {
        if (g_object_get_data(G_OBJECT(w), "canvas-grip") != NULL) grip = TRUE;
        if (g_object_get_data(G_OBJECT(w), "canvas-corner") != NULL) corner = TRUE;
    }
    /* A picture moves from anywhere; text only from its bar (clicks edit it) */
    gboolean move = grip || (it != NULL && it->kind == CANVAS_BLOCK_IMAGE && !corner);
    if (it == NULL || (!move && !corner)) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    p->drag_item = it;
    p->drag_resize = corner;
    p->drag_x = it->x;
    p->drag_y = it->y;
    p->drag_w = it->kind == CANVAS_BLOCK_TEXT ? MAX(it->w, gtk_widget_get_width(it->frame)) : it->w;
    p->drag_h = it->h;
    raise_item(it);
    select_item(it);
}

static void on_drag_update(GtkGestureDrag* gesture, gdouble dx, gdouble dy, gpointer data) {
    (void)gesture;
    CanvasPage* p = data;
    CanvasItem* it = p->drag_item;
    if (it == NULL) return;
    if (!p->drag_resize) {
        it->x = p->drag_x + (gint)dx;
        it->y = p->drag_y + (gint)dy;
    } else if (it->kind == CANVAS_BLOCK_TEXT) {
        it->w = MAX(p->drag_w + (gint)dx, MIN_TEXT_WIDTH);
    } else {
        /* Pictures keep their proportions */
        it->w = MAX(p->drag_w + (gint)dx, MIN_IMAGE_SIZE);
        it->h = MAX((gint)(it->w / it->aspect), MIN_IMAGE_SIZE);
    }
    place_item(it);
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

/* Click on the empty page: deselect; double click: new text box there */
static void on_page_pressed(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    CanvasPage* p = data;
    GtkWidget* hit = gtk_widget_pick(p->surface, x, y, GTK_PICK_DEFAULT);
    if (item_of(hit) != NULL || p->dir == NULL) return;
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

/* ------------------------------------------------------------------------- */
/* Keys                                                                      */
/* ------------------------------------------------------------------------- */

/* Next (step 1) / previous (-1) block in drawing order, wrapping around */
static void focus_next_item(CanvasPage* p, CanvasItem* from, gint step) {
    GtkWidget* start = from != NULL ? from->frame : NULL;
    GtkWidget* w = start;
    do {
        if (w == NULL) {
            w = step > 0 ? gtk_widget_get_first_child(p->surface) : gtk_widget_get_last_child(p->surface);
        } else {
            w = step > 0 ? gtk_widget_get_next_sibling(w) : gtk_widget_get_prev_sibling(w);
        }
        if (w != NULL && g_object_get_data(G_OBJECT(w), ITEM_KEY) != NULL) {
            select_item(g_object_get_data(G_OBJECT(w), ITEM_KEY));
            return;
        }
    } while (w != start && !(w == NULL && start == NULL));
}

/* Keys of the text box being edited */
static gboolean text_key(CanvasItem* it, guint keyval, GdkModifierType state) {
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    gboolean shift = (state & GDK_SHIFT_MASK) != 0;
    gboolean alt = (state & GDK_ALT_MASK) != 0;
    if (keyval == GDK_KEY_Escape && !ctrl && !alt) {
        select_item(it);
        return TRUE;
    }
    if ((keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab) && !ctrl && !alt) {
        focus_next_item(it->page, it, keyval == GDK_KEY_Tab ? 1 : -1);
        return TRUE;
    }
    if (!ctrl) return FALSE;
    guint style = 0;
    switch (keyval) {
    case GDK_KEY_b: case GDK_KEY_B: style = (shift || alt) ? 0 : CANVAS_STYLE_BOLD; break;
    case GDK_KEY_i: case GDK_KEY_I: style = (shift || alt) ? 0 : CANVAS_STYLE_ITALIC; break;
    case GDK_KEY_u: case GDK_KEY_U: style = (shift || alt) ? 0 : CANVAS_STYLE_UNDERLINE; break;
    case GDK_KEY_s: case GDK_KEY_S: style = (shift && !alt) ? CANVAS_STYLE_STRIKE : 0; break;
    case GDK_KEY_e: case GDK_KEY_E: style = (shift || alt) ? 0 : CANVAS_STYLE_CODE; break;
    case GDK_KEY_1: case GDK_KEY_KP_1: style = (alt && !shift) ? CANVAS_STYLE_HEADING : 0; break;
    default: break;
    }
    if (style == 0) return FALSE;
    toggle_style(it, style);
    return TRUE;
}

/* Keys of a selected block */
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
        edit_item(it);
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
    place_item(it);
    update_extent(p);
    schedule_save(p);
    return TRUE;
}

static gboolean on_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType state, gpointer data) {
    (void)ctrl;
    (void)keycode;
    CanvasPage* p = data;
    if (p->dir == NULL) return FALSE;
    gboolean control = (state & GDK_CONTROL_MASK) != 0;
    gboolean other = (state & (GDK_SHIFT_MASK | GDK_ALT_MASK)) != 0;
    if (control && !other && (keyval == GDK_KEY_v || keyval == GDK_KEY_V)) {
        return paste(p);
    }
    if (control && !other && (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter)) {
        gint x, y;
        new_block_position(p, FALSE, &x, &y);
        new_text_box(p, x, y, "");
        return TRUE;
    }
    GtkWidget* f = focused_widget(p);
    CanvasItem* it = focused_item(p);
    if (it != NULL && f == it->frame) return block_key(p, it, keyval, state);
    if (it != NULL && f == it->content && it->kind == CANVAS_BLOCK_TEXT) return text_key(it, keyval, state);
    if (f == p->surface && !control && !other && keyval == GDK_KEY_Tab) {
        focus_next_item(p, NULL, 1);
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------------- */
/* Page                                                                      */
/* ------------------------------------------------------------------------- */

static void close_page(CanvasPage* p) {
    AppData* app = p->app;
    save_now(p);
    p->closed = TRUE;
    if (app->canvas_page == NULL || app->notebook == NULL) return;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    GtkWidget* closed = app->canvas_page;
    GtkWidget* focus_back = focus_route_return_target(app, closed);
    g_object_ref(closed);
    gint page = gtk_notebook_page_num(nb, closed);
    app->canvas_page = NULL;
    if (page >= 0) gtk_notebook_remove_page(nb, page);
    focus_route_return(app, focus_back, closed);
    g_object_unref(closed);
}

static void on_close_clicked(GtkButton* button, gpointer data) {
    (void)button;
    close_page(data);
}

static void on_add_clicked(GtkButton* button, gpointer data) {
    (void)button;
    CanvasPage* p = data;
    gint x, y;
    new_block_position(p, FALSE, &x, &y);
    new_text_box(p, x, y, "");
}

static void on_label_middle_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    close_page(data);
}

/* The page goes away (closed, or the window destroyed): saved before, in
 * close_page() or canvas_view_save() at quit */
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
    GtkWidget* hint = gtk_label_new("Double click: text box · Ctrl+V: paste a picture · saved automatically");
    gtk_widget_add_css_class(hint, "canvas-hint");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(hint), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(hint, TRUE);
    gtk_box_append(GTK_BOX(bar), hint);
    GtkWidget* add = icon_button_new("list-add", "Add Text Box (Ctrl+Enter / Double Click)");
    g_signal_connect(add, "clicked", G_CALLBACK(on_add_clicked), p);
    gtk_box_append(GTK_BOX(bar), add);
    GtkWidget* close = icon_button_new("window-close", "Close Canvas (Ctrl+W)");
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), p);
    gtk_box_append(GTK_BOX(bar), close);
    gtk_box_append(GTK_BOX(root), bar);

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

    /* After the Git tab and the result pages, before the editor tabs */
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

/* ------------------------------------------------------------------------- */
/* API                                                                       */
/* ------------------------------------------------------------------------- */

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
    if (first != NULL && g_object_get_data(G_OBJECT(first), ITEM_KEY) != NULL) {
        select_item(g_object_get_data(G_OBJECT(first), ITEM_KEY));
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

gboolean canvas_view_close_shown(AppData* app) {
    CanvasPage* p = app != NULL ? page_of(app) : NULL;
    if (p == NULL || app->notebook == NULL || !gtk_widget_get_mapped(p->root)) return FALSE;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    if (gtk_notebook_get_current_page(nb) != gtk_notebook_page_num(nb, p->root)) return FALSE;
    close_page(p);
    return TRUE;
}
