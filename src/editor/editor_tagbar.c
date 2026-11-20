/* The tagbar of an editor tab (see editor_tagbar.h) */
#include "editor/editor_tagbar.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "editor/editor.h"
#include "code/code_outline.h"
#include "widgets/outline_pane.h"
#include "widgets/focus_route.h"

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
    gboolean wanted;         /* shown in this tab (when the file has an outline) */
    GtkWidget* status;       /* Vim mode: the tagbar window's status line */
    GtkWidget* status_order; /* "Name" (grouped by kind) or "Order" (file order) */
    GtkWidget* status_file;
} Tagbar;

static void tagbar_free(Tagbar* tb) {
    if (tb->cancellable != NULL) g_cancellable_cancel(tb->cancellable);
    g_clear_object(&tb->cancellable);
    g_clear_handle_id(&tb->refresh_source, g_source_remove);
    g_clear_handle_id(&tb->cursor_source, g_source_remove);
    g_free(tb);
}

static gboolean tagbar_wanted(Tagbar* tb) {
    return tb->tab != NULL && tb->wanted && code_outline_supported(tb->tab);
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

/* The navigation keys of the tab's mode */
static NavStyle nav_style(gpointer data) {
    Tagbar* tb = data;
    EditorMode mode = tb->tab != NULL ? tb->tab->mode : EDITOR_MODE_NOTEPAD;
    if (mode == EDITOR_MODE_VIM) return NAV_STYLE_VIM;
    if (mode == EDITOR_MODE_EMACS || mode == EDITOR_MODE_ORGMODE) return NAV_STYLE_EMACS;
    return NAV_STYLE_NOTEPAD;
}

static void panel_action(gpointer data, NavAction action) {
    Tagbar* tb = data;
    focus_route_panel_action(tb->app, outline_pane_list(tb->pane), action);
}

/* The status line of Vim's Tagbar window (airline style): "Tagbar", the
 * sort ("Name" or "Order"), the file */
static void update_status(Tagbar* tb) {
    if (tb->tab == NULL || !gtk_widget_get_visible(tb->status)) return;
    gtk_label_set_text(GTK_LABEL(tb->status_order), outline_pane_grouped(tb->pane) ? "Name" : "Order");
    gtk_label_set_text(GTK_LABEL(tb->status_file), tb->tab->name != NULL ? tb->tab->name : "[No Name]");
}

static void sort_changed(gpointer data) {
    update_status(data);
}

static const OutlinePaneCallbacks callbacks = {go_to, cursor_line, escape, close_tagbar, nav_style, panel_action,
                                               sort_changed};

static GtkWidget* status_new(Tagbar* tb) {
    GtkWidget* status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(status, "vim-statusline");
    GtkWidget* badge = gtk_label_new("Tagbar");
    gtk_widget_add_css_class(badge, "vim-mode-badge");
    gtk_widget_add_css_class(badge, "vim-mode-normal");
    gtk_box_append(GTK_BOX(status), badge);
    tb->status_order = gtk_label_new(NULL);
    gtk_box_append(GTK_BOX(status), tb->status_order);
    tb->status_file = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(tb->status_file), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(tb->status_file), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(tb->status_file, TRUE);
    gtk_box_append(GTK_BOX(status), tb->status_file);
    gtk_widget_set_visible(status, FALSE);
    return status;
}

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
    update_status(tb); /* headings are in file order; the file may have a new name */
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
    tb->wanted = app->settings.tagbar; /* new tabs follow Preferences → Editor */
    tab->tagbar = tb;
    tb->pane = outline_pane_new(content, &app->editor_tagbar_width, _("Reading the symbols…"), &callbacks, tb);
    tb->status = status_new(tb);
    outline_pane_set_footer(tb->pane, tb->status);
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

static void update_tabs(GList* tabs, gboolean shown) {
    for (GList* l = tabs; l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t == NULL || t->tagbar == NULL) continue;
        t->tagbar->wanted = shown;
        update_visibility(t->tagbar);
    }
}

