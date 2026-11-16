/* Browser tabs: web pages in the preview notebook or the editor area (see browser.h) */
#include "preview/browser.h"
#include "core/app_events.h"
#include "editor/editor.h"
#include "widgets/focus_route.h"
#include "theme/icons.h"
#include "core/i18n.h"
#include "theme/theme.h"
#include "preview/web_address.h"
#include "preview/web_engine.h"

#define PAGE_KEY "lookin-browser"

/* The default application for uri (the system browser for web pages) */
static void launch_external(AppData* app, const gchar* uri) {
    GtkUriLauncher* launcher = gtk_uri_launcher_new(uri);
    gtk_uri_launcher_launch(launcher, GTK_WINDOW(app->window), NULL, NULL, NULL);
    g_object_unref(launcher);
}

gboolean browser_available(void) {
    return web_engine_available();
}

typedef struct {
    AppData* app;
    GtkWidget* page;  /* the notebook page */
    GtkWidget* view; /* web_engine.h */
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
                  ".browser-page .browser-bar { padding: 3px 8px; background-color: @bg_bar;"
                  " border-bottom: 1px solid @border; font-size: @size_bar; color: @fg; }"
                  ".browser-page .browser-bar button { padding: 0 6px; border: none; min-height: 23px; }"
                  ".browser-page .browser-bar entry { min-height: 22px; padding-top: 0; padding-bottom: 0;"
                  " font-size: @size_normal; }");
}

static BrowserTab* tab_of(GtkWidget* page) {
    return page != NULL ? g_object_get_data(G_OBJECT(page), PAGE_KEY) : NULL;
}

/* The notebook holding page, or NULL */
static GtkNotebook* notebook_of(GtkWidget* page) {
    GtkWidget* parent = gtk_widget_get_parent(page);
    GtkWidget* nb = parent != NULL ? gtk_widget_get_ancestor(parent, GTK_TYPE_NOTEBOOK) : NULL;
    return (nb != NULL && gtk_notebook_page_num(GTK_NOTEBOOK(nb), page) >= 0) ? GTK_NOTEBOOK(nb) : NULL;
}

/* The tab holding the keyboard focus, or NULL */
static BrowserTab* focused_tab(AppData* app) {
    GtkWidget* w = app->window != NULL ? gtk_window_get_focus(app->window) : NULL;
    for (; w != NULL; w = gtk_widget_get_parent(w)) {
        BrowserTab* bt = tab_of(w);
        if (bt != NULL) return bt;
    }
    return NULL;
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
    gtk_widget_set_sensitive(bt->back_btn, web_engine_can_go_back(bt->view));
    gtk_widget_set_sensitive(bt->forward_btn, web_engine_can_go_forward(bt->view));
    gboolean loading = web_engine_is_loading(bt->view);
    if (loading != bt->loading) {
        bt->loading = loading;
        icon_button_set_content(bt->reload_btn, loading ? "process-stop" : "view-refresh", NULL);
        gtk_widget_set_tooltip_text(bt->reload_btn, loading ? _("Stop (Escape)") : _("Reload (F5)"));
    }
    if (!loading) gtk_entry_set_progress_fraction(GTK_ENTRY(bt->entry), 0.0);
}

/* The address bar shows the page's URI, unless it is being edited */
static void show_address(BrowserTab* bt) {
    const gchar* uri = web_engine_uri(bt->view);
    if (uri == NULL || g_strcmp0(uri, "about:blank") == 0) uri = "";
    gtk_editable_set_text(GTK_EDITABLE(bt->entry), uri);
}

static void update_title(BrowserTab* bt) {
    const gchar* title = web_engine_title(bt->view);
    const gchar* uri = web_engine_uri(bt->view);
    const gchar* text = title != NULL && title[0] != '\0' ? title
                        : uri != NULL && uri[0] != '\0' && g_strcmp0(uri, "about:blank") != 0 ? uri
                        : _("New Tab");
    gtk_label_set_text(GTK_LABEL(bt->title), text);
    gtk_widget_set_tooltip_text(bt->label, text);
}

static void on_progress(BrowserTab* bt) {
    gdouble p = web_engine_progress(bt->view);
    gtk_entry_set_progress_fraction(GTK_ENTRY(bt->entry), bt->loading && p < 1.0 ? p : 0.0);
}

