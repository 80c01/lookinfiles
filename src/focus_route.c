/* Keyboard focus routing: which panel or page owns the focus, tab switching,
 * global mode keys in list pages. */
#include "focus_route.h"
#include "app_events.h"
#include "panels.h"

#include <string.h>

#define NAV_MARK "lookin-nav-keys"
#define EDITOR_TABS_GROUP "editor-tabs"

void focus_route_mark_nav(GtkWidget* widget) {
    if (widget != NULL) {
        g_object_set_data(G_OBJECT(widget), NAV_MARK, GINT_TO_POINTER(1));
    }
}

EditorTab* focus_route_page_tab(AppData* app, GtkWidget* page) {
    if (app == NULL || page == NULL) return NULL;
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = (EditorTab*)l->data;
        if (t != NULL && t->container == page) return t;
    }
    return NULL;
}

static void classify_page(AppData* app, FocusInfo* info, GtkNotebook* nb, GtkWidget* page) {
    info->notebook = nb;
    info->page = page;
    if (page == NULL) return;
    info->tab = focus_route_page_tab(app, page);
    if (info->tab != NULL) {
        info->area = FOCUS_AREA_EDITOR;
    } else if (app->text_view != NULL && gtk_widget_is_ancestor(app->text_view, page)) {
        info->area = FOCUS_AREA_PREVIEW;
    } else {
        info->area = FOCUS_AREA_PAGE;
    }
}

/* page is a page of a notebook (GTK puts pages in an internal box) */
static GtkNotebook* notebook_of_page(GtkWidget* page) {
    GtkWidget* parent = gtk_widget_get_parent(page);
    GtkWidget* nb = parent != NULL ? gtk_widget_get_ancestor(parent, GTK_TYPE_NOTEBOOK) : NULL;
    if (nb != NULL && gtk_notebook_page_num(GTK_NOTEBOOK(nb), page) >= 0) {
        return GTK_NOTEBOOK(nb);
    }
    return NULL;
}

static GtkWidget* current_page(GtkNotebook* nb) {
    gint cur = gtk_notebook_get_current_page(nb);
    return cur >= 0 ? gtk_notebook_get_nth_page(nb, cur) : NULL;
}

void focus_route_get(AppData* app, FocusInfo* info) {
    memset(info, 0, sizeof(*info));
    if (app == NULL || app->window == NULL) return;
    GtkWidget* focus = gtk_window_get_focus(GTK_WINDOW(app->window));
    info->focus = focus;
    for (GtkWidget* w = focus; w != NULL; w = gtk_widget_get_parent(w)) {
        if (g_object_get_data(G_OBJECT(w), NAV_MARK) != NULL) {
            info->nav_keys = TRUE;
        }
        if (app->tree_view != NULL && w == GTK_WIDGET(app->tree_view)) {
            info->area = FOCUS_AREA_EXPLORER;
            return;
        }
        if (GTK_IS_NOTEBOOK(w)) {
            /* Tab bar (the notebook itself or a tab label): its current page */
            classify_page(app, info, GTK_NOTEBOOK(w), current_page(GTK_NOTEBOOK(w)));
            info->nav_keys = FALSE;
            return;
        }
        GtkNotebook* nb = notebook_of_page(w);
        if (nb != NULL) {
            classify_page(app, info, nb, w);
            return;
        }
    }
    info->nav_keys = FALSE; /* a marked widget outside the panels above */
}

/* First widget marked with focus_route_mark_nav() under w */
static GtkWidget* find_marked(GtkWidget* w) {
    if (g_object_get_data(G_OBJECT(w), NAV_MARK) != NULL) return w;
    if (GTK_IS_LIST_VIEW(w) || GTK_IS_COLUMN_VIEW(w) || GTK_IS_TEXT_VIEW(w)) return NULL;
    for (GtkWidget* c = gtk_widget_get_first_child(w); c != NULL; c = gtk_widget_get_next_sibling(c)) {
        GtkWidget* found = find_marked(c);
        if (found != NULL) return found;
    }
    return NULL;
}

void focus_route_focus_page(AppData* app, GtkWidget* page) {
    if (app == NULL || page == NULL) return;
    EditorTab* tab = focus_route_page_tab(app, page);
    GtkWidget* target = NULL;
    if (tab != NULL) {
        target = tab->view;
    } else if (app->text_view != NULL && gtk_widget_is_ancestor(app->text_view, page)) {
        target = app->text_view;
    } else {
        target = find_marked(page);
    }
    if (target == NULL || !gtk_widget_grab_focus(target)) {
        gtk_widget_child_focus(page, GTK_DIR_TAB_FORWARD);
    }
}

