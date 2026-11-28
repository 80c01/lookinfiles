/* The PDF preview follows the cursor of a LaTeX source (see latex_sync.h) */
#include "latex/latex_sync.h"
#include "editor/editor.h"
#include "latex/latex_synctex.h"
#include "preview/pdf.h"
#include "preview/preview_state.h"
#include <string.h>

/* Pause after a cursor move before asking SyncTeX */
#define SYNC_DELAY_MS 120
/* The place is shown again this long after the pages change (the view
 * estimates the height of the pages not laid out yet) */
#define SETTLE_MS     100
#define SETTLE_TIMES  3
/* After a compilation, the new pages are followed this long */
#define BUILD_FOLLOW_US (4 * G_USEC_PER_SEC)

struct LatexSync {
    AppData* app;
    GtkWindow* window;      /* window whose focus is watched, NULL once destroyed */
    gulong focus_handler;
    GtkTextBuffer* tracked; /* buffer of the .tex tab followed (weak), or NULL */
    guint delay_id;         /* SyncTeX asked after a pause */
    GCancellable* running;  /* "synctex view" running, or NULL */
    GCancellable* editing;  /* "synctex edit" running (Ctrl+Click), or NULL */
    gboolean again;         /* the cursor moved meanwhile */
    gchar* last_key;        /* PDF, source and line last asked for */
    /* The place found, shown again while the preview settles */
    gchar* place_pdf;
    gint place_page;
    gdouble place_y;
    gdouble keep_align;     /* its height in the preview kept after a compilation, or -1 */
    guint settle_id;
    gint settle_left;
    gint64 follow_until;    /* pages followed after a compilation until then */
    gulong buffer_handler;  /* "changed" of the preview buffer while followed */
};

static gboolean is_tex(const gchar* path) {
    return path != NULL && g_str_has_suffix(path, ".tex");
}

/* The editor tab of buffer that has its cursor (a split view shares it) */
static EditorTab* buffer_tab(AppData* app, GtkTextBuffer* buffer) {
    EditorTab* found = NULL;
    for (GList* l = app->editors; buffer != NULL && l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t != NULL && t->buffer == buffer && t->view != NULL && is_tex(t->path)) {
            if (!t->view_passive) return t;
            found = found != NULL ? found : t;
        }
    }
    return found;
}

/* The PDF shown in the preview when it has SyncTeX data, or NULL */
static const gchar* synced_pdf(AppData* app) {
    const gchar* pdf = preview_state_path(app);
    if (pdf == NULL || !g_str_has_suffix(pdf, ".pdf") || preview_state_raw(app)) return NULL;
    gchar* synctex = latex_synctex_file(pdf);
    if (synctex == NULL) return NULL;
    g_free(synctex);
    return pdf;
}

/* Height of the cursor in its editor view (0: top, 1: bottom) */
static gdouble cursor_align(EditorTab* tab) {
    if (tab == NULL || tab->view == NULL) return 0.3;
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &cursor, gtk_text_buffer_get_insert(tab->buffer));
    GdkRectangle visible, line;
    gtk_text_view_get_visible_rect(GTK_TEXT_VIEW(tab->view), &visible);
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(tab->view), &cursor, &line);
    return visible.height > 0 ? CLAMP((gdouble)(line.y - visible.y) / visible.height, 0.0, 1.0) : 0.3;
}

/* Scroll the preview to the place found; FALSE when its page is not shown */
static gboolean show_place(struct LatexSync* st) {
    AppData* app = st->app;
    if (st->place_pdf == NULL || g_strcmp0(preview_state_path(app), st->place_pdf) != 0) return FALSE;
    gdouble align = st->keep_align >= 0.0 ? st->keep_align : cursor_align(buffer_tab(app, st->tracked));
    return pdf_preview_show_point(app, st->place_page, st->place_y, align);
}

static gboolean settle_cb(gpointer data) {
    struct LatexSync* st = data;
    show_place(st);
    if (--st->settle_left > 0) return G_SOURCE_CONTINUE;
    st->settle_id = 0;
    return G_SOURCE_REMOVE;
}

