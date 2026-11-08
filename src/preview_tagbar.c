/* The tagbar of the preview (see preview_tagbar.h) */
#include "preview_tagbar.h"
#include "code_outline_tree.h"
#include "nav_keys.h"
#include "outline_pane.h"
#include "i18n.h"
#include "settings_store.h"

#define REFRESH_DELAY_MS 120   /* after the last change of the buffer (a render) */
#define SCROLL_DELAY_MS 80     /* scrolling is followed after this pause */

typedef struct PreviewTagbar {
    AppData* app;
    GtkTextView* view;
    GtkTextBuffer* buffer;
    GtkAdjustment* vadj;       /* or NULL */
    OutlinePane* pane;
    GCancellable* outline_owner; /* preview_tagbar_set_outline(), or NULL */
    GPtrArray* outline;
    guint refresh_source;
    guint scroll_source;
    gint jump_line;            /* line of the entry gone to, -1 */
    GCancellable* jump_owner;  /* the preview it was in (referenced), or NULL */
    gboolean chord_k;          /* Ctrl+K pressed: Ctrl+O comes next */
} PreviewTagbar;

static void tagbar_free(PreviewTagbar* pt) {
    g_signal_handlers_disconnect_by_data(pt->buffer, pt);
    if (pt->vadj != NULL) g_signal_handlers_disconnect_by_data(pt->vadj, pt);
    g_clear_handle_id(&pt->refresh_source, g_source_remove);
    g_clear_handle_id(&pt->scroll_source, g_source_remove);
    g_clear_object(&pt->outline_owner);
    g_clear_object(&pt->jump_owner);
    g_clear_pointer(&pt->outline, g_ptr_array_unref);
    if (pt->app->preview_tagbar_state == pt) pt->app->preview_tagbar_state = NULL;
    g_free(pt);
}

/* ------------------------------------------------------------------------- */
/* Headings of the buffer                                                    */
/* ------------------------------------------------------------------------- */

static void add_heading_tag(GtkTextTag* tag, gpointer data) {
    gchar* name = NULL;
    g_object_get(tag, "name", &name, NULL);
    if (code_outline_heading_tag_level(name) > 0) g_ptr_array_add(data, tag);
    g_free(name);
}

/* One entry per line of each range of the tag */
static void add_ranges(GtkTextBuffer* buffer, GtkTextTag* tag, gint level, GPtrArray* out) {
    GtkTextIter it;
    gtk_text_buffer_get_start_iter(buffer, &it);
    while (TRUE) {
        if (gtk_text_iter_starts_tag(&it, tag)) {
            GtkTextIter end = it;
            gtk_text_iter_forward_to_tag_toggle(&end, tag);
            GtkTextIter line = it;
            while (gtk_text_iter_compare(&line, &end) < 0) {
                GtkTextIter stop = line;
                if (!gtk_text_iter_ends_line(&stop)) gtk_text_iter_forward_to_line_end(&stop);
                if (gtk_text_iter_compare(&stop, &end) > 0) stop = end;
                gchar* text = gtk_text_iter_get_text(&line, &stop);
                CodeOutlineEntry* e = code_outline_heading_new(text, level, gtk_text_iter_get_line(&line));
                g_free(text);
                if (e != NULL) g_ptr_array_add(out, e);
                if (!gtk_text_iter_forward_line(&line)) break;
            }
            it = end;
        }
        if (!gtk_text_iter_forward_to_tag_toggle(&it, tag)) break;
    }
}

static GPtrArray* collect_entries(PreviewTagbar* pt) {
    GtkTextBuffer* buffer = pt->buffer;
    GPtrArray* entries = code_outline_entries_new();
    if (pt->outline != NULL && pt->outline_owner != NULL && !g_cancellable_is_cancelled(pt->outline_owner)) {
        for (guint i = 0; i < pt->outline->len; i++) {
            const CodeOutlineEntry* e = g_ptr_array_index(pt->outline, i);
            CodeOutlineEntry* copy;
            if (e->level > 0) {
                copy = code_outline_heading_new(e->name, e->level, e->line);
                if (copy == NULL) continue;
                copy->column = e->column;
            } else {
                /* A symbol of a code preview (preview_symbols) */
                copy = code_outline_entry_new(e->name, e->detail, e->container, e->kind, e->line, e->column);
                copy->declaration = e->declaration;
            }
            g_ptr_array_add(entries, copy);
        }
    }
    GPtrArray* tags = g_ptr_array_new();
    gtk_text_tag_table_foreach(gtk_text_buffer_get_tag_table(buffer), add_heading_tag, tags);
    for (guint i = 0; i < tags->len; i++) {
        GtkTextTag* tag = g_ptr_array_index(tags, i);
        gchar* name = NULL;
        g_object_get(tag, "name", &name, NULL);
        add_ranges(buffer, tag, code_outline_heading_tag_level(name), entries);
        g_free(name);
    }
    g_ptr_array_unref(tags);
    code_outline_sort(entries);
    return entries;
}

