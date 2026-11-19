/* Preview kept on the line being edited (see preview_sync.h). */
#include "preview/preview_sync.h"
#include "preview/preview_folding.h"
#include "preview/preview_state.h"

#define SYNC_MARK "preview-sync"
/* Frames the view is kept on the line while the new preview is laid out */
#define SETTLE_FRAMES 60
/* Height in the view of a line asked for (preview_sync_goto_line()) */
#define GOTO_ALIGN 0.3

struct PreviewSync {
    gchar* path;            /* file in the preview */
    guint page;             /* its page (mailbox messages) */
    GArray* sources;        /* MdSource of a rendered document, or NULL */
    gint first_line;        /* text shown line by line from this buffer line, or -1 */
    gboolean keep;          /* the view goes back to a line once shown */
    gboolean restore;       /* the place below is kept for the next preview of path */
    gint restore_line;      /* source line at the top of the view, or -1 */
    gint restore_offset;    /* else the offset at the top */
    gint restore_delta;     /* pixels from the top of that line to the top of the view */
    guint idle_id;          /* scroll once the new preview is in the buffer */
    guint shown_tick_id;    /* or at the next frame, if it comes first */
    guint settle_id;        /* tick callback keeping the view on the line */
    GdkFrameClock* settle_clock; /* and its "layout" handler, placing it before paint */
    gulong settle_layout;
    gdouble settle_align;   /* height of the line in the view */
    gint settle_delta;      /* or pixels of it above the top of the view (-1: align) */
    gdouble settle_upper;   /* document height at the last frame */
    gint settle_frames;     /* frames left, and frames with the height unchanged */
    gint settle_stable;
    gint synced_offset;     /* offset last scrolled to for the editor's cursor */
    GtkTextBuffer* tracked; /* editor buffer of path whose cursor is followed */
    GtkWindow* window;      /* window whose focus is watched (an editor of path */
    gulong focus_handler;   /* may have opened), NULL once destroyed */
    gchar* goto_path;       /* the next load of this file opens on goto_line */
    gint goto_line;         /* source line, -1: none */
    gboolean goto_load;     /* the load under way opens on goto_line */
    gboolean goto_scroll;   /* scroll there once the preview is in the buffer */
};

static struct PreviewSync* sync_get(AppData* app) {
    if (app->preview_sync == NULL) {
        app->preview_sync = g_new0(struct PreviewSync, 1);
        app->preview_sync->first_line = -1;
        app->preview_sync->goto_line = -1;
        app->preview_sync->synced_offset = -1;
    }
    return app->preview_sync;
}

/* Offset in the preview of source line line, -1 if the preview has no lines */
static gint line_offset(AppData* app, struct PreviewSync* s, gint line) {
    if (s->sources != NULL) {
        return md_source_offset(s->sources, line);
    }
    if (s->first_line < 0) {
        return -1;
    }
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_line(app->buffer, &iter, s->first_line + line);
    return gtk_text_iter_get_offset(&iter);
}

/* Source line at offset of the preview, -1 if the preview has no lines */
static gint offset_line(AppData* app, struct PreviewSync* s, gint offset) {
    if (s->sources != NULL) {
        return md_source_line(s->sources, offset);
    }
    if (s->first_line < 0) {
        return -1;
    }
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_offset(app->buffer, &iter, offset);
    return MAX(gtk_text_iter_get_line(&iter) - s->first_line, 0);
}

/* Scroll the view to the mark at once, with the heights of the lines known
 * now: at height align, or the top of its line delta pixels above the top of
 * the view when delta >= 0 */
