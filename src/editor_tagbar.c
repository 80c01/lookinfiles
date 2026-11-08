/* The tagbar of an editor tab (see editor_tagbar.h) */
#include "editor_tagbar.h"
#include "core/i18n.h"
#include "editor.h"
#include "code_outline.h"
#include "widgets/outline_pane.h"
#include "core/settings_store.h"

#define REFRESH_DELAY_MS 600   /* pause in the edits before the outline is read again */
#define CURSOR_DELAY_MS 80     /* cursor moves are followed after this pause */

typedef struct Tagbar {
    AppData* app;
    EditorTab* tab;          /* NULL once released */
    OutlinePane* pane;
    GCancellable* cancellable;
    guint refresh_source;
    guint cursor_source;
    gboolean stale;          /* the buffer changed while the tagbar was hidden */
} Tagbar;

static void tagbar_free(Tagbar* tb) {
    if (tb->cancellable != NULL) g_cancellable_cancel(tb->cancellable);
    g_clear_object(&tb->cancellable);
    g_clear_handle_id(&tb->refresh_source, g_source_remove);
    g_clear_handle_id(&tb->cursor_source, g_source_remove);
    g_free(tb);
}

static gboolean tagbar_wanted(Tagbar* tb) {
    return tb->tab != NULL && tb->app->settings.tagbar && code_outline_supported(tb->tab);
}

/* ------------------------------------------------------------------------- */
/* Pane callbacks                                                            */
/* ------------------------------------------------------------------------- */

static gint cursor_line(gpointer data) {
    Tagbar* tb = data;
    EditorTab* tab = tb->tab;
    if (tab == NULL) return -1;
    GtkTextMark* mark = tab->view_passive && tab->view_insert != NULL ? tab->view_insert
                                                                        : gtk_text_buffer_get_insert(tab->buffer);
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &it, mark);
    return gtk_text_iter_get_line(&it);
}

static void go_to(gpointer data, const CodeOutlineEntry* entry) {
    Tagbar* tb = data;
    EditorTab* tab = tb->tab;
    if (tab == NULL) return;
    EditorTab* dest = NULL;
    if (tab->path != NULL && tab->path[0] != '\0') {
        dest = code_nav_jump(tb->app, tab, tab->path, entry->line, entry->column);
    } else {
        GtkTextIter where;
        gtk_text_buffer_get_iter_at_line_index(tab->buffer, &where, entry->line, entry->column);
        gtk_text_buffer_place_cursor(tab->buffer, &where);
        editor_center_cursor(tab);
        dest = tab;
    }
    if (dest != NULL && dest->view != NULL) gtk_widget_grab_focus(dest->view);
}

static void escape(gpointer data) {
    Tagbar* tb = data;
    if (tb->tab != NULL && tb->tab->view != NULL) gtk_widget_grab_focus(tb->tab->view);
}

static void close_tagbar(gpointer data) {
    Tagbar* tb = data;
    if (tb->tab != NULL) editor_tagbar_toggle(tb->app, tb->tab, NULL);
}

static const OutlinePaneCallbacks callbacks = {go_to, cursor_line, escape, close_tagbar};

/* ------------------------------------------------------------------------- */
/* Cursor and outline                                                        */
/* ------------------------------------------------------------------------- */

static gboolean on_cursor_timeout(gpointer data) {
    Tagbar* tb = data;
    tb->cursor_source = 0;
    if (!outline_pane_has_focus(tb->pane)) outline_pane_follow_cursor(tb->pane);
    return G_SOURCE_REMOVE;
}

static void on_mark_set(GtkTextBuffer* buffer, const GtkTextIter* where, GtkTextMark* mark, gpointer data) {
    (void)where;
    Tagbar* tb = data;
    if (mark != gtk_text_buffer_get_insert(buffer) || !outline_pane_shown(tb->pane)) return;
    if (tb->cursor_source == 0) tb->cursor_source = g_timeout_add(CURSOR_DELAY_MS, on_cursor_timeout, tb);
}

static void on_outline(AppData* app, EditorTab* tab, GPtrArray* entries, const gchar* source, gpointer data) {
    (void)app;
    (void)tab;
    Tagbar* tb = data;
    outline_pane_set_entries(tb->pane, entries, g_strcmp0(source, "Markdown headings") == 0, source);
}

static void refresh_now(Tagbar* tb) {
    g_clear_handle_id(&tb->refresh_source, g_source_remove);
    if (tb->tab == NULL) return;
    tb->stale = FALSE;
    if (tb->cancellable != NULL) g_cancellable_cancel(tb->cancellable);
    g_clear_object(&tb->cancellable);
    tb->cancellable = g_cancellable_new();
    /* tb stays alive while its cancellable is not cancelled: nothing to free */
    code_outline_collect(tb->app, tb->tab, tb->cancellable, on_outline, tb, NULL);
}