static void refresh_now(PreviewTagbar* pt) {
    g_clear_handle_id(&pt->refresh_source, g_source_remove);
    GPtrArray* entries = collect_entries(pt);
    gboolean symbols = FALSE;
    for (guint i = 0; i < entries->len && !symbols; i++) {
        symbols = ((const CodeOutlineEntry*)g_ptr_array_index(entries, i))->level == 0;
    }
    outline_pane_set_entries(pt->pane, entries, !symbols, symbols ? _("built-in scanner") : NULL);
    outline_pane_set_shown(pt->pane, pt->app->settings.preview_tagbar && entries->len > 0);
    g_ptr_array_unref(entries);
}

static gboolean on_refresh_timeout(gpointer data) {
    PreviewTagbar* pt = data;
    pt->refresh_source = 0;
    refresh_now(pt);
    return G_SOURCE_REMOVE;
}

static void on_buffer_changed(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    PreviewTagbar* pt = data;
    if (!pt->app->settings.preview_tagbar) return; /* read again when turned on */
    g_clear_handle_id(&pt->refresh_source, g_source_remove);
    pt->refresh_source = g_timeout_add(REFRESH_DELAY_MS, on_refresh_timeout, pt);
}

/* ------------------------------------------------------------------------- */
/* Pane callbacks                                                            */
/* ------------------------------------------------------------------------- */

/* The line at the top of the view: the section being read. After a jump,
 * the line gone to while it is on screen and not above the top: near the
 * end of the text it cannot be scrolled to the top. */
static gint top_line(gpointer data) {
    PreviewTagbar* pt = data;
    GdkRectangle rect;
    gtk_text_view_get_visible_rect(pt->view, &rect);
    GtkTextIter it;
    gtk_text_view_get_line_at_y(pt->view, &it, rect.y + 1, NULL);
    gint top = gtk_text_iter_get_line(&it);
    if (pt->jump_owner != pt->app->preview_cancellable) pt->jump_line = -1; /* another preview */
    if (pt->jump_line >= top) {
        gtk_text_view_get_line_at_y(pt->view, &it, rect.y + rect.height - 1, NULL);
        if (pt->jump_line <= gtk_text_iter_get_line(&it)) return pt->jump_line;
    }
    pt->jump_line = -1;
    g_clear_object(&pt->jump_owner);
    return top;
}

static void go_to(gpointer data, const CodeOutlineEntry* entry) {
    PreviewTagbar* pt = data;
    pt->jump_line = entry->line;
    g_set_object(&pt->jump_owner, pt->app->preview_cancellable);
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(pt->view);
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_line(buffer, &it, entry->line);
    gtk_text_buffer_place_cursor(buffer, &it);
    /* Vertical scroll only: centred lines (PDF page badges) would also
     * scroll sideways with gtk_text_view_scroll_to_mark() */
    GdkRectangle rect;
    gtk_text_view_get_iter_location(pt->view, &it, &rect);
    if (pt->vadj != NULL) {
        gdouble top = MIN(rect.y, gtk_adjustment_get_upper(pt->vadj) - gtk_adjustment_get_page_size(pt->vadj));
        gtk_adjustment_set_value(pt->vadj, MAX(top, gtk_adjustment_get_lower(pt->vadj)));
    }
    gtk_widget_grab_focus(GTK_WIDGET(pt->view));
}

static void escape(gpointer data) {
    PreviewTagbar* pt = data;
    gtk_widget_grab_focus(GTK_WIDGET(pt->view));
}

static void save_setting(AppData* app) {
    gchar* path = settings_store_path();
    Settings s;
    settings_store_load(path, &s);
    s.preview_tagbar = app->settings.preview_tagbar;
    GError* error = NULL;
    if (!settings_store_save(path, &s, &error)) {
        g_printerr("[ Tagbar setting not saved: %s ]\n", error->message);
        g_clear_error(&error);
    }
    g_free(path);
}

static void toggle(PreviewTagbar* pt) {
    preview_tagbar_set_shown(pt->app, !pt->app->settings.preview_tagbar);
    if (outline_pane_shown(pt->pane)) {
        outline_pane_focus(pt->pane);
    } else {
        gtk_widget_grab_focus(GTK_WIDGET(pt->view));
    }
    save_setting(pt->app);
}

