/* Browser tabs: web pages in the preview notebook (see browser.h) */
#include "browser.h"
#include "core/app_events.h"
#include "focus_route.h"
#include "icons.h"
#include "core/i18n.h"
#include "theme.h"
#include "web_address.h"

#ifdef HAVE_WEBKIT
#include <webkit/webkit.h>
#endif

#define PAGE_KEY "lookin-browser"

/* The default application for uri (the system browser for web pages) */
static void launch_external(AppData* app, const gchar* uri) {
    GtkUriLauncher* launcher = gtk_uri_launcher_new(uri);
    gtk_uri_launcher_launch(launcher, GTK_WINDOW(app->window), NULL, NULL, NULL);
    g_object_unref(launcher);
}

gboolean browser_available(void) {
#ifdef HAVE_WEBKIT
    return TRUE;
#else
    return FALSE;
#endif
}

#ifdef HAVE_WEBKIT

typedef struct {
    AppData* app;
    GtkWidget* page;  /* the notebook page */
    WebKitWebView* view;
    /* Widgets used by the view's signals: referenced, since the view can
     * still notify while the page is being destroyed */
    GtkWidget* back_btn;
    GtkWidget* forward_btn;
    GtkWidget* reload_btn;
    GtkWidget* entry; /* address bar */
    GtkWidget* label; /* tab label (also kept while the page is detached) */
    GtkWidget* title; /* the label's text */
    gboolean loading; /* reload_btn shows Stop */
} BrowserTab;

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".browser-page { background-color: @bg_view; }"
                  ".browser-page .browser-bar { padding: 4px 10px; color: @fg; }");
}

static BrowserTab* tab_of(GtkWidget* page) {
    return page != NULL ? g_object_get_data(G_OBJECT(page), PAGE_KEY) : NULL;
}

static void tab_free(gpointer data) {
    BrowserTab* bt = data;
    g_clear_object(&bt->back_btn);
    g_clear_object(&bt->forward_btn);
    g_clear_object(&bt->reload_btn);
    g_clear_object(&bt->entry);
    g_clear_object(&bt->title);
    g_clear_object(&bt->label);
    g_free(bt);
}

/* The browser pages of the preview notebook, in order */
static GPtrArray* notebook_pages(AppData* app) {
    GPtrArray* pages = g_ptr_array_new();
    if (app->notebook == NULL) return pages;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gint n = gtk_notebook_get_n_pages(nb);
    for (gint i = 0; i < n; i++) {
        GtkWidget* page = gtk_notebook_get_nth_page(nb, i);
        if (tab_of(page) != NULL) g_ptr_array_add(pages, page);
    }
    return pages;
}

static gboolean focus_within(GtkWidget* w) {
    return (gtk_widget_get_state_flags(w) & GTK_STATE_FLAG_FOCUS_WITHIN) != 0;
}

static void update_buttons(BrowserTab* bt) {
    gtk_widget_set_sensitive(bt->back_btn, webkit_web_view_can_go_back(bt->view));
    gtk_widget_set_sensitive(bt->forward_btn, webkit_web_view_can_go_forward(bt->view));
    gboolean loading = webkit_web_view_is_loading(bt->view);
    if (loading != bt->loading) {
        bt->loading = loading;
        icon_button_set_content(bt->reload_btn, loading ? "process-stop" : "view-refresh", NULL);
        gtk_widget_set_tooltip_text(bt->reload_btn, loading ? _("Stop (Escape)") : _("Reload (F5)"));
    }
    if (!loading) gtk_entry_set_progress_fraction(GTK_ENTRY(bt->entry), 0.0);
}

/* The address bar shows the page's URI, unless it is being edited */
static void show_address(BrowserTab* bt) {
    const gchar* uri = webkit_web_view_get_uri(bt->view);
    if (uri == NULL || g_strcmp0(uri, "about:blank") == 0) uri = "";
    gtk_editable_set_text(GTK_EDITABLE(bt->entry), uri);
}

