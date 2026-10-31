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

/* --- Focus history: the panes focused last, newest first */

#define HISTORY_LEN 8
#define HISTORY_KEY "lookin-focus-history"
#define HOLD_KEY "lookin-focus-hold"

typedef struct {
    GWeakRef refs[HISTORY_LEN];
} FocusHistory;

static void history_free(gpointer data) {
    FocusHistory* h = data;
    for (gint i = 0; i < HISTORY_LEN; i++) g_weak_ref_clear(&h->refs[i]);
    g_free(h);
}

static FocusHistory* history(AppData* app) {
    if (app->window == NULL) return NULL;
    FocusHistory* h = g_object_get_data(G_OBJECT(app->window), HISTORY_KEY);
    if (h == NULL) {
        h = g_new0(FocusHistory, 1);
        for (gint i = 0; i < HISTORY_LEN; i++) g_weak_ref_init(&h->refs[i], NULL);
        g_object_set_data_full(G_OBJECT(app->window), HISTORY_KEY, h, history_free);
    }
    return h;
}

/* The pane a focused widget stands for: the explorer list, a notebook page
 * (its tab bar too), else the widget itself (terminal pane...) */
static GtkWidget* pane_of(AppData* app, GtkWidget* focus) {
    for (GtkWidget* w = focus; w != NULL; w = gtk_widget_get_parent(w)) {
        if (app->tree_view != NULL && w == GTK_WIDGET(app->tree_view)) return w;
        if (GTK_IS_NOTEBOOK(w)) return current_page(GTK_NOTEBOOK(w));
        if (notebook_of_page(w) != NULL) return w;
    }
    return focus;
}

static void history_push(AppData* app, GtkWidget* pane) {
    FocusHistory* h = history(app);
    if (h == NULL || pane == NULL) return;
    GtkWidget* old[HISTORY_LEN];
    for (gint i = 0; i < HISTORY_LEN; i++) old[i] = g_weak_ref_get(&h->refs[i]);
    gint n = 0;
    g_weak_ref_set(&h->refs[n++], pane);
    for (gint i = 0; i < HISTORY_LEN; i++) {
        if (old[i] != NULL && old[i] != pane && n < HISTORY_LEN) {
            g_weak_ref_set(&h->refs[n++], old[i]);
        }
    }
    while (n < HISTORY_LEN) g_weak_ref_set(&h->refs[n++], NULL);
    for (gint i = 0; i < HISTORY_LEN; i++) {
        if (old[i] != NULL) g_object_unref(old[i]);
    }
}

/* The pane is still in the window and shown (a page: its notebook is) */
static gboolean pane_usable(AppData* app, GtkWidget* pane) {
    if (pane == NULL || gtk_widget_get_root(pane) != GTK_ROOT(app->window)) return FALSE;
    GtkNotebook* nb = notebook_of_page(pane);
    if (nb != NULL) {
        return gtk_widget_get_visible(pane) && gtk_widget_get_mapped(GTK_WIDGET(nb));
    }
    return gtk_widget_get_mapped(pane);
}

static gboolean focus_pane(AppData* app, GtkWidget* pane) {
    if (!pane_usable(app, pane)) return FALSE;
    GtkNotebook* nb = notebook_of_page(pane);
    if (nb != NULL) {
        gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, pane));
        focus_route_focus_page(app, pane);
        return TRUE;
    }
    return gtk_widget_grab_focus(pane);
}

/* Focus the most recent pane still shown that is not leaving (nor inside it) */
static gboolean focus_recent(AppData* app, GtkWidget* leaving) {
    FocusHistory* h = history(app);
    if (h == NULL) return FALSE;
    gboolean done = FALSE;
    for (gint i = 0; i < HISTORY_LEN && !done; i++) {
        GtkWidget* pane = g_weak_ref_get(&h->refs[i]);
        if (pane == NULL) continue;
        if (leaving == NULL || (pane != leaving && !gtk_widget_is_ancestor(pane, leaving))) {
            done = focus_pane(app, pane);
        }
        g_object_unref(pane);
    }
    return done;
}

GtkWidget* focus_route_return_target(AppData* app, GtkWidget* page) {
    if (app == NULL || app->window == NULL || page == NULL) return NULL;
    FocusInfo info;
    focus_route_get(app, &info);
    gboolean inside = info.page == page ||
                      (info.focus != NULL && gtk_widget_is_ancestor(info.focus, page));
    if (!inside) return NULL;
    FocusHistory* h = history(app);
    if (h == NULL) return NULL;
    for (gint i = 0; i < HISTORY_LEN; i++) {
        GtkWidget* pane = g_weak_ref_get(&h->refs[i]);
        if (pane == NULL) continue;
        if (pane != page && !gtk_widget_is_ancestor(pane, page)) return pane;
        g_object_unref(pane);
    }
    return g_object_ref(page); /* no earlier pane: any pane still shown */
}

void focus_route_return(AppData* app, GtkWidget* target, GtkWidget* closed) {
    if (target == NULL) return;
    if (app != NULL && app->window != NULL && (target == closed || !focus_pane(app, target))) {
        focus_recent(app, closed);
    }
    g_object_unref(target);
}

void focus_route_hold_history(AppData* app, gboolean hold) {
    if (app == NULL || app->window == NULL) return;
    gint n = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(app->window), HOLD_KEY));
    n = hold ? n + 1 : MAX(n - 1, 0);
    g_object_set_data(G_OBJECT(app->window), HOLD_KEY, GINT_TO_POINTER(n));
}

static gboolean history_held(AppData* app) {
    return app->window != NULL &&
           GPOINTER_TO_INT(g_object_get_data(G_OBJECT(app->window), HOLD_KEY)) > 0;
}

/* The focused widget went away (its tab closed...): back to the last pane */
static gboolean lost_focus_idle(gpointer data) {
    AppData* app = (AppData*)data;
    if (app->window != NULL && gtk_window_get_focus(GTK_WINDOW(app->window)) == NULL &&
        !app->switching_ws && !app->applying_layout) {
        focus_recent(app, NULL);
    }
    return G_SOURCE_REMOVE;
}

void focus_route_focus_changed(AppData* app) {
    if (app == NULL) return;
    app->nav_prefix = NAV_PREFIX_NONE;
    app->nav_recenter_step = 0;
    GtkWidget* focus = app->window != NULL ? gtk_window_get_focus(GTK_WINDOW(app->window)) : NULL;
    if (focus == NULL) {
        g_idle_add(lost_focus_idle, app);
    } else if (is_tab_notebook(app, focus)) {
        g_idle_add(redirect_idle, app);
    } else if (!history_held(app)) {
        history_push(app, pane_of(app, focus));
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
    case NAV_TAB_CLOSE:
        app_emit(app, APP_EVENT_CLOSE_TAB);
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