static void place_view(GtkTextView* view, GtkTextMark* mark, gdouble align, gint delta) {
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
    if (vadj == NULL) {
        return;
    }
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_mark(gtk_text_view_get_buffer(view), &iter, mark);
    GdkRectangle visible, line;
    gtk_text_view_get_visible_rect(view, &visible);
    gdouble top;
    if (delta >= 0) {
        gint y = 0, height = 0;
        gtk_text_view_get_line_yrange(view, &iter, &y, &height);
        top = y + delta;
    } else {
        /* As gtk_text_view_scroll_to_mark() */
        gtk_text_view_get_iter_location(view, &iter, &line);
        top = line.y - align * (visible.height - line.height);
    }
    gdouble lower = gtk_adjustment_get_lower(vadj);
    gdouble max = MAX(gtk_adjustment_get_upper(vadj) - gtk_adjustment_get_page_size(vadj), lower);
    gdouble value = CLAMP(gtk_adjustment_get_value(vadj) + (top - visible.y), lower, max);
    if (ABS(value - gtk_adjustment_get_value(vadj)) >= 1.0) {
        gtk_adjustment_set_value(vadj, value);
    }
}

/* Scroll the view to the mark (see place_view()). Not with
 * gtk_text_view_scroll_to_mark() for a place kept: done later, once the
 * lines are laid out, it would put the text of the line at the top, not
 * its margin above. */
static void scroll_to_sync_mark(GtkTextView* view, GtkTextMark* mark, gdouble align, gint delta) {
    if (delta >= 0) {
        place_view(view, mark, align, delta);
    } else {
        gtk_text_view_scroll_to_mark(view, mark, 0.0, TRUE, 0.0, align);
    }
}

/* Show offset of the preview at height align of the view (0: top, 1: bottom),
 * or delta pixels of its line above the top of the view when delta >= 0 */
static void scroll_to(AppData* app, gint offset, gdouble align, gint delta) {
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_offset(app->buffer, &iter, offset);
    GtkTextMark* mark = gtk_text_buffer_get_mark(app->buffer, SYNC_MARK);
    if (mark == NULL) {
        mark = gtk_text_buffer_create_mark(app->buffer, SYNC_MARK, &iter, TRUE);
    } else {
        gtk_text_buffer_move_mark(app->buffer, mark, &iter);
    }
    scroll_to_sync_mark(GTK_TEXT_VIEW(app->text_view), mark, align, delta);
}

static void settle_stop(AppData* app, struct PreviewSync* s);

/* The lines of a new preview get their real height over a few frames (the
 * view estimates the lines not laid out yet): scroll to the line again at
 * each frame until the height of the document stops changing */