static void on_changed(gpointer data, WebEngineChange what) {
    BrowserTab* bt = data;
    if ((what & WEB_ENGINE_URI) && !focus_within(bt->entry)) show_address(bt);
    if (what & (WEB_ENGINE_URI | WEB_ENGINE_TITLE)) update_title(bt);
    if (what & WEB_ENGINE_LOADING) update_buttons(bt);
    if (what & WEB_ENGINE_PROGRESS) on_progress(bt);
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
    GtkNotebook* nb = notebook_of(bt->page);
    if (nb == NULL) return;
    if (GTK_WIDGET(nb) == app->notebook) show_preview_panel(app);
    gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, bt->page));
    if (focus_page) {
        gtk_widget_grab_focus(bt->view);
    } else {
        gtk_widget_grab_focus(bt->entry);
    }
}

/* Put the page in nb (NULL: the preview notebook) at position (-1: last);
 * it moves like an editor tab (drag, Ctrl+Alt+Arrow, its menu) */
static void insert_page(AppData* app, BrowserTab* bt, GtkNotebook* nb, gint position) {
    if (nb == NULL) nb = GTK_NOTEBOOK(app->notebook);
    gtk_notebook_insert_page(nb, bt->page, bt->label, position);
    gtk_notebook_set_tab_reorderable(nb, bt->page, TRUE);
    gtk_notebook_set_tab_detachable(nb, bt->page, TRUE);
    app_emit(app, APP_EVENT_CHROME_CHANGED); /* the split button shows with a tab */
}

/* Take the page out of the notebook: an owned ref */
static GtkWidget* detach_page(GtkWidget* page) {
    GtkRoot* root = gtk_widget_get_root(page);
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    if (focus != NULL && gtk_widget_is_ancestor(focus, page)) gtk_root_set_focus(root, NULL);
    g_object_ref(page);
    GtkNotebook* nb = notebook_of(page);
    if (nb != NULL) gtk_notebook_remove_page(nb, gtk_notebook_page_num(nb, page));
    return page;
}

/* Take the tab out of its notebook (the page and the tab are freed), or of
 * the hidden workspace keeping it */
static void close_tab(BrowserTab* bt) {
    AppData* app = bt->app;
    GtkWidget* closed = g_object_ref(bt->page);
    GtkNotebook* nb = notebook_of(closed);
    if (nb != NULL && app->window != NULL && gtk_widget_get_root(closed) == GTK_ROOT(app->window)) {
        GtkWidget* focus_back = focus_route_return_target(app, closed);
        editor_remove_page(app, closed); /* an editor notebook left empty goes */
        focus_route_return(app, focus_back, closed);
    } else if (nb != NULL) {
        /* In the editor panel of a hidden workspace: a notebook left empty
         * goes when the workspace shows again */
        gtk_notebook_remove_page(nb, gtk_notebook_page_num(nb, closed));
    } else {
        for (gint i = 0; i < app->num_workspaces; i++) {
            GPtrArray* kept = app->workspaces[i].browser_pages;
            if (kept != NULL) g_ptr_array_remove(kept, closed);
        }
    }
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

static void on_view_close(gpointer data) {
    BrowserTab* bt = data;
    g_idle_add(close_idle, g_object_ref(bt->page));
}

static BrowserTab* tab_new(AppData* app, GtkWidget* related);

/* A link or script opens a new window: a new tab after this one, shown
 * once the engine is ready */
static GtkWidget* on_create(gpointer data, GtkWidget* view, const gchar* uri) {
    (void)uri;
    BrowserTab* bt = data;
    AppData* app = bt->app;
    BrowserTab* nt = tab_new(app, view);
    GtkNotebook* nb = notebook_of(bt->page);
    gint at = nb != NULL ? gtk_notebook_page_num(nb, bt->page) : -1;
    insert_page(app, nt, nb, at >= 0 ? at + 1 : -1);
    return nt->view;
}

static void on_ready_to_show(gpointer data) {
    show_tab(data, TRUE);
}

/* An address the tab does not show: its application */
static void on_external(gpointer data, const gchar* uri, gboolean not_shown) {
    BrowserTab* bt = data;
    if (not_shown) {
        gchar* msg = g_strdup_printf(_("Not shown in the tab, opened with the default application: %s"), uri);
        app_notice(bt->app, msg, FALSE);
        g_free(msg);
    }
    launch_external(bt->app, uri);
}

static void on_notice(gpointer data, const gchar* message) {
    app_notice(((BrowserTab*)data)->app, message, FALSE);
}

static const WebEngineCallbacks engine_callbacks = {
    on_changed, on_create, on_ready_to_show, on_view_close, on_external, on_notice,
};

typedef struct {
    AppData* app;
    gchar* uri;
    gchar* title;
} BookmarkRequest;

/* After the context menu is gone: the bookmark dialog is modal */
static gboolean bookmark_idle(gpointer data) {
    BookmarkRequest* req = data;
    AppData* app = req->app;
    g_free(app->bookmark_add_target);
    g_free(app->bookmark_add_name);
    app->bookmark_add_target = req->uri;
    app->bookmark_add_name = req->title;
    g_free(req);
    app_emit(app, APP_EVENT_ADD_BOOKMARK);
    return G_SOURCE_REMOVE;
}

static void go_to_address(BrowserTab* bt) {
    gchar* uri = web_address_to_uri(gtk_editable_get_text(GTK_EDITABLE(bt->entry)));
    if (uri == NULL) return;
    web_engine_load(bt->view, uri);
    g_free(uri);
    gtk_widget_grab_focus(bt->view);
}

static void on_entry_activate(GtkEntry* entry, gpointer data) {
    (void)entry;
    go_to_address(data);
}

static void reload_or_stop(BrowserTab* bt) {
    if (web_engine_is_loading(bt->view)) {
        web_engine_stop(bt->view);
    } else {
        web_engine_reload(bt->view);
    }
}

static void on_back_clicked(GtkButton* button, gpointer data) {
    (void)button;
    web_engine_go_back(((BrowserTab*)data)->view);
}

static void on_forward_clicked(GtkButton* button, gpointer data) {
    (void)button;
    web_engine_go_forward(((BrowserTab*)data)->view);
}

static void on_reload_clicked(GtkButton* button, gpointer data) {
    (void)button;
    reload_or_stop(data);
}

static void on_label_middle_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    close_tab(data);
}