static void update_title(BrowserTab* bt) {
    const gchar* title = webkit_web_view_get_title(bt->view);
    const gchar* uri = webkit_web_view_get_uri(bt->view);
    const gchar* text = title != NULL && title[0] != '\0' ? title
                        : uri != NULL && uri[0] != '\0' && g_strcmp0(uri, "about:blank") != 0 ? uri
                        : _("New Tab");
    gtk_label_set_text(GTK_LABEL(bt->title), text);
    gtk_widget_set_tooltip_text(bt->label, text);
}

static void on_title(GObject* obj, GParamSpec* pspec, gpointer data) {
    (void)obj;
    (void)pspec;
    update_title(data);
}

static void on_uri(GObject* obj, GParamSpec* pspec, gpointer data) {
    (void)obj;
    (void)pspec;
    BrowserTab* bt = data;
    if (!focus_within(bt->entry)) show_address(bt);
    update_title(bt);
}

static void on_progress(GObject* obj, GParamSpec* pspec, gpointer data) {
    (void)obj;
    (void)pspec;
    BrowserTab* bt = data;
    gdouble p = webkit_web_view_get_estimated_load_progress(bt->view);
    gtk_entry_set_progress_fraction(GTK_ENTRY(bt->entry), bt->loading && p < 1.0 ? p : 0.0);
}

static void on_loading(GObject* obj, GParamSpec* pspec, gpointer data) {
    (void)obj;
    (void)pspec;
    update_buttons(data);
}

static void on_history_changed(WebKitBackForwardList* list, WebKitBackForwardListItem* added,
                               gpointer removed, gpointer data) {
    (void)list;
    (void)added;
    (void)removed;
    update_buttons(data);
}

/* The preview panel holds the tabs: show it when hidden */
static void show_preview_panel(AppData* app) {
    if (app->viewer_box == NULL || gtk_widget_get_visible(app->viewer_box)) return;
    app_layout_begin(app);
    gtk_widget_set_visible(app->viewer_box, TRUE);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
}

static void show_tab(BrowserTab* bt, gboolean focus_page) {
    AppData* app = bt->app;
    show_preview_panel(app);
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, bt->page));
    if (focus_page) {
        gtk_widget_grab_focus(GTK_WIDGET(bt->view));
    } else {
        gtk_widget_grab_focus(bt->entry);
    }
}

/* Put the page in the notebook at position (-1: last) */
static void insert_page(AppData* app, BrowserTab* bt, gint position) {
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gtk_notebook_insert_page(nb, bt->page, bt->label, position);
    gtk_notebook_set_tab_reorderable(nb, bt->page, TRUE);
}

/* Take the page out of the notebook: an owned ref */
static GtkWidget* detach_page(AppData* app, GtkWidget* page) {
    GtkRoot* root = gtk_widget_get_root(page);
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    if (focus != NULL && gtk_widget_is_ancestor(focus, page)) gtk_root_set_focus(root, NULL);
    g_object_ref(page);
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gint n = gtk_notebook_page_num(nb, page);
    if (n >= 0) gtk_notebook_remove_page(nb, n);
    return page;
}

static void close_tab(BrowserTab* bt) {
    AppData* app = bt->app;
    if (app->notebook == NULL) return;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    GtkWidget* closed = bt->page;
    gint num = gtk_notebook_page_num(nb, closed);
    if (num < 0) return;
    GtkWidget* focus_back = focus_route_return_target(app, closed);
    g_object_ref(closed);
    gtk_notebook_remove_page(nb, num); /* frees the tab with the page */
    focus_route_return(app, focus_back, closed);
    g_object_unref(closed);
}

/* window.close() of the page: closed once its signal is over */
static gboolean close_idle(gpointer data) {
    GtkWidget* page = data;
    BrowserTab* bt = tab_of(page);
    if (bt != NULL) close_tab(bt);
    g_object_unref(page);
    return G_SOURCE_REMOVE;
}