static gboolean settle_tick(GtkWidget* widget, GdkFrameClock* clock, gpointer data) {
    (void)clock;
    AppData* app = data;
    struct PreviewSync* s = app->preview_sync;
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(widget));
    gdouble upper = vadj != NULL ? gtk_adjustment_get_upper(vadj) : 0.0;
    s->settle_stable = (upper == s->settle_upper) ? s->settle_stable + 1 : 0;
    s->settle_upper = upper;
    GtkTextMark* mark = gtk_text_buffer_get_mark(app->buffer, SYNC_MARK);
    if (mark != NULL) {
        scroll_to_sync_mark(GTK_TEXT_VIEW(widget), mark, s->settle_align, s->settle_delta);
    }
    if (mark == NULL || s->settle_stable >= 3 || --s->settle_frames <= 0) {
        s->settle_id = 0; /* removed by returning */
        settle_stop(app, s);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* After the view laid out the lines on screen, before the frame is painted:
 * the scroll of the tick was made with heights still estimated (the view
 * would show another part of the document for a frame) */
static void on_settle_layout(GdkFrameClock* clock, gpointer data) {
    (void)clock;
    AppData* app = data;
    struct PreviewSync* s = app->preview_sync;
    GtkTextMark* mark = gtk_text_buffer_get_mark(app->buffer, SYNC_MARK);
    if (mark != NULL) {
        place_view(GTK_TEXT_VIEW(app->text_view), mark, s->settle_align, s->settle_delta);
    }
}

static void settle_stop(AppData* app, struct PreviewSync* s) {
    if (s->settle_id != 0) {
        gtk_widget_remove_tick_callback(app->text_view, s->settle_id);
        s->settle_id = 0;
    }
    if (s->settle_clock != NULL) {
        g_signal_handler_disconnect(s->settle_clock, s->settle_layout);
        g_clear_object(&s->settle_clock);
    }
}

/* scroll_to() on a preview just put in the buffer */
static void scroll_settled(AppData* app, struct PreviewSync* s, gint offset, gdouble align, gint delta) {
    scroll_to(app, offset, align, delta);
    s->settle_align = align;
    s->settle_delta = delta;
    s->settle_upper = -1.0;
    s->settle_stable = 0;
    s->settle_frames = SETTLE_FRAMES;
    if (s->settle_id == 0) {
        s->settle_id = gtk_widget_add_tick_callback(app->text_view, settle_tick, app, NULL);
    }
    GdkFrameClock* clock = gtk_widget_get_frame_clock(app->text_view);
    if (s->settle_clock == NULL && clock != NULL) {
        s->settle_clock = g_object_ref(clock);
        s->settle_layout = g_signal_connect_after(clock, "layout", G_CALLBACK(on_settle_layout), app);
    }
}

/* The editor tab of buffer that has its cursor (a split view shares it) */
static EditorTab* cursor_tab(AppData* app, GtkTextBuffer* buffer) {
    EditorTab* found = NULL;
    for (GList* l = app->editors; buffer != NULL && l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t != NULL && t->buffer == buffer && t->view != NULL) {
            if (!t->view_passive) {
                return t;
            }
            found = (found != NULL) ? found : t;
        }
    }
    return found;
}

/* Bring the line of the editor's cursor into the preview, at the height of
 * the cursor in the editor; FALSE if the preview has no lines */
static gboolean scroll_to_cursor(AppData* app, struct PreviewSync* s, EditorTab* tab, gboolean settle) {
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &cursor, gtk_text_buffer_get_insert(tab->buffer));
    gint offset = line_offset(app, s, gtk_text_iter_get_line(&cursor));
    if (offset < 0) {
        return FALSE;
    }
    if (offset == s->synced_offset) {
        return TRUE; /* same block (a paragraph is one point) */
    }
    s->synced_offset = offset;
    GdkRectangle visible, line;
    gtk_text_view_get_visible_rect(GTK_TEXT_VIEW(tab->view), &visible);
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(tab->view), &cursor, &line);
    gdouble align = visible.height > 0 ? CLAMP((gdouble)(line.y - visible.y) / visible.height, 0.0, 1.0) : 0.3;
    if (settle) {
        scroll_settled(app, s, offset, align, -1);
    } else {
        settle_stop(app, s);
        scroll_to(app, offset, align, -1);
    }
    return TRUE;
}

static void on_editor_cursor_moved(GObject* buffer, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    AppData* app = data;
    struct PreviewSync* s = app->preview_sync;
    EditorTab* tab = cursor_tab(app, GTK_TEXT_BUFFER(buffer));
    /* The user moves in the editor while the preview is on screen */
    if (s == NULL || s->idle_id != 0 || !s->keep || tab == NULL || g_strcmp0(tab->path, s->path) != 0 ||
        g_strcmp0(preview_state_path(app), s->path) != 0 || !gtk_widget_has_focus(tab->view) ||
        !gtk_widget_get_mapped(app->text_view)) {
        return;
    }
    scroll_to_cursor(app, s, tab, FALSE);
}

static void untrack(AppData* app, struct PreviewSync* s) {
    if (s->tracked != NULL) {
        g_signal_handlers_disconnect_by_func(s->tracked, G_CALLBACK(on_editor_cursor_moved), app);
        g_object_remove_weak_pointer(G_OBJECT(s->tracked), (gpointer*)&s->tracked);
        s->tracked = NULL;
    }
}