/* Show the place now, then again a few times while the pages are laid out */
static void show_settled(struct LatexSync* st) {
    show_place(st);
    st->settle_left = SETTLE_TIMES;
    if (st->settle_id == 0) st->settle_id = g_timeout_add(SETTLE_MS, settle_cb, st);
}

static void stop_following(struct LatexSync* st) {
    if (st->buffer_handler != 0 && st->app->buffer != NULL) {
        g_signal_handler_disconnect(st->app->buffer, st->buffer_handler);
    }
    st->buffer_handler = 0;
    st->follow_until = 0;
}

/* The preview buffer changed after a compilation (the new PDF, its next pages) */
static void on_preview_changed(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    struct LatexSync* st = data;
    if (g_get_monotonic_time() > st->follow_until) {
        stop_following(st);
        return;
    }
    show_settled(st);
}

static void query(struct LatexSync* st, gboolean force);

static void on_view_done(GObject* source, GAsyncResult* result, gpointer data) {
    gchar* out = NULL;
    GError* error = NULL;
    g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source), result, &out, NULL, &error);
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        /* At exit: st is gone */
        g_error_free(error);
        g_free(out);
        return;
    }
    g_clear_error(&error);
    struct LatexSync* st = data;
    g_clear_object(&st->running);
    gint page = 0;
    gdouble y = 0.0;
    gchar* pdf = g_object_steal_data(source, "latex-sync-pdf");
    if (latex_synctex_parse_view(out, &page, &y)) {
        g_free(st->place_pdf);
        st->place_pdf = g_steal_pointer(&pdf);
        st->place_page = page;
        st->place_y = y;
        show_settled(st);
    }
    g_free(pdf);
    g_free(out);
    if (st->again) {
        st->again = FALSE;
        query(st, FALSE);
    }
}

/* Ask SyncTeX where the cursor's line is in the PDF shown (force: even
 * when it is the line asked for last) */
static void query(struct LatexSync* st, gboolean force) {
    AppData* app = st->app;
    EditorTab* tab = buffer_tab(app, st->tracked);
    const gchar* pdf = synced_pdf(app);
    if (tab == NULL || pdf == NULL) return;
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &cursor, gtk_text_buffer_get_insert(tab->buffer));
    gint line = gtk_text_iter_get_line(&cursor) + 1;
    gchar* key = g_strdup_printf("%s\n%s\n%d", pdf, tab->path, line);
    if (!force && g_strcmp0(key, st->last_key) == 0) {
        g_free(key);
        show_place(st); /* same line: the view may have moved */
        return;
    }
    if (st->running != NULL) {
        st->again = TRUE;
        g_free(key);
        return;
    }
    g_free(st->last_key);
    st->last_key = key;

    gchar* input = g_strdup_printf("%d:0:%s", line, tab->path);
    const gchar* argv[] = {"synctex", "view", "-i", input, "-o", pdf, NULL};
    GSubprocessLauncher* launcher =
        g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
    gchar* dir = g_path_get_dirname(pdf);
    g_subprocess_launcher_set_cwd(launcher, dir);
    g_free(dir);
    GSubprocess* proc = g_subprocess_launcher_spawnv(launcher, argv, NULL);
    g_object_unref(launcher);
    g_free(input);
    if (proc == NULL) return; /* synctex is not installed */
    g_object_set_data_full(G_OBJECT(proc), "latex-sync-pdf", g_strdup(pdf), g_free);
    st->running = g_cancellable_new();
    g_subprocess_communicate_utf8_async(proc, NULL, st->running, on_view_done, st);
    g_object_unref(proc);
}

static gboolean delay_cb(gpointer data) {
    struct LatexSync* st = data;
    st->delay_id = 0;
    query(st, FALSE);
    return G_SOURCE_REMOVE;
}