static void close_tagbar(gpointer data) {
    toggle(data);
}

static const OutlinePaneCallbacks callbacks = {go_to, top_line, escape, close_tagbar};

static gboolean on_scroll_timeout(gpointer data) {
    PreviewTagbar* pt = data;
    pt->scroll_source = 0;
    if (!outline_pane_has_focus(pt->pane)) outline_pane_follow_cursor(pt->pane);
    return G_SOURCE_REMOVE;
}

static void on_scrolled(GtkAdjustment* adj, gpointer data) {
    (void)adj;
    PreviewTagbar* pt = data;
    if (!outline_pane_shown(pt->pane) || pt->scroll_source != 0) return;
    pt->scroll_source = g_timeout_add(SCROLL_DELAY_MS, on_scroll_timeout, pt);
}

/* ------------------------------------------------------------------------- */
/* Public                                                                    */
/* ------------------------------------------------------------------------- */

GtkWidget* preview_tagbar_wrap(AppData* app, GtkWidget* content, GtkWidget* text_view) {
    PreviewTagbar* pt = g_new0(PreviewTagbar, 1);
    pt->app = app;
    pt->view = GTK_TEXT_VIEW(text_view);
    pt->jump_line = -1;
    app->preview_tagbar_state = pt;
    pt->pane = outline_pane_new(content, &app->editor_tagbar_width, _("Reading the headings…"), &callbacks, pt);
    GtkWidget* paned = outline_pane_widget(pt->pane);
    g_object_set_data_full(G_OBJECT(paned), "preview-tagbar", pt, (GDestroyNotify)tagbar_free);
    pt->buffer = gtk_text_view_get_buffer(pt->view);
    g_signal_connect(pt->buffer, "changed", G_CALLBACK(on_buffer_changed), pt);
    pt->vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(text_view));
    if (pt->vadj != NULL) g_signal_connect(pt->vadj, "value-changed", G_CALLBACK(on_scrolled), pt);
    return paned;
}

gboolean preview_tagbar_key(AppData* app, guint keyval, GdkModifierType state) {
    PreviewTagbar* pt = app != NULL ? app->preview_tagbar_state : NULL;
    if (pt == NULL) return FALSE;
    GdkModifierType mods = state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK | GDK_SHIFT_MASK);
    if (pt->chord_k) {
        /* Shift, Control... alone keep the chord waiting */
        if (keyval >= GDK_KEY_Shift_L && keyval <= GDK_KEY_Hyper_R) return FALSE;
        pt->chord_k = FALSE;
        if (mods == GDK_CONTROL_MASK && (keyval == GDK_KEY_o || keyval == GDK_KEY_O)) toggle(pt);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && (keyval == GDK_KEY_k || keyval == GDK_KEY_K)) {
        pt->chord_k = TRUE;
        return TRUE;
    }
    if (mods == 0 && keyval == GDK_KEY_o && app->nav_prefix == NAV_PREFIX_NONE) {
        toggle(pt);
        return TRUE;
    }
    return FALSE;
}

void preview_tagbar_set_outline(AppData* app, GCancellable* owner, GPtrArray* entries) {
    PreviewTagbar* pt = app != NULL ? app->preview_tagbar_state : NULL;
    if (pt == NULL) return;
    g_clear_object(&pt->outline_owner);
    g_clear_pointer(&pt->outline, g_ptr_array_unref);
    if (owner != NULL && entries != NULL) {
        pt->outline_owner = g_object_ref(owner);
        pt->outline = g_ptr_array_ref(entries);
    }
    if (!app->settings.preview_tagbar) return;
    g_clear_handle_id(&pt->refresh_source, g_source_remove);
    pt->refresh_source = g_timeout_add(REFRESH_DELAY_MS, on_refresh_timeout, pt);
}

void preview_tagbar_set_shown(AppData* app, gboolean shown) {
    PreviewTagbar* pt = app != NULL ? app->preview_tagbar_state : NULL;
    if (app == NULL || app->settings.preview_tagbar == shown) return;
    app->settings.preview_tagbar = shown;
    if (pt == NULL) return;
    if (shown) {
        refresh_now(pt); /* the buffer was not followed while off */
    } else {
        outline_pane_set_shown(pt->pane, FALSE);
    }
}

void preview_tagbar_place(AppData* app) {
    PreviewTagbar* pt = app != NULL ? app->preview_tagbar_state : NULL;
    if (pt != NULL) outline_pane_place(pt->pane);
}
