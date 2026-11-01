/* The Help tab: the user guide in a page of the preview notebook (see help_guide.h) */
#include "help_guide.h"
#include "app_events.h"
#include "focus_route.h"
#include "help.h"
#include "icons.h"
#include "markdown.h"
#include "nav_keys.h"
#include "panels.h"
#include "text_style.h"
#include "theme.h"
#include "user_guide.h"

#define PAGE_KEY "lookin-help-guide"

typedef struct {
    AppData* app;
    GtkWidget* page;    /* the notebook page (app->help_page) */
    GtkWidget* title;   /* header label: the title of the page shown */
    GtkWidget* view;    /* the rendered page */
    gchar* current;     /* user_guide page name shown */
} HelpGuide;

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".help-guide { background-color: @bg_view; }"
                  ".help-guide .help-guide-header { padding: 4px 10px; color: @fg; }");
}

static HelpGuide* guide_state(AppData* app) {
    return app != NULL && app->help_page != NULL ? g_object_get_data(G_OBJECT(app->help_page), PAGE_KEY) : NULL;
}

/* Render page in the view and show its title */
static void load_page(HelpGuide* hg, const gchar* page) {
    gchar* text = user_guide_read(page);
    if (text == NULL) {
        text = g_strdup_printf("# Page Not Found\n\nThe user guide has no page `%s`.\n", page);
    }
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(hg->view));
    ensure_markdown_tags(buffer);
    render_markdown_to_buffer(buffer, GTK_TEXT_VIEW(hg->view), text, NULL, FALSE, NULL, 0);
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_place_cursor(buffer, &start);
    gtk_adjustment_set_value(gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(hg->view)), 0.0);

    gchar* title = user_guide_title(text);
    gtk_label_set_text(GTK_LABEL(hg->title), title != NULL ? title : "User Guide");
    g_free(title);
    g_free(text);
    g_free(hg->current);
    hg->current = g_strdup(page);
}

/* Scroll the view by lines (negative: up) */
static void scroll_lines(HelpGuide* hg, gint lines) {
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(hg->view));
    GtkTextIter first;
    gtk_text_buffer_get_start_iter(gtk_text_view_get_buffer(GTK_TEXT_VIEW(hg->view)), &first);
    GdkRectangle rect;
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(hg->view), &first, &rect);
    gdouble line_h = rect.height > 0 ? rect.height : 14.0;
    gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) + lines * line_h);
}

static void scroll_pages(HelpGuide* hg, gint pages) {
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(hg->view));
    gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) + pages * gtk_adjustment_get_page_size(adj));
}

static void scroll_end(HelpGuide* hg, gboolean top) {
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(hg->view));
    gtk_adjustment_set_value(adj, top ? gtk_adjustment_get_lower(adj) : gtk_adjustment_get_upper(adj));
}

static void close_page(AppData* app) {
    if (app->help_page == NULL || app->notebook == NULL) return;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    GtkWidget* closed = app->help_page;
    GtkWidget* focus_back = focus_route_return_target(app, closed);
    g_object_ref(closed);
    gint num = gtk_notebook_page_num(nb, closed);
    app->help_page = NULL;
    if (num >= 0) gtk_notebook_remove_page(nb, num); /* frees the state */
    focus_route_return(app, focus_back, closed);
    g_object_unref(closed);
    app_emit(app, APP_EVENT_CHROME_CHANGED); /* the Help button */
}

/* Navigation keys of the global mode scroll the page; ? shows the cheat sheet */
static gboolean on_key(GtkEventControllerKey* controller, guint keyval, guint keycode,
                       GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    HelpGuide* hg = data;
    AppData* app = hg->app;
    NavPrefix next = NAV_PREFIX_NONE;
    NavAction action = nav_keys_lookup(app_nav_style(app), (NavPrefix)app->nav_prefix, keyval, state, &next);
    app->nav_prefix = next;
    switch (action) {
    case NAV_NONE:
        break;
    case NAV_FORWARD:
    case NAV_BACKWARD:
        return FALSE;
    case NAV_DOWN: scroll_lines(hg, 1); return TRUE;
    case NAV_UP: scroll_lines(hg, -1); return TRUE;
    case NAV_PAGE_DOWN: scroll_pages(hg, 1); return TRUE;
    case NAV_PAGE_UP: scroll_pages(hg, -1); return TRUE;
    case NAV_FIRST: scroll_end(hg, TRUE); return TRUE;
    case NAV_LAST: scroll_end(hg, FALSE); return TRUE;
    case NAV_PANEL_NEXT: app_emit(app, APP_EVENT_FOCUS_NEXT_PANEL); return TRUE;
    case NAV_PANEL_PREV: app_emit(app, APP_EVENT_FOCUS_PREV_PANEL); return TRUE;
    case NAV_TAB_CLOSE: app_emit(app, APP_EVENT_CLOSE_TAB); return TRUE;
    case NAV_TAB_NEXT:
    case NAV_TAB_PREV:
        focus_route_step_tab(app, GTK_NOTEBOOK(app->notebook), action == NAV_TAB_NEXT ? 1 : -1, TRUE);
        return TRUE;
    default:
        return TRUE; /* prefixes, and keys the page has no use for */
    }

    if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) == 0) {
        if (keyval == GDK_KEY_question) {
            toggle_help_panel(app);
            return TRUE;
        }
        if (keyval == GDK_KEY_Escape && app->help_window != NULL && gtk_widget_get_visible(app->help_window)) {
            toggle_help_panel(app);
            return TRUE;
        }
    }
    return FALSE;
}

