/* The tagbar of a Jupyter notebook tab (see ipynb_tab_private.h): the
 * headings of its Markdown cells (ipynb_outline.h) beside the cells, in the
 * widget of the editor tagbar (outline_pane.h) and with its width. It
 * follows the current cell, is read again after a pause in the changes, and
 * a click or Enter scrolls to the heading's cell and focuses it. */
#include "core/i18n.h"
#include "jupyter/ipynb_outline.h"
#include "jupyter/ipynb_tab_private.h"
#include "widgets/focus_route.h"
#include "widgets/outline_pane.h"
#include "widgets/panels.h"

#define REFRESH_DELAY_MS 600 /* pause in the changes before the headings are read again */
#define CURSOR_DELAY_MS 80   /* moves to another cell are followed after this pause */

struct IpynbTagbar {
    IpynbTab* tab;      /* NULL once released */
    OutlinePane* pane;
    GPtrArray* entries; /* the headings shown, or NULL */
    guint refresh_source;
    guint cursor_source;
    guint scroll_source;
    guint scroll_key;   /* ipynb_doc_cell_key() of the cell scrolled to */
    gboolean stale;     /* the notebook changed while the tagbar was hidden */
};

static void tagbar_free(IpynbTagbar* tb) {
    g_clear_handle_id(&tb->refresh_source, g_source_remove);
    g_clear_handle_id(&tb->cursor_source, g_source_remove);
    g_clear_handle_id(&tb->scroll_source, g_source_remove);
    g_clear_pointer(&tb->entries, g_ptr_array_unref);
    g_free(tb);
}

/* --- Pane callbacks --- */

/* The cell gone to at the top of the view, once laid out: the viewport
 * has scrolled it into view when it got the focus (only just in view, and
 * maybe still animating), which setting the value ends */
static gboolean on_scroll_idle(gpointer data) {
    IpynbTagbar* tb = data;
    tb->scroll_source = 0;
    IpynbTab* t = tb->tab;
    gint index = t != NULL ? ipynb_doc_find_cell(t->doc, tb->scroll_key) : -1;
    if (index < 0 || (guint)index >= t->views->len) return G_SOURCE_REMOVE;
    GtkWidget* cell = gtk_widget_get_parent(g_ptr_array_index(t->views, (guint)index));
    graphene_point_t top;
    if (cell != NULL && gtk_widget_compute_point(cell, t->cells_box, &GRAPHENE_POINT_INIT(0, 0), &top)) {
        GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(t->scroll));
        gtk_adjustment_set_value(adj, top.y);
    }
    return G_SOURCE_REMOVE;
}

/* Focus the heading's cell, then scroll it to the top of the view */
static void go_to(gpointer data, const CodeOutlineEntry* entry) {
    IpynbTagbar* tb = data;
    IpynbTab* t = tb->tab;
    if (t == NULL || entry->line < 0 || (guint)entry->line >= t->views->len) return;
    ipynb_tab_focus_cell(t, (guint)entry->line);
    tb->scroll_key = ipynb_doc_cell_key(t->doc, (guint)entry->line);
    g_clear_handle_id(&tb->scroll_source, g_source_remove);
    tb->scroll_source = g_idle_add(on_scroll_idle, tb);
}

static gint cursor_line(gpointer data) {
    IpynbTagbar* tb = data;
    return tb->tab != NULL ? (gint)tb->tab->current : -1;
}

static void escape(gpointer data) {
    IpynbTagbar* tb = data;
    if (tb->tab != NULL) ipynb_tab_focus_cell(tb->tab, tb->tab->current);
}

static void close_tagbar(gpointer data) {
    IpynbTagbar* tb = data;
    if (tb->tab != NULL) ipynb_tagbar_toggle(tb->tab);
}

/* The cells have no editing mode: the keys of the global mode */
static NavStyle nav_style(gpointer data) {
    IpynbTagbar* tb = data;
    return tb->tab != NULL ? app_nav_style(tb->tab->app) : NAV_STYLE_NOTEPAD;
}

static void panel_action(gpointer data, NavAction action) {
    IpynbTagbar* tb = data;
    if (tb->tab != NULL) focus_route_panel_action(tb->tab->app, outline_pane_list(tb->pane), action);
}

static const OutlinePaneCallbacks callbacks = {go_to, cursor_line, escape, close_tagbar, nav_style, panel_action};

/* --- Headings and cursor --- */