gboolean focus_route_step_tab(AppData* app, GtkNotebook* nb, gint step, gboolean focus) {
    if (nb == NULL) return FALSE;
    gint n = gtk_notebook_get_n_pages(nb);
    gint cur = gtk_notebook_get_current_page(nb);
    if (n <= 1 || cur < 0) return FALSE;
    gint dir = step < 0 ? -1 : 1;
    gint p = cur;
    /* Hidden pages (the Git tab outside a repository) are skipped */
    for (gint i = 1; i < n; i++) {
        p = ((p + dir) % n + n) % n;
        GtkWidget* page = gtk_notebook_get_nth_page(nb, p);
        if (page != NULL && gtk_widget_get_visible(page)) {
            gtk_notebook_set_current_page(nb, p);
            if (focus) focus_route_focus_page(app, page);
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean is_tab_notebook(AppData* app, GtkWidget* w) {
    return w != NULL && GTK_IS_NOTEBOOK(w) &&
           (w == app->notebook ||
            g_strcmp0(gtk_notebook_get_group_name(GTK_NOTEBOOK(w)), EDITOR_TABS_GROUP) == 0);
}

/* After a click on a tab, GTK leaves the focus on the notebook: give it to
 * the page, where the keys of its mode work */
static gboolean redirect_idle(gpointer data) {
    AppData* app = (AppData*)data;
    GtkWidget* focus = app->window != NULL ? gtk_window_get_focus(GTK_WINDOW(app->window)) : NULL;
    if (is_tab_notebook(app, focus)) {
        focus_route_focus_page(app, current_page(GTK_NOTEBOOK(focus)));
    }
    return G_SOURCE_REMOVE;
}

void focus_route_focus_changed(AppData* app) {
    if (app == NULL) return;
    app->nav_prefix = NAV_PREFIX_NONE;
    app->nav_recenter_step = 0;
    GtkWidget* focus = app->window != NULL ? gtk_window_get_focus(GTK_WINDOW(app->window)) : NULL;
    if (is_tab_notebook(app, focus)) {
        g_idle_add(redirect_idle, app);
    }
}

/* --- Global mode keys in list pages */

typedef struct {
    AppData* app;
    GtkSingleSelection* selection;
} ListNav;

static void list_select(GtkWidget* list, ListNav* ln, guint pos) {
    gtk_single_selection_set_selected(ln->selection, pos);
    GtkListScrollFlags flags = GTK_LIST_SCROLL_FOCUS | GTK_LIST_SCROLL_SELECT;
    if (GTK_IS_LIST_VIEW(list)) {
        gtk_list_view_scroll_to(GTK_LIST_VIEW(list), pos, flags, NULL);
    } else if (GTK_IS_COLUMN_VIEW(list)) {
        gtk_column_view_scroll_to(GTK_COLUMN_VIEW(list), pos, NULL, flags, NULL);
    }
}

/* Rows shown at once, from the scroll adjustment */
static guint list_page_rows(GtkWidget* list, guint n) {
    GtkAdjustment* adj = GTK_IS_SCROLLABLE(list) ? gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(list)) : NULL;
    gdouble upper = adj != NULL ? gtk_adjustment_get_upper(adj) : 0.0;
    if (upper <= 0.0 || n == 0) return 10;
    guint rows = (guint)(gtk_adjustment_get_page_size(adj) * n / upper);
    return rows > 1 ? rows - 1 : 1; /* one row of the page stays in view */
}

static gboolean on_list_key(GtkEventControllerKey* controller, guint keyval, guint keycode,
                            GdkModifierType state, gpointer data) {
    (void)keycode;
    ListNav* ln = (ListNav*)data;
    AppData* app = ln->app;
    GtkWidget* list = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
    NavPrefix next = NAV_PREFIX_NONE;
    NavAction action = nav_keys_lookup(app_nav_style(app), (NavPrefix)app->nav_prefix, keyval, state, &next);
    app->nav_prefix = next;

    guint n = g_list_model_get_n_items(G_LIST_MODEL(ln->selection));
    guint cur = gtk_single_selection_get_selected(ln->selection);
    gboolean none = cur == GTK_INVALID_LIST_POSITION || cur >= n;
    switch (action) {
    case NAV_NONE:
    case NAV_FORWARD:
    case NAV_BACKWARD:
        return FALSE; /* Enter activates, Left / Right are the list's */
    case NAV_DOWN:
        if (n > 0) list_select(list, ln, none ? 0 : MIN(cur + 1, n - 1));
        return TRUE;
    case NAV_UP:
        if (n > 0) list_select(list, ln, none || cur == 0 ? 0 : cur - 1);
        return TRUE;
    case NAV_FIRST:
        if (n > 0) list_select(list, ln, 0);
        return TRUE;
    case NAV_LAST:
        if (n > 0) list_select(list, ln, n - 1);
        return TRUE;
    case NAV_PAGE_DOWN:
        if (n > 0) list_select(list, ln, none ? 0 : MIN(cur + list_page_rows(list, n), n - 1));
        return TRUE;
    case NAV_PAGE_UP: {
        guint rows = list_page_rows(list, n);
        if (n > 0) list_select(list, ln, none || cur < rows ? 0 : cur - rows);
        return TRUE;
    }
    case NAV_PANEL_NEXT:
        app_emit(app, APP_EVENT_FOCUS_NEXT_PANEL);
        return TRUE;
    case NAV_PANEL_PREV:
        app_emit(app, APP_EVENT_FOCUS_PREV_PANEL);
        return TRUE;
    case NAV_TAB_NEXT:
    case NAV_TAB_PREV: {
        GtkWidget* nb = gtk_widget_get_ancestor(list, GTK_TYPE_NOTEBOOK);
        if (nb != NULL) focus_route_step_tab(app, GTK_NOTEBOOK(nb), action == NAV_TAB_NEXT ? 1 : -1, TRUE);
        return TRUE;
    }
    default:
        return TRUE; /* prefixes, and keys a list has no use for (search, scrolling) */
    }
}

static void list_nav_free(gpointer data, GClosure* closure) {
    (void)closure;
    g_free(data);
}

void focus_route_attach_list(AppData* app, GtkWidget* list, GtkSingleSelection* selection) {
    if (app == NULL || list == NULL || selection == NULL) return;
    ListNav* ln = g_new0(ListNav, 1);
    ln->app = app;
    ln->selection = selection;
    focus_route_mark_nav(list);
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect_data(keys, "key-pressed", G_CALLBACK(on_list_key), ln,
                          list_nav_free, 0);
    gtk_widget_add_controller(list, keys);
}