static void on_cursor_moved(GObject* buffer, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    struct LatexSync* st = data;
    EditorTab* tab = buffer_tab(st->app, GTK_TEXT_BUFFER(buffer));
    /* The user moves in the editor (not a reload of the tab) */
    if (tab == NULL || !gtk_widget_has_focus(tab->view) || synced_pdf(st->app) == NULL) return;
    st->keep_align = -1.0;
    if (st->delay_id != 0) g_source_remove(st->delay_id);
    st->delay_id = g_timeout_add(SYNC_DELAY_MS, delay_cb, st);
}

static void untrack(struct LatexSync* st) {
    if (st->tracked != NULL) {
        g_signal_handlers_disconnect_by_func(st->tracked, G_CALLBACK(on_cursor_moved), st);
        g_object_remove_weak_pointer(G_OBJECT(st->tracked), (gpointer*)&st->tracked);
        st->tracked = NULL;
    }
}

static void track(struct LatexSync* st, GtkTextBuffer* buffer) {
    if (buffer == NULL || buffer == st->tracked) return;
    untrack(st);
    st->tracked = buffer;
    g_object_add_weak_pointer(G_OBJECT(buffer), (gpointer*)&st->tracked);
    g_signal_connect(buffer, "notify::cursor-position", G_CALLBACK(on_cursor_moved), st);
}

/* The focus went into a .tex editor tab: follow its cursor */
static void on_focus_changed(GObject* window, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    struct LatexSync* st = data;
    GtkWidget* focus = gtk_window_get_focus(GTK_WINDOW(window));
    for (GList* l = st->app->editors; focus != NULL && l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t != NULL && t->view == focus && is_tex(t->path)) {
            track(st, t->buffer);
            return;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Back from the PDF: Ctrl+Click opens the source at the place (synctex edit) */
/* ------------------------------------------------------------------------- */

typedef struct {
    gchar* pdf;
    gint page;
    gdouble y;
    gdouble align; /* height of the click in the preview */
} EditClick;

static void edit_click_free(gpointer data) {
    EditClick* c = data;
    g_free(c->pdf);
    g_free(c);
}

static void on_edit_done(GObject* source, GAsyncResult* result, gpointer data) {
    gchar* out = NULL;
    GError* error = NULL;
    g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source), result, &out, NULL, &error);
    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_error_free(error); /* at exit, or a newer click: st may be gone */
        g_free(out);
        return;
    }
    g_clear_error(&error);
    struct LatexSync* st = data;
    g_clear_object(&st->editing);
    EditClick* c = g_object_get_data(source, "latex-sync-click");
    gchar* dir = g_path_get_dirname(c->pdf);
    gchar* input = NULL;
    gint line = 0;
    if (latex_synctex_parse_edit(out, dir, &input, &line) && g_file_test(input, G_FILE_TEST_IS_REGULAR)) {
        /* The PDF stays where it is: the place clicked is the cursor's */
        g_free(st->place_pdf);
        st->place_pdf = g_strdup(c->pdf);
        st->place_page = c->page;
        st->place_y = c->y;
        st->keep_align = c->align;
        g_free(st->last_key);
        st->last_key = g_strdup_printf("%s\n%s\n%d", c->pdf, input, line + 1);
        EditorTab* tab = editor_open_at(st->app, input, line, 0);
        if (tab != NULL) track(st, tab->buffer);
    }
    g_free(input);
    g_free(dir);
    g_free(out);
}