/* Follow the cursor of the editor of the file shown, if one is open */
static void track_editor(AppData* app, struct PreviewSync* s) {
    GtkTextBuffer* buffer = NULL;
    for (GList* l = app->editors; s->path != NULL && l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t != NULL && t->buffer != NULL && g_strcmp0(t->path, s->path) == 0) {
            buffer = t->buffer;
            break;
        }
    }
    if (buffer == s->tracked) {
        return;
    }
    untrack(app, s);
    if (buffer != NULL) {
        s->tracked = buffer;
        g_object_add_weak_pointer(G_OBJECT(buffer), (gpointer*)&s->tracked);
        g_signal_connect(buffer, "notify::cursor-position", G_CALLBACK(on_editor_cursor_moved), app);
    }
}

static void on_focus_changed(GObject* window, GParamSpec* pspec, gpointer data) {
    (void)window;
    (void)pspec;
    AppData* app = data;
    if (app->preview_sync != NULL) {
        track_editor(app, app->preview_sync);
    }
}

/* The new preview is in the buffer: back to the place kept when the file
 * was shown again (saved: the view does not move), else to the editor's
 * cursor when the file is being edited */
static void scroll_when_shown(AppData* app) {
    struct PreviewSync* s = app->preview_sync;
    g_clear_handle_id(&s->idle_id, g_source_remove);
    if (s->shown_tick_id != 0) {
        gtk_widget_remove_tick_callback(app->text_view, s->shown_tick_id);
        s->shown_tick_id = 0;
    }
    track_editor(app, s);
    if (s->goto_scroll) {
        s->goto_scroll = FALSE;
        s->restore = FALSE;
        gint offset = line_offset(app, s, s->goto_line);
        if (offset >= 0) scroll_settled(app, s, offset, GOTO_ALIGN, -1);
        return;
    }
    EditorTab* tab = cursor_tab(app, s->tracked);
    gboolean editing = tab != NULL && !gtk_widget_has_focus(app->text_view) &&
                       (gtk_widget_has_focus(tab->view) || tab == app->last_focused_editor) &&
                       gtk_widget_get_mapped(tab->view);
    if (s->restore) {
        gint offset = s->restore_line >= 0 ? line_offset(app, s, s->restore_line) : -1;
        scroll_settled(app, s, offset >= 0 ? offset : s->restore_offset, 0.0, s->restore_delta);
    } else if (editing) {
        scroll_to_cursor(app, s, tab, TRUE);
    }
    s->restore = FALSE;
}

static gboolean shown_idle(gpointer data) {
    AppData* app = data;
    app->preview_sync->idle_id = 0; /* removed by returning */
    scroll_when_shown(app);
    return G_SOURCE_REMOVE;
}

/* The frame clock may paint before the idle runs: the view would show the
 * top of the new preview for a frame */
static gboolean shown_tick(GtkWidget* widget, GdkFrameClock* clock, gpointer data) {
    (void)widget;
    (void)clock;
    AppData* app = data;
    app->preview_sync->shown_tick_id = 0;
    scroll_when_shown(app);
    return G_SOURCE_REMOVE;
}

void preview_sync_goto_line(AppData* app, const gchar* path, gint line) {
    struct PreviewSync* s = sync_get(app);
    g_free(s->goto_path);
    s->goto_path = (path != NULL && line >= 0) ? g_strdup(path) : NULL;
    s->goto_line = s->goto_path != NULL ? line : -1;
}