static void refresh_now(IpynbTagbar* tb) {
    g_clear_handle_id(&tb->refresh_source, g_source_remove);
    if (tb->tab == NULL) return;
    tb->stale = FALSE;
    GPtrArray* entries = ipynb_outline_headings(tb->tab->doc);
    /* Outputs coming in change the notebook, not its headings */
    if (tb->entries != NULL && ipynb_outline_same(entries, tb->entries)) {
        g_ptr_array_unref(entries);
        return;
    }
    g_clear_pointer(&tb->entries, g_ptr_array_unref);
    tb->entries = entries;
    outline_pane_set_entries(tb->pane, entries, TRUE, NULL);
}

static gboolean on_refresh_timeout(gpointer data) {
    IpynbTagbar* tb = data;
    tb->refresh_source = 0;
    refresh_now(tb);
    return G_SOURCE_REMOVE;
}

static gboolean on_cursor_timeout(gpointer data) {
    IpynbTagbar* tb = data;
    tb->cursor_source = 0;
    if (tb->tab != NULL && !outline_pane_has_focus(tb->pane)) outline_pane_follow_cursor(tb->pane);
    return G_SOURCE_REMOVE;
}

void ipynb_tagbar_changed(IpynbTab* t) {
    IpynbTagbar* tb = t->tagbar;
    if (tb == NULL) return;
    if (!outline_pane_shown(tb->pane)) {
        tb->stale = TRUE;
        return;
    }
    g_clear_handle_id(&tb->refresh_source, g_source_remove);
    tb->refresh_source = g_timeout_add(REFRESH_DELAY_MS, on_refresh_timeout, tb);
}

void ipynb_tagbar_cursor_moved(IpynbTab* t) {
    IpynbTagbar* tb = t->tagbar;
    if (tb == NULL || !outline_pane_shown(tb->pane) || tb->cursor_source != 0) return;
    tb->cursor_source = g_timeout_add(CURSOR_DELAY_MS, on_cursor_timeout, tb);
}

/* --- Public --- */

GtkWidget* ipynb_tagbar_wrap(IpynbTab* t, GtkWidget* content) {
    IpynbTagbar* tb = g_new0(IpynbTagbar, 1);
    tb->tab = t;
    t->tagbar = tb;
    tb->pane = outline_pane_new(content, &t->app->editor_tagbar_width, _("Reading the headings…"), &callbacks, tb);
    GtkWidget* paned = outline_pane_widget(tb->pane);
    g_object_set_data_full(G_OBJECT(paned), "ipynb-tagbar", tb, (GDestroyNotify)tagbar_free);
    tb->stale = TRUE;
    return paned;
}

void ipynb_tagbar_release(IpynbTab* t) {
    IpynbTagbar* tb = t->tagbar;
    if (tb == NULL) return;
    g_clear_handle_id(&tb->refresh_source, g_source_remove);
    g_clear_handle_id(&tb->cursor_source, g_source_remove);
    g_clear_handle_id(&tb->scroll_source, g_source_remove);
    outline_pane_detach(tb->pane);
    tb->tab = NULL;
    t->tagbar = NULL;
}

void ipynb_tagbar_set_shown(IpynbTab* t, gboolean shown) {
    IpynbTagbar* tb = t->tagbar;
    if (tb == NULL || shown == outline_pane_shown(tb->pane)) return;
    outline_pane_set_shown(tb->pane, shown);
    if (shown && (tb->entries == NULL || tb->stale)) refresh_now(tb);
    if (shown) outline_pane_follow_cursor(tb->pane);
}

gboolean ipynb_tagbar_shown(IpynbTab* t) {
    return t->tagbar != NULL && outline_pane_shown(t->tagbar->pane);
}

void ipynb_tagbar_toggle(IpynbTab* t) {
    if (t->tagbar == NULL) return;
    gboolean show = !outline_pane_shown(t->tagbar->pane);
    ipynb_tagbar_set_shown(t, show);
    if (show) {
        outline_pane_focus(t->tagbar->pane);
    } else {
        ipynb_tab_focus_cell(t, t->current);
    }
}

GtkWidget* ipynb_tagbar_list(IpynbTab* t) {
    return ipynb_tagbar_shown(t) ? outline_pane_list(t->tagbar->pane) : NULL;
}

gboolean ipynb_tagbar_has_focus(IpynbTab* t) {
    if (!ipynb_tagbar_shown(t)) return FALSE;
    GtkWidget* panel = gtk_paned_get_end_child(GTK_PANED(outline_pane_widget(t->tagbar->pane)));
    GtkRoot* root = panel != NULL ? gtk_widget_get_root(panel) : NULL;
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    return focus != NULL && gtk_widget_is_ancestor(focus, panel);
}