static void on_view_close(WebKitWebView* view, gpointer data) {
    (void)view;
    BrowserTab* bt = data;
    g_idle_add(close_idle, g_object_ref(bt->page));
}

static BrowserTab* tab_new(AppData* app, WebKitWebView* related);

/* A link or script opens a new window: a new tab after this one, shown
 * once WebKit is ready */
static GtkWidget* on_create(WebKitWebView* view, WebKitNavigationAction* action, gpointer data) {
    (void)action;
    BrowserTab* bt = data;
    AppData* app = bt->app;
    BrowserTab* nt = tab_new(app, view);
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gint at = gtk_notebook_page_num(nb, bt->page);
    insert_page(app, nt, at >= 0 ? at + 1 : -1);
    return GTK_WIDGET(nt->view);
}

static void on_ready_to_show(WebKitWebView* view, gpointer data) {
    (void)view;
    show_tab(data, TRUE);
}

/* Schemes a tab shows itself; others (mailto:, apt:...) go to their application */
static gboolean scheme_shown(const gchar* uri) {
    const gchar* scheme = uri != NULL ? g_uri_peek_scheme(uri) : NULL;
    if (scheme == NULL) return TRUE;
    static const gchar* const shown[] = {"http", "https", "file", "about", "data", "blob", NULL};
    for (gsize i = 0; shown[i] != NULL; i++) {
        if (g_strcmp0(scheme, shown[i]) == 0) return TRUE;
    }
    return FALSE;
}

static gboolean on_decide_policy(WebKitWebView* view, WebKitPolicyDecision* decision,
                                 WebKitPolicyDecisionType type, gpointer data) {
    (void)view;
    BrowserTab* bt = data;
    if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
        WebKitNavigationAction* nav =
            webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
        const gchar* uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(nav));
        if (scheme_shown(uri)) return FALSE;
        webkit_policy_decision_ignore(decision);
        launch_external(bt->app, uri);
        return TRUE;
    }
    if (type != WEBKIT_POLICY_DECISION_TYPE_RESPONSE) return FALSE;
    /* A file the page cannot show (archive, package...): the default application */
    WebKitResponsePolicyDecision* rd = WEBKIT_RESPONSE_POLICY_DECISION(decision);
    if (webkit_response_policy_decision_is_mime_type_supported(rd) ||
        !webkit_response_policy_decision_is_main_frame_main_resource(rd)) {
        return FALSE;
    }
    gchar* uri = g_strdup(webkit_uri_request_get_uri(webkit_response_policy_decision_get_request(rd)));
    webkit_policy_decision_ignore(decision);
    gchar* msg = g_strdup_printf(_("Not shown in the tab, opened with the default application: %s"), uri);
    app_notice(bt->app, msg, FALSE);
    g_free(msg);
    launch_external(bt->app, uri);
    g_free(uri);
    return TRUE;
}

/* Camera, microphone, location, notifications...: denied */
static gboolean on_permission(WebKitWebView* view, WebKitPermissionRequest* request, gpointer data) {
    (void)view;
    (void)data;
    webkit_permission_request_deny(request);
    return TRUE;
}

/* Upload a file: would open a file chooser window */
static gboolean on_file_chooser(WebKitWebView* view, WebKitFileChooserRequest* request, gpointer data) {
    (void)view;
    webkit_file_chooser_request_cancel(request);
    app_notice(((BrowserTab*)data)->app, _("Choosing a file to upload is not supported in browser tabs"), FALSE);
    return TRUE;
}

/* Printing would open a print dialog window */
static gboolean on_print(WebKitWebView* view, WebKitPrintOperation* op, gpointer data) {
    (void)view;
    (void)op;
    app_notice(((BrowserTab*)data)->app, _("Printing is not supported in browser tabs"), FALSE);
    return TRUE;
}

/* Fullscreen video and the like: the window stays as it is */
static gboolean on_fullscreen(WebKitWebView* view, gpointer data) {
    (void)view;
    (void)data;
    return TRUE;
}