static void on_close_clicked(GtkButton* button, gpointer data) {
    (void)button;
    close_page(((HelpGuide*)data)->app);
}

static void on_label_middle_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    close_page(((HelpGuide*)data)->app);
}

static void on_shortcuts_clicked(GtkButton* button, gpointer data) {
    (void)button;
    toggle_help_panel(((HelpGuide*)data)->app);
}

static void on_contents_clicked(GtkButton* button, gpointer data) {
    (void)button;
    HelpGuide* hg = data;
    load_page(hg, USER_GUIDE_HOME);
    gtk_widget_grab_focus(hg->view);
}

static void guide_free(gpointer data) {
    HelpGuide* hg = data;
    g_free(hg->current);
    g_free(hg);
}

/* The Help page, made when missing: after the Git tab, the result pages and
 * the Canvas page, before the editor tabs */
static HelpGuide* ensure_page(AppData* app) {
    HelpGuide* hg = guide_state(app);
    if (hg != NULL || app->notebook == NULL) return hg;
    ensure_css();
    hg = g_new0(HelpGuide, 1);
    hg->app = app;

    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(page, "help-guide");
    gtk_widget_set_hexpand(page, TRUE);
    gtk_widget_set_vexpand(page, TRUE);

    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(bar, "help-guide-header");
    hg->title = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(hg->title), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(hg->title), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(hg->title, TRUE);
    gtk_box_append(GTK_BOX(bar), hg->title);
    GtkWidget* contents = icon_button_new("view-paged", "Contents of the User Guide");
    g_signal_connect(contents, "clicked", G_CALLBACK(on_contents_clicked), hg);
    gtk_box_append(GTK_BOX(bar), contents);
    GtkWidget* shortcuts = icon_button_new("dialog-question", "Show Keyboard Shortcuts (?)");
    g_signal_connect(shortcuts, "clicked", G_CALLBACK(on_shortcuts_clicked), hg);
    gtk_box_append(GTK_BOX(bar), shortcuts);
    GtkWidget* close = icon_button_new("window-close", "Close Help");
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), hg);
    gtk_box_append(GTK_BOX(bar), close);
    gtk_box_append(GTK_BOX(page), bar);

    hg->view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(hg->view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(hg->view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(hg->view), GTK_WRAP_WORD_CHAR);
    set_view_monospace(hg->view, FALSE);
    set_view_text_padding(hg->view);
    apply_viewer_background(hg->view);
    focus_route_mark_nav(hg->view);
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), hg);
    gtk_widget_add_controller(hg->view, keys);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), hg->view);
    gtk_box_append(GTK_BOX(page), scroll);

    hg->page = page;
    g_object_set_data_full(G_OBJECT(page), PAGE_KEY, hg, guide_free);
    app->help_page = page;

    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gint after = app->git_tab_box != NULL ? gtk_notebook_page_num(nb, app->git_tab_box) : 0;
    if (app->refs_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->refs_page));
    if (app->search_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->search_page));
    if (app->canvas_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->canvas_page));
    GtkWidget* label = icon_label_new("dialog-question", "Help");
    GtkGesture* mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(mclick, "released", G_CALLBACK(on_label_middle_click), hg);
    gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(mclick));
    gtk_notebook_insert_page(nb, page, label, after + 1);
    app_emit(app, APP_EVENT_CHROME_CHANGED); /* the Help button */
    return hg;
}

void help_guide_show(AppData* app, const gchar* page) {
    if (app == NULL) return;
    gboolean opened = guide_state(app) != NULL;
    HelpGuide* hg = ensure_page(app);
    if (hg == NULL) return;
    if (page != NULL || !opened) {
        load_page(hg, page != NULL ? page : USER_GUIDE_HOME);
    }
    /* The preview panel holds the tab: show it when hidden */
    if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
        app_emit(app, APP_EVENT_LAYOUT_CHANGED);
        app->applying_layout = TRUE;
        gtk_widget_set_visible(app->viewer_box, TRUE);
        app_emit(app, APP_EVENT_CHROME_CHANGED);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    }
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, hg->page));
    gtk_widget_grab_focus(hg->view);
}

gboolean help_guide_is_open(AppData* app) {
    return guide_state(app) != NULL;
}

gboolean help_guide_close_shown(AppData* app) {
    if (app == NULL || app->help_page == NULL || app->notebook == NULL) return FALSE;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    if (gtk_notebook_get_current_page(nb) != gtk_notebook_page_num(nb, app->help_page)) return FALSE;
    if (!gtk_widget_get_mapped(app->help_page)) return FALSE;
    close_page(app);
    return TRUE;
}
