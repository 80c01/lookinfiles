/* Preview kept on the line being edited (see preview_sync.h). */
#include "preview/preview_sync.h"
#include "preview/preview_folding.h"
#include "preview/preview_state.h"

#define SYNC_MARK "preview-sync"

struct PreviewSync {
    gchar* path;            /* file in the preview */
    guint page;             /* its page (mailbox messages) */
    GArray* sources;        /* MdSource of a rendered document, or NULL */
    gint first_line;        /* text shown line by line from this buffer line, or -1 */
    gboolean keep;          /* the view goes back to a line once shown */
    gboolean restore;       /* the place below is kept for the next preview of path */
    gint restore_line;      /* source line at the top of the view, or -1 */
    gint restore_offset;    /* else the offset at the top */
    guint idle_id;          /* scroll once the new preview is in the buffer */
    gint synced_offset;     /* offset last scrolled to for the editor's cursor */
    GtkTextBuffer* tracked; /* editor buffer of path whose cursor is followed */
    GtkWindow* window;      /* window whose focus is watched (an editor of path */
    gulong focus_handler;   /* may have opened), NULL once destroyed */
};

static struct PreviewSync* sync_get(AppData* app) {
    if (app->preview_sync == NULL) {
        app->preview_sync = g_new0(struct PreviewSync, 1);
        app->preview_sync->first_line = -1;
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

/* Show offset of the preview at height align of the view (0: top, 1: bottom) */
static void scroll_to(AppData* app, gint offset, gdouble align) {
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_offset(app->buffer, &iter, offset);
    GtkTextMark* mark = gtk_text_buffer_get_mark(app->buffer, SYNC_MARK);
    if (mark == NULL) {
        mark = gtk_text_buffer_create_mark(app->buffer, SYNC_MARK, &iter, TRUE);
    } else {
        gtk_text_buffer_move_mark(app->buffer, mark, &iter);
    }
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(app->text_view), mark, 0.0, TRUE, 0.0, align);
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
static gboolean scroll_to_cursor(AppData* app, struct PreviewSync* s, EditorTab* tab) {
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
    scroll_to(app, offset, align);
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
    scroll_to_cursor(app, s, tab);
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

/* The new preview is in the buffer: to the editor's cursor when the file is
 * being edited, else back to the line kept */
static gboolean scroll_when_shown(gpointer data) {
    AppData* app = data;
    struct PreviewSync* s = app->preview_sync;
    s->idle_id = 0;
    track_editor(app, s);
    EditorTab* tab = cursor_tab(app, s->tracked);
    gboolean editing = tab != NULL && !gtk_widget_has_focus(app->text_view) &&
                       (gtk_widget_has_focus(tab->view) || tab == app->last_focused_editor) &&
                       (s->restore || gtk_widget_get_mapped(tab->view));
    if (!(editing && scroll_to_cursor(app, s, tab)) && s->restore) {
        gint offset = s->restore_line >= 0 ? line_offset(app, s, s->restore_line) : -1;
        scroll_to(app, offset >= 0 ? offset : s->restore_offset, 0.0);
    }
    s->restore = FALSE;
    return G_SOURCE_REMOVE;
}

void preview_sync_loading(AppData* app, const gchar* path) {
    struct PreviewSync* s = sync_get(app);
    s->restore = FALSE;
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
    s->restore = TRUE;
}

void preview_sync_showing(AppData* app, const gchar* path, const MdOut* md, gboolean keep) {
    struct PreviewSync* s = sync_get(app);
    GtkAdjustment* hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(app->text_view));
    if (hadj != NULL) {
        gtk_adjustment_set_value(hadj, gtk_adjustment_get_lower(hadj));
    }
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(app->text_view));
    if (vadj != NULL) {
        gtk_adjustment_set_value(vadj, gtk_adjustment_get_lower(vadj));
    }
    s->restore = s->restore && keep && g_strcmp0(path, s->path) == 0;
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
        s->idle_id = g_idle_add_full(G_PRIORITY_HIGH_IDLE, scroll_when_shown, app, NULL);
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
    if (s->window != NULL) {
        g_signal_handler_disconnect(s->window, s->focus_handler);
        g_object_remove_weak_pointer(G_OBJECT(s->window), (gpointer*)&s->window);
    }
    untrack(app, s);
    if (s->sources != NULL) {
        g_array_unref(s->sources);
    }
    g_free(s->path);
    g_clear_pointer(&app->preview_sync, g_free);
}