static void on_preview_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    struct LatexSync* st = data;
    AppData* app = st->app;
    GdkModifierType mods = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));
    if (n_press != 1 || (mods & GDK_CONTROL_MASK) == 0 || (mods & (GDK_SHIFT_MASK | GDK_ALT_MASK)) != 0) return;
    const gchar* pdf = synced_pdf(app);
    if (pdf == NULL) return;
    gint bx = 0, by = 0;
    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(app->text_view), GTK_TEXT_WINDOW_WIDGET, (gint)x, (gint)y,
                                          &bx, &by);
    gint page = 0;
    gdouble px = 0.0, py = 0.0;
    if (!pdf_preview_page_at(app, bx, by, &page, &px, &py)) return;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);

    gchar* arg = latex_synctex_edit_arg(page, px, py, pdf);
    const gchar* argv[] = {"synctex", "edit", "-o", arg, NULL};
    GSubprocessLauncher* launcher =
        g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
    gchar* dir = g_path_get_dirname(pdf);
    g_subprocess_launcher_set_cwd(launcher, dir);
    g_free(dir);
    GSubprocess* proc = g_subprocess_launcher_spawnv(launcher, argv, NULL);
    g_object_unref(launcher);
    g_free(arg);
    if (proc == NULL) return; /* synctex is not installed */
    EditClick* c = g_new0(EditClick, 1);
    c->pdf = g_strdup(pdf);
    c->page = page;
    c->y = py;
    c->align = pdf_preview_point_align(app, page, py);
    g_object_set_data_full(G_OBJECT(proc), "latex-sync-click", c, edit_click_free);
    if (st->editing != NULL) {
        g_cancellable_cancel(st->editing); /* the newer click wins */
        g_object_unref(st->editing);
    }
    st->editing = g_cancellable_new();
    g_subprocess_communicate_utf8_async(proc, NULL, st->editing, on_edit_done, st);
    g_object_unref(proc);
}

void latex_sync_init(AppData* app) {
    if (app->latex_sync != NULL || app->window == NULL) return;
    struct LatexSync* st = g_new0(struct LatexSync, 1);
    st->app = app;
    st->keep_align = -1.0;
    st->window = app->window;
    g_object_add_weak_pointer(G_OBJECT(st->window), (gpointer*)&st->window);
    st->focus_handler = g_signal_connect(st->window, "notify::focus-widget", G_CALLBACK(on_focus_changed), st);
    app->latex_sync = st;
}

void latex_sync_attach_preview(AppData* app) {
    struct LatexSync* st = app != NULL ? app->latex_sync : NULL;
    if (st == NULL || app->text_view == NULL) return;
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
    g_signal_connect(click, "pressed", G_CALLBACK(on_preview_click), st);
    gtk_widget_add_controller(app->text_view, GTK_EVENT_CONTROLLER(click));
}

void latex_sync_after_build(AppData* app) {
    struct LatexSync* st = app->latex_sync;
    if (st == NULL) return;
    if (buffer_tab(app, st->tracked) == NULL && app->last_focused_editor != NULL &&
        is_tex(app->last_focused_editor->path)) {
        track(st, app->last_focused_editor->buffer);
    }
    /* The place of the cursor stays where it is in the preview (the old PDF
     * is still shown): saving does not scroll it */
    st->keep_align = -1.0;
    if (st->place_pdf != NULL && g_strcmp0(preview_state_path(app), st->place_pdf) == 0) {
        st->keep_align = pdf_preview_point_align(app, st->place_page, st->place_y);
    }
    /* The new PDF and its pages come in over the next moments */
    st->follow_until = g_get_monotonic_time() + BUILD_FOLLOW_US;
    if (st->buffer_handler == 0 && app->buffer != NULL) {
        st->buffer_handler = g_signal_connect(app->buffer, "changed", G_CALLBACK(on_preview_changed), st);
    }
    query(st, TRUE);
}

void latex_sync_free(AppData* app) {
    struct LatexSync* st = app != NULL ? app->latex_sync : NULL;
    if (st == NULL) return;
    if (st->running != NULL) {
        g_cancellable_cancel(st->running);
        g_object_unref(st->running);
    }
    if (st->editing != NULL) {
        g_cancellable_cancel(st->editing);
        g_object_unref(st->editing);
    }
    if (st->delay_id != 0) g_source_remove(st->delay_id);
    if (st->settle_id != 0) g_source_remove(st->settle_id);
    stop_following(st);
    if (st->window != NULL) {
        g_signal_handler_disconnect(st->window, st->focus_handler);
        g_object_remove_weak_pointer(G_OBJECT(st->window), (gpointer*)&st->window);
    }
    untrack(st);
    g_free(st->last_key);
    g_free(st->place_pdf);
    g_free(st);
    app->latex_sync = NULL;
}