static void go_to_address(BrowserTab* bt) {
    gchar* uri = web_address_to_uri(gtk_editable_get_text(GTK_EDITABLE(bt->entry)));
    if (uri == NULL) return;
    webkit_web_view_load_uri(bt->view, uri);
    g_free(uri);
    gtk_widget_grab_focus(GTK_WIDGET(bt->view));
}

static void on_entry_activate(GtkEntry* entry, gpointer data) {
    (void)entry;
    go_to_address(data);
}

static void reload_or_stop(BrowserTab* bt) {
    if (webkit_web_view_is_loading(bt->view)) {
        webkit_web_view_stop_loading(bt->view);
    } else {
        webkit_web_view_reload(bt->view);
    }
}

static void on_back_clicked(GtkButton* button, gpointer data) {
    (void)button;
    webkit_web_view_go_back(((BrowserTab*)data)->view);
}

static void on_forward_clicked(GtkButton* button, gpointer data) {
    (void)button;
    webkit_web_view_go_forward(((BrowserTab*)data)->view);
}

static void on_reload_clicked(GtkButton* button, gpointer data) {
    (void)button;
    reload_or_stop(data);
}

static void on_close_clicked(GtkButton* button, gpointer data) {
    (void)button;
    close_tab(data);
}

static void on_label_middle_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    close_tab(data);
}

/* The mouse's back / forward buttons */
static void on_side_button(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    BrowserTab* bt = data;
    guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
    if (button != 8 && button != 9) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    if (button == 8) {
        webkit_web_view_go_back(bt->view);
    } else {
        webkit_web_view_go_forward(bt->view);
    }
}

/* Keys of the tab, before the page gets them: Ctrl+L, F5, Escape, and the
 * tab / panel keys that have a modifier or are function keys (the page keeps
 * every key it could type) */
static gboolean on_key(GtkEventControllerKey* controller, guint keyval, guint keycode,
                       GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    BrowserTab* bt = data;
    AppData* app = bt->app;
    GdkModifierType mods = state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_ALT_MASK);
    if (mods == GDK_CONTROL_MASK && (keyval == GDK_KEY_l || keyval == GDK_KEY_L)) {
        gtk_widget_grab_focus(bt->entry);
        gtk_editable_select_region(GTK_EDITABLE(bt->entry), 0, -1);
        return TRUE;
    }
    if (mods == 0 && keyval == GDK_KEY_F5) {
        webkit_web_view_reload(bt->view);
        return TRUE;
    }
    if (mods == 0 && keyval == GDK_KEY_Escape) {
        if (focus_within(bt->entry)) {
            show_address(bt);
            gtk_widget_grab_focus(GTK_WIDGET(bt->view));
            return TRUE;
        }
        if (webkit_web_view_is_loading(bt->view)) {
            webkit_web_view_stop_loading(bt->view);
            return TRUE;
        }
        return FALSE;
    }
    if ((mods & ~GDK_SHIFT_MASK) == GDK_CONTROL_MASK &&
        (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab)) {
        gint step = (keyval == GDK_KEY_ISO_Left_Tab || (mods & GDK_SHIFT_MASK)) ? -1 : 1;
        focus_route_step_tab(app, GTK_NOTEBOOK(app->notebook), step, TRUE);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && (keyval == GDK_KEY_Page_Down || keyval == GDK_KEY_Page_Up)) {
        focus_route_step_tab(app, GTK_NOTEBOOK(app->notebook), keyval == GDK_KEY_Page_Down ? 1 : -1, TRUE);
        return TRUE;
    }
    if ((mods & ~GDK_SHIFT_MASK) == 0 && keyval == GDK_KEY_F6) {
        app_emit(app, (mods & GDK_SHIFT_MASK) ? APP_EVENT_FOCUS_PREV_PANEL : APP_EVENT_FOCUS_NEXT_PANEL);
        return TRUE;
    }
    return FALSE;
}