static void on_menu_close(GtkWidget* child, gpointer data) {
    (void)data;
    BrowserTab* bt = tab_of(child);
    if (bt != NULL) close_tab(bt);
}

/* Add Bookmark… of the tab menu: the page's address, named after its title */
static void on_menu_bookmark(GtkWidget* child, gpointer data) {
    (void)data;
    BrowserTab* bt = tab_of(child);
    if (bt == NULL) return;
    const gchar* uri = web_engine_uri(bt->view);
    if (uri == NULL || uri[0] == '\0') {
        app_notice(bt->app, _("This tab has no page to bookmark"), FALSE);
        return;
    }
    BookmarkRequest* req = g_new0(BookmarkRequest, 1);
    req->app = bt->app;
    req->uri = g_strdup(uri);
    req->title = g_strdup(web_engine_title(bt->view));
    g_idle_add(bookmark_idle, req);
}

/* The tab menu, as for an editor tab: add a bookmark, move, close */
static void on_label_right_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    BrowserTab* bt = data;
    editor_page_show_context_menu_extra(bt->app, bt->page, bt->label, on_menu_close, NULL,
                                        _("Add Bookmark…"), on_menu_bookmark);
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
        web_engine_go_back(bt->view);
    } else {
        web_engine_go_forward(bt->view);
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
        web_engine_reload(bt->view);
        return TRUE;
    }
    if (mods == 0 && keyval == GDK_KEY_Escape) {
        if (focus_within(bt->entry)) {
            show_address(bt);
            gtk_widget_grab_focus(bt->view);
            return TRUE;
        }
        if (web_engine_is_loading(bt->view)) {
            web_engine_stop(bt->view);
            return TRUE;
        }
        return FALSE;
    }
    if ((mods & ~GDK_SHIFT_MASK) == GDK_CONTROL_MASK &&
        (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab)) {
        gint step = (keyval == GDK_KEY_ISO_Left_Tab || (mods & GDK_SHIFT_MASK)) ? -1 : 1;
        GtkNotebook* nb = notebook_of(bt->page);
        if (nb != NULL) focus_route_step_tab(app, nb, step, TRUE);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK && (keyval == GDK_KEY_Page_Down || keyval == GDK_KEY_Page_Up)) {
        GtkNotebook* nb = notebook_of(bt->page);
        if (nb != NULL) focus_route_step_tab(app, nb, keyval == GDK_KEY_Page_Down ? 1 : -1, TRUE);
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
static BrowserTab* tab_new(AppData* app, GtkWidget* related) {
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
    gtk_box_append(GTK_BOX(page), bar);

    GtkWidget* view = web_engine_new(related, &engine_callbacks, bt);
    bt->view = view;
    gtk_widget_set_hexpand(view, TRUE);
    gtk_widget_set_vexpand(view, TRUE);
    focus_route_mark_focus(view);
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
    editor_page_set_movable(page);

    bt->label = g_object_ref(icon_label_new("globe", _("New Tab")));
    bt->title = g_object_ref(gtk_widget_get_last_child(bt->label));
    gtk_label_set_ellipsize(GTK_LABEL(bt->title), PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(GTK_LABEL(bt->title), 12); /* minimum width when tabs are crowded */
    gtk_label_set_max_width_chars(GTK_LABEL(bt->title), 24);
    GtkGesture* mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(mclick, "released", G_CALLBACK(on_label_middle_click), bt);
    gtk_widget_add_controller(bt->label, GTK_EVENT_CONTROLLER(mclick));
    GtkGesture* rclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclick), GDK_BUTTON_SECONDARY);
    g_signal_connect(rclick, "pressed", G_CALLBACK(on_label_right_click), bt);
    gtk_widget_add_controller(bt->label, GTK_EVENT_CONTROLLER(rclick));

    update_buttons(bt);
    return bt;
}