void editor_tagbar_set_shown(AppData* app, gboolean shown) {
    if (app == NULL) return;
    app->settings.tagbar = shown;
    update_tabs(app->editors, shown);
    for (gint i = 0; i < app->num_workspaces; i++) {
        if (i != app->current_ws) update_tabs(app->workspaces[i].editors, shown);
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
    Tagbar* tb = tab != NULL ? tab->tagbar : NULL;
    if (app == NULL || tb == NULL) return;
    gboolean shown = outline_pane_shown(tb->pane);
    if (!shown && !code_outline_supported(tab)) {
        if (message != NULL) message(tab, _("No tagbar for this file type (code and Markdown files)"));
        return;
    }
    tb->wanted = !shown; /* this tab only: Preferences → Editor sets the others */
    update_visibility(tb);
    if (tb->wanted && outline_pane_shown(tb->pane)) {
        outline_pane_focus(tb->pane);
    } else if (tab->view != NULL) {
        gtk_widget_grab_focus(tab->view);
    }
    app_emit(app, APP_EVENT_LAYOUT_CHANGED);
}

void editor_tagbar_set_vim_status(EditorTab* tab, gboolean shown) {
    Tagbar* tb = tab != NULL ? tab->tagbar : NULL;
    if (tb == NULL) return;
    gtk_widget_set_visible(tb->status, shown);
    update_status(tb);
}

gboolean editor_tagbar_available(EditorTab* tab) {
    return tab != NULL && tab->tagbar != NULL && code_outline_supported(tab);
}

gboolean editor_tagbar_shown(EditorTab* tab) {
    return tab != NULL && tab->tagbar != NULL && outline_pane_shown(tab->tagbar->pane);
}

GtkWidget* editor_tagbar_list(EditorTab* tab) {
    return editor_tagbar_shown(tab) ? outline_pane_list(tab->tagbar->pane) : NULL;
}

/* ------------------------------------------------------------------------- */
/* States                                                                    */
/* ------------------------------------------------------------------------- */

static GWeakRef* weak_ref_new(gpointer obj) {
    GWeakRef* ref = g_new0(GWeakRef, 1);
    g_weak_ref_init(ref, obj);
    return ref;
}

static void weak_ref_free(gpointer data) {
    GWeakRef* ref = data;
    if (ref != NULL) {
        g_weak_ref_clear(ref);
        g_free(ref);
    }
}

struct EditorTagbarStates {
    GPtrArray* panes;   /* GWeakRef* to the tabs' GtkPaned (they hold the Tagbar) */
    GArray* wanted;     /* gboolean per pane */
    gboolean last;      /* settings.tagbar */
};

static void take_tabs(EditorTagbarStates* st, GList* tabs) {
    for (GList* l = tabs; l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t == NULL || t->tagbar == NULL) continue;
        g_ptr_array_add(st->panes, weak_ref_new(outline_pane_widget(t->tagbar->pane)));
        g_array_append_val(st->wanted, t->tagbar->wanted);
    }
}

EditorTagbarStates* editor_tagbar_states_take(AppData* app) {
    EditorTagbarStates* st = g_new0(EditorTagbarStates, 1);
    st->panes = g_ptr_array_new_with_free_func(weak_ref_free);
    st->wanted = g_array_new(FALSE, FALSE, sizeof(gboolean));
    st->last = app->settings.tagbar;
    take_tabs(st, app->editors);
    for (gint i = 0; i < app->num_workspaces; i++) {
        if (i != app->current_ws) take_tabs(st, app->workspaces[i].editors);
    }
    return st;
}

void editor_tagbar_states_restore(AppData* app, const EditorTagbarStates* st) {
    if (app == NULL || st == NULL) return;
    app->settings.tagbar = st->last;
    for (guint i = 0; i < st->panes->len; i++) {
        GtkWidget* pane = g_weak_ref_get(g_ptr_array_index(st->panes, i));
        if (pane == NULL) continue; /* closed meanwhile */
        Tagbar* tb = g_object_get_data(G_OBJECT(pane), "editor-tagbar");
        if (tb != NULL && tb->tab != NULL) {
            tb->wanted = g_array_index(st->wanted, gboolean, i);
            update_visibility(tb);
        }
        g_object_unref(pane);
    }
}

gboolean editor_tagbar_states_same(const EditorTagbarStates* a, const EditorTagbarStates* b) {
    if (a == b) return TRUE;
    if (a == NULL || b == NULL) return FALSE;
    if (a->last != b->last) return FALSE;
    GHashTable* map = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (guint i = 0; i < a->panes->len; i++) {
        GtkWidget* pane = g_weak_ref_get(g_ptr_array_index(a->panes, i));
        if (pane != NULL) {
            g_hash_table_replace(map, pane, GINT_TO_POINTER(g_array_index(a->wanted, gboolean, i) ? 2 : 1));
            g_object_unref(pane);
        }
    }
    gboolean same = TRUE;
    for (guint i = 0; i < b->panes->len; i++) {
        GtkWidget* pane = g_weak_ref_get(g_ptr_array_index(b->panes, i));
        if (pane == NULL) continue;
        gpointer prev = g_hash_table_lookup(map, pane);
        g_object_unref(pane);
        gpointer cur = GINT_TO_POINTER(g_array_index(b->wanted, gboolean, i) ? 2 : 1);
        if (prev != NULL && prev != cur) {
            same = FALSE;
            break;
        }
    }
    g_hash_table_destroy(map);
    return same;
}

void editor_tagbar_states_free(EditorTagbarStates* st) {
    if (st == NULL) return;
    g_ptr_array_unref(st->panes);
    g_array_unref(st->wanted);
    g_free(st);
}