static gboolean on_refresh_timeout(gpointer data) {
    Tagbar* tb = data;
    tb->refresh_source = 0;
    refresh_now(tb);
    return G_SOURCE_REMOVE;
}

static void update_visibility(Tagbar* tb) {
    gboolean show = tagbar_wanted(tb);
    if (show == outline_pane_shown(tb->pane)) return;
    outline_pane_set_shown(tb->pane, show);
    if (show && (!outline_pane_has_entries(tb->pane) || tb->stale)) refresh_now(tb);
}

static void on_buffer_changed(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    Tagbar* tb = data;
    if (!outline_pane_shown(tb->pane)) {
        /* Saved under another name, the file may now have an outline */
        tb->stale = TRUE;
        update_visibility(tb);
        return;
    }
    g_clear_handle_id(&tb->refresh_source, g_source_remove);
    tb->refresh_source = g_timeout_add(REFRESH_DELAY_MS, on_refresh_timeout, tb);
}

/* ------------------------------------------------------------------------- */
/* Public                                                                    */
/* ------------------------------------------------------------------------- */

GtkWidget* editor_tagbar_wrap(AppData* app, EditorTab* tab, GtkWidget* content) {
    Tagbar* tb = g_new0(Tagbar, 1);
    tb->app = app;
    tb->tab = tab;
    tab->tagbar = tb;
    tb->pane = outline_pane_new(content, &app->editor_tagbar_width, _("Reading the symbols…"), &callbacks, tb);
    GtkWidget* paned = outline_pane_widget(tb->pane);
    g_object_set_data_full(G_OBJECT(paned), "editor-tagbar", tb, (GDestroyNotify)tagbar_free);
    g_signal_connect(tab->buffer, "mark-set", G_CALLBACK(on_mark_set), tb);
    g_signal_connect_after(tab->buffer, "changed", G_CALLBACK(on_buffer_changed), tb);
    update_visibility(tb);
    return paned;
}

void editor_tagbar_release(EditorTab* tab) {
    Tagbar* tb = tab != NULL ? tab->tagbar : NULL;
    if (tb == NULL) return;
    if (tab->buffer != NULL) g_signal_handlers_disconnect_by_data(tab->buffer, tb);
    if (tb->cancellable != NULL) g_cancellable_cancel(tb->cancellable);
    g_clear_handle_id(&tb->refresh_source, g_source_remove);
    g_clear_handle_id(&tb->cursor_source, g_source_remove);
    outline_pane_detach(tb->pane);
    tb->tab = NULL;
    tab->tagbar = NULL;
}

static void update_tabs(GList* tabs) {
    for (GList* l = tabs; l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t != NULL && t->tagbar != NULL) update_visibility(t->tagbar);
    }
}

void editor_tagbar_set_shown(AppData* app, gboolean shown) {
    if (app == NULL || app->settings.tagbar == shown) return;
    app->settings.tagbar = shown;
    update_tabs(app->editors);
    for (gint i = 0; i < app->num_workspaces; i++) {
        if (i != app->current_ws) update_tabs(app->workspaces[i].editors);
    }
}

static void place_tabs(GList* tabs) {
    for (GList* l = tabs; l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t != NULL && t->tagbar != NULL) outline_pane_place(t->tagbar->pane);
    }
}

void editor_tagbar_set_width(AppData* app, gint width) {
    if (app == NULL || app->editor_tagbar_width == width) return;
    app->editor_tagbar_width = width;
    place_tabs(app->editors);
    for (gint i = 0; i < app->num_workspaces; i++) {
        if (i != app->current_ws) place_tabs(app->workspaces[i].editors);
    }
}

void editor_tagbar_toggle(AppData* app, EditorTab* tab, CodeNavMessageFunc message) {
    if (app == NULL || tab == NULL) return;
    if (!app->settings.tagbar && !code_outline_supported(tab)) {
        if (message != NULL) message(tab, _("No tagbar for this file type (code and Markdown files)"));
        return;
    }
    editor_tagbar_set_shown(app, !app->settings.tagbar);
    Tagbar* tb = tab->tagbar;
    if (app->settings.tagbar && tb != NULL && outline_pane_shown(tb->pane)) {
        outline_pane_focus(tb->pane);
    } else if (tab->view != NULL) {
        gtk_widget_grab_focus(tab->view);
    }
    gchar* path = settings_store_path();
    Settings s;
    settings_store_load(path, &s);
    s.tagbar = app->settings.tagbar;
    GError* error = NULL;
    if (!settings_store_save(path, &s, &error)) {
        g_printerr("[ Tagbar setting not saved: %s ]\n", error->message);
        g_clear_error(&error);
    }
    g_free(path);
}