static GtkWidget* bar_button(GtkWidget* bar, const gchar* icon, const gchar* tooltip, GCallback cb, BrowserTab* bt) {
    GtkWidget* btn = icon_button_new(icon, tooltip);
    g_signal_connect(btn, "clicked", cb, bt);
    gtk_box_append(GTK_BOX(bar), btn);
    return btn;
}

/* A new tab, not yet in the notebook; related: the page that opened it */
static BrowserTab* tab_new(AppData* app, WebKitWebView* related) {
    ensure_css();
    BrowserTab* bt = g_new0(BrowserTab, 1);
    bt->app = app;

    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(page, "browser-page");
    gtk_widget_set_hexpand(page, TRUE);
    gtk_widget_set_vexpand(page, TRUE);

    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(bar, "browser-bar");
    bt->back_btn = g_object_ref(bar_button(bar, "go-previous", _("Back"), G_CALLBACK(on_back_clicked), bt));
    bt->forward_btn = g_object_ref(bar_button(bar, "go-next", _("Forward"), G_CALLBACK(on_forward_clicked), bt));
    bt->reload_btn = g_object_ref(bar_button(bar, "view-refresh", _("Reload (F5)"), G_CALLBACK(on_reload_clicked), bt));
    bt->entry = g_object_ref(gtk_entry_new());
    gtk_entry_set_placeholder_text(GTK_ENTRY(bt->entry), _("Address, file path or search"));
    gtk_entry_set_input_purpose(GTK_ENTRY(bt->entry), GTK_INPUT_PURPOSE_URL);
    gtk_widget_set_tooltip_text(bt->entry, _("Address or words to search (Ctrl+L)"));
    gtk_widget_set_hexpand(bt->entry, TRUE);
    g_signal_connect(bt->entry, "activate", G_CALLBACK(on_entry_activate), bt);
    gtk_box_append(GTK_BOX(bar), bt->entry);
    bar_button(bar, "window-close", _("Close Tab (Ctrl+W)"), G_CALLBACK(on_close_clicked), bt);
    gtk_box_append(GTK_BOX(page), bar);

    GtkWidget* view = related != NULL ? g_object_new(WEBKIT_TYPE_WEB_VIEW, "related-view", related, NULL)
                                      : webkit_web_view_new();
    bt->view = WEBKIT_WEB_VIEW(view);
    gtk_widget_set_hexpand(view, TRUE);
    gtk_widget_set_vexpand(view, TRUE);
    focus_route_mark_focus(view);
    g_signal_connect(view, "notify::title", G_CALLBACK(on_title), bt);
    g_signal_connect(view, "notify::uri", G_CALLBACK(on_uri), bt);
    g_signal_connect(view, "notify::estimated-load-progress", G_CALLBACK(on_progress), bt);
    g_signal_connect(view, "notify::is-loading", G_CALLBACK(on_loading), bt);
    g_signal_connect(webkit_web_view_get_back_forward_list(bt->view), "changed",
                     G_CALLBACK(on_history_changed), bt);
    g_signal_connect(view, "create", G_CALLBACK(on_create), bt);
    g_signal_connect(view, "ready-to-show", G_CALLBACK(on_ready_to_show), bt);
    g_signal_connect(view, "close", G_CALLBACK(on_view_close), bt);
    g_signal_connect(view, "decide-policy", G_CALLBACK(on_decide_policy), bt);
    g_signal_connect(view, "permission-request", G_CALLBACK(on_permission), bt);
    g_signal_connect(view, "run-file-chooser", G_CALLBACK(on_file_chooser), bt);
    g_signal_connect(view, "print", G_CALLBACK(on_print), bt);
    g_signal_connect(view, "enter-fullscreen", G_CALLBACK(on_fullscreen), bt);
    GtkGesture* side = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(side), 0);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(side), GTK_PHASE_CAPTURE);
    g_signal_connect(side, "pressed", G_CALLBACK(on_side_button), bt);
    gtk_widget_add_controller(view, GTK_EVENT_CONTROLLER(side));
    gtk_box_append(GTK_BOX(page), view);

    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), bt);
    gtk_widget_add_controller(page, keys);

    bt->page = page;
    g_object_set_data_full(G_OBJECT(page), PAGE_KEY, bt, tab_free);

    bt->label = g_object_ref(icon_label_new("network-transmit-receive", _("New Tab")));
    bt->title = g_object_ref(gtk_widget_get_last_child(bt->label));
    gtk_label_set_ellipsize(GTK_LABEL(bt->title), PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(GTK_LABEL(bt->title), 12); /* minimum width when tabs are crowded */
    gtk_label_set_max_width_chars(GTK_LABEL(bt->title), 24);
    GtkGesture* mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(mclick, "released", G_CALLBACK(on_label_middle_click), bt);
    gtk_widget_add_controller(bt->label, GTK_EVENT_CONTROLLER(mclick));

    update_buttons(bt);
    return bt;
}