void preview_sync_loading(AppData* app, const gchar* path) {
    struct PreviewSync* s = sync_get(app);
    s->restore = FALSE;
    /* The line asked for is for the next load of its file only: a reload
     * (saved, m) keeps its place */
    s->goto_load = s->goto_path != NULL && g_strcmp0(path, s->goto_path) == 0;
    g_clear_pointer(&s->goto_path, g_free);
    if (s->goto_load) return;
    if (path == NULL || g_strcmp0(path, s->path) != 0 || g_strcmp0(path, preview_state_path(app)) != 0 ||
        preview_state_page(app) != s->page || preview_folding_restoring(app, path) ||
        !gtk_widget_get_mapped(app->text_view)) {
        return; /* another file or page, or a fold change keeping its own place */
    }
    GtkTextView* view = GTK_TEXT_VIEW(app->text_view);
    GdkRectangle visible;
    gtk_text_view_get_visible_rect(view, &visible);
    GtkTextIter top;
    gtk_text_view_get_iter_at_location(view, &top, visible.x, visible.y);
    s->restore_offset = gtk_text_iter_get_offset(&top);
    s->restore_line = offset_line(app, s, s->restore_offset);
    /* The place within the line kept (a paragraph, a PDF page) */
    gint anchor = s->restore_line >= 0 ? line_offset(app, s, s->restore_line) : s->restore_offset;
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_offset(app->buffer, &iter, anchor >= 0 ? anchor : s->restore_offset);
    gint y = 0, height = 0;
    gtk_text_view_get_line_yrange(view, &iter, &y, &height);
    s->restore_delta = MAX(visible.y - y, 0);
    s->restore = TRUE;
}

void preview_sync_showing(AppData* app, const gchar* path, const MdOut* md, gboolean keep) {
    struct PreviewSync* s = sync_get(app);
    settle_stop(app, s);
    GtkAdjustment* hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(app->text_view));
    if (hadj != NULL) {
        gtk_adjustment_set_value(hadj, gtk_adjustment_get_lower(hadj));
    }
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(app->text_view));
    if (vadj != NULL) {
        gtk_adjustment_set_value(vadj, gtk_adjustment_get_lower(vadj));
    }
    s->restore = s->restore && keep && g_strcmp0(path, s->path) == 0;
    s->goto_scroll = s->goto_load && keep;
    if (g_strcmp0(path, s->path) != 0) {
        g_free(s->path);
        s->path = g_strdup(path);
    }
    s->page = preview_state_page(app);
    g_clear_pointer(&s->sources, g_array_unref);
    if (md != NULL && md->sources->len > 0) {
        s->sources = g_array_copy(md->sources);
    }
    s->first_line = -1;
    s->synced_offset = -1;
    s->keep = keep;
    if (s->focus_handler == 0 && app->window != NULL) {
        s->window = app->window;
        g_object_add_weak_pointer(G_OBJECT(s->window), (gpointer*)&s->window);
        s->focus_handler = g_signal_connect(s->window, "notify::focus-widget", G_CALLBACK(on_focus_changed), app);
    }
    if (keep && s->idle_id == 0) {
        /* Before the frame: the view is not drawn at the top first */
        s->idle_id = g_idle_add_full(G_PRIORITY_HIGH_IDLE, shown_idle, app, NULL);
        s->shown_tick_id = gtk_widget_add_tick_callback(app->text_view, shown_tick, app, NULL);
    }
}

void preview_sync_text_lines(AppData* app, gint first_line) {
    sync_get(app)->first_line = first_line;
}

void preview_sync_free(AppData* app) {
    struct PreviewSync* s = app != NULL ? app->preview_sync : NULL;
    if (s == NULL) {
        return;
    }
    if (s->idle_id != 0) {
        g_source_remove(s->idle_id);
    }
    if (s->shown_tick_id != 0) {
        gtk_widget_remove_tick_callback(app->text_view, s->shown_tick_id);
    }
    settle_stop(app, s);
    if (s->window != NULL) {
        g_signal_handler_disconnect(s->window, s->focus_handler);
        g_object_remove_weak_pointer(G_OBJECT(s->window), (gpointer*)&s->window);
    }
    untrack(app, s);
    if (s->sources != NULL) {
        g_array_unref(s->sources);
    }
    g_free(s->path);
    g_free(s->goto_path);
    g_clear_pointer(&app->preview_sync, g_free);
}