/* No engine: an address goes to the default browser, an empty tab says why */
static void open_without_engine(AppData* app, const gchar* uri) {
    if (uri != NULL) {
        launch_external(app, uri);
        return;
    }
#ifdef G_OS_WIN32
    app_notice(app, _("Browser tabs need the Microsoft Edge WebView2 Runtime and WebView2Loader.dll next to the program"), FALSE);
#else
    app_notice(app, _("Browser tabs need WebKitGTK 6.0 (install libwebkitgtk-6.0-dev and rebuild)"), FALSE);
#endif
}

void browser_open(AppData* app, const gchar* uri) {
    if (app == NULL) return;
    if (!web_engine_available()) {
        open_without_engine(app, uri);
        return;
    }
    if (app->notebook == NULL) return;
    BrowserTab* bt = tab_new(app, NULL);
    insert_page(app, bt, NULL, -1);
    if (uri != NULL) {
        web_engine_load(bt->view, uri);
        gtk_editable_set_text(GTK_EDITABLE(bt->entry), uri);
    }
    show_tab(bt, uri != NULL);
}

void browser_new_tab_in(AppData* app, GtkNotebook* nb) {
    if (app == NULL) return;
    if (!web_engine_available()) {
        open_without_engine(app, NULL);
        return;
    }
    if (app->notebook == NULL) return;
    BrowserTab* bt = tab_new(app, NULL);
    insert_page(app, bt, nb, -1);
    show_tab(bt, FALSE);
}

const gchar* browser_page_uri(GtkWidget* page) {
    BrowserTab* bt = tab_of(page);
    const gchar* uri = bt != NULL ? web_engine_uri(bt->view) : NULL;
    return (uri != NULL && uri[0] != '\0' && g_strcmp0(uri, "about:blank") != 0) ? uri : NULL;
}

GtkWidget* browser_open_in(AppData* app, const gchar* uri, GtkNotebook* nb) {
    if (app == NULL || app->notebook == NULL || uri == NULL || nb == NULL || !web_engine_available()) return NULL;
    BrowserTab* bt = tab_new(app, NULL);
    insert_page(app, bt, nb, -1);
    web_engine_load(bt->view, uri);
    gtk_editable_set_text(GTK_EDITABLE(bt->entry), uri);
    return bt->page;
}

gboolean browser_close_shown(AppData* app) {
    if (app == NULL) return FALSE;
    BrowserTab* bt = focused_tab(app);
    if (bt == NULL && app->notebook != NULL) {
        /* Else the current page of the preview notebook */
        GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
        gint cur = gtk_notebook_get_current_page(nb);
        GtkWidget* page = cur >= 0 ? gtk_notebook_get_nth_page(nb, cur) : NULL;
        bt = tab_of(page);
        if (bt != NULL && !gtk_widget_get_mapped(page)) bt = NULL;
    }
    if (bt == NULL) return FALSE;
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
        g_ptr_array_add(ws->browser_pages, detach_page(g_ptr_array_index(pages, i)));
    }
    g_ptr_array_unref(pages);
}

void browser_workspace_shown(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || ws->browser_pages == NULL) return;
    GPtrArray* pages = ws->browser_pages;
    ws->browser_pages = NULL;
    for (guint i = 0; i < pages->len; i++) {
        BrowserTab* bt = tab_of(g_ptr_array_index(pages, i));
        if (bt != NULL && app->notebook != NULL) insert_page(app, bt, NULL, -1);
    }
    g_ptr_array_unref(pages);
}

void browser_workspace_removed(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces && ws == &app->workspaces[app->current_ws]) {
        GPtrArray* pages = notebook_pages(app);
        for (guint i = 0; i < pages->len; i++) {
            g_object_unref(detach_page(g_ptr_array_index(pages, i)));
        }
        g_ptr_array_unref(pages);
    }
    g_clear_pointer(&ws->browser_pages, g_ptr_array_unref);
}