void browser_open(AppData* app, const gchar* uri) {
    if (app == NULL || app->notebook == NULL) return;
    BrowserTab* bt = tab_new(app, NULL);
    insert_page(app, bt, -1);
    if (uri != NULL) {
        webkit_web_view_load_uri(bt->view, uri);
        gtk_editable_set_text(GTK_EDITABLE(bt->entry), uri);
    }
    show_tab(bt, uri != NULL);
}

gboolean browser_close_shown(AppData* app) {
    if (app == NULL || app->notebook == NULL) return FALSE;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gint cur = gtk_notebook_get_current_page(nb);
    GtkWidget* page = cur >= 0 ? gtk_notebook_get_nth_page(nb, cur) : NULL;
    BrowserTab* bt = tab_of(page);
    if (bt == NULL || !gtk_widget_get_mapped(page)) return FALSE;
    close_tab(bt);
    return TRUE;
}

void browser_workspace_hidden(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    GPtrArray* pages = notebook_pages(app);
    if (pages->len > 0 && ws->browser_pages == NULL) {
        ws->browser_pages = g_ptr_array_new_with_free_func(g_object_unref);
    }
    for (guint i = 0; i < pages->len; i++) {
        g_ptr_array_add(ws->browser_pages, detach_page(app, g_ptr_array_index(pages, i)));
    }
    g_ptr_array_unref(pages);
}

void browser_workspace_shown(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || ws->browser_pages == NULL) return;
    GPtrArray* pages = ws->browser_pages;
    ws->browser_pages = NULL;
    for (guint i = 0; i < pages->len; i++) {
        BrowserTab* bt = tab_of(g_ptr_array_index(pages, i));
        if (bt != NULL && app->notebook != NULL) insert_page(app, bt, -1);
    }
    g_ptr_array_unref(pages);
}

void browser_workspace_removed(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces && ws == &app->workspaces[app->current_ws]) {
        GPtrArray* pages = notebook_pages(app);
        for (guint i = 0; i < pages->len; i++) {
            g_object_unref(detach_page(app, g_ptr_array_index(pages, i)));
        }
        g_ptr_array_unref(pages);
    }
    g_clear_pointer(&ws->browser_pages, g_ptr_array_unref);
}

#else /* !HAVE_WEBKIT */

void browser_open(AppData* app, const gchar* uri) {
    if (app == NULL) return;
    if (uri != NULL) {
        launch_external(app, uri);
    } else {
        app_notice(app, _("Browser tabs need WebKitGTK 6.0 (install libwebkitgtk-6.0-dev and rebuild)"), FALSE);
    }
}

gboolean browser_close_shown(AppData* app) {
    (void)app;
    return FALSE;
}

void browser_workspace_hidden(AppData* app, VirtualLayout* ws) {
    (void)app;
    (void)ws;
}

void browser_workspace_shown(AppData* app, VirtualLayout* ws) {
    (void)app;
    (void)ws;
}

void browser_workspace_removed(AppData* app, VirtualLayout* ws) {
    (void)app;
    (void)ws;
}

#endif /* HAVE_WEBKIT */
