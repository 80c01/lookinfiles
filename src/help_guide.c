/* The Help tab: the user guide in a page of the preview notebook (see help_guide.h) */
#include "help_guide.h"
#include "i18n.h"
#include "app_events.h"
#include "browser.h"
#include "focus_route.h"
#include "games.h"
#include "git_tab.h"
#include "help.h"
#include "icons.h"
#include "markdown.h"
#include "markdown_link.h"
#include "minimap.h"
#include "nav_keys.h"
#include "panels.h"
#include "text_style.h"
#include "theme.h"
#include "user_guide.h"

#define PAGE_KEY "lookin-help-guide"

/* A place in the history: a page and the first character shown */
typedef struct {
    gchar* page;
    gint offset;
} HelpPlace;

typedef struct {
    AppData* app;
    GtkWidget* page;    /* the notebook page (app->help_page) */
    GtkWidget* title;   /* header label: the title of the page shown */
    GtkWidget* back_btn;
    GtkWidget* forward_btn;
    GtkWidget* view;    /* the rendered page */
    gchar* current;     /* user_guide page name shown */
    GPtrArray* back;    /* HelpPlace*, most recent last */
    GPtrArray* forward;
    gint link;          /* link selected with Tab (markdown_link index), -1: none */
    GtkWidget* label;   /* tab label (owned ref: kept while the page is detached) */
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

static void place_free(gpointer p) {
    HelpPlace* place = p;
    g_free(place->page);
    g_free(place);
}

static GtkTextBuffer* guide_buffer(HelpGuide* hg) {
    return gtk_text_view_get_buffer(GTK_TEXT_VIEW(hg->view));
}

/* Scroll so that iter is the first line shown (once the text is laid out) */
static void scroll_to_iter_top(HelpGuide* hg, GtkTextIter* iter) {
    GtkTextBuffer* buffer = guide_buffer(hg);
    GtkTextMark* mark = gtk_text_buffer_get_mark(buffer, "help-target");
    if (mark == NULL) {
        mark = gtk_text_buffer_create_mark(buffer, "help-target", iter, TRUE);
    } else {
        gtk_text_buffer_move_mark(buffer, mark, iter);
    }
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(hg->view), mark, 0.0, TRUE, 0.0, 0.0);
}

/* First character shown at the top of the view */
static gint top_offset(HelpGuide* hg) {
    GdkRectangle rect;
    gtk_text_view_get_visible_rect(GTK_TEXT_VIEW(hg->view), &rect);
    GtkTextIter iter;
    gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(hg->view), &iter, 0, rect.y);
    return gtk_text_iter_get_offset(&iter);
}

/* The rendered heading whose anchor is anchor: TRUE and its start in *iter */
static gboolean find_anchor(HelpGuide* hg, const gchar* anchor, GtkTextIter* iter) {
    GtkTextBuffer* buffer = guide_buffer(hg);
    GtkTextTagTable* table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag* levels[6];
    for (gint i = 0; i < 6; i++) {
        gchar name[8];
        g_snprintf(name, sizeof name, "md_h%d", i + 1);
        levels[i] = gtk_text_tag_table_lookup(table, name);
    }
    GtkTextIter line;
    gtk_text_buffer_get_start_iter(buffer, &line);
    do {
        gboolean heading = FALSE;
        for (gint i = 0; i < 6 && !heading; i++) {
            heading = levels[i] != NULL && gtk_text_iter_has_tag(&line, levels[i]);
        }
        if (!heading) continue;
        GtkTextIter end = line;
        if (!gtk_text_iter_ends_line(&end)) gtk_text_iter_forward_to_line_end(&end);
        gchar* text = gtk_text_iter_get_text(&line, &end);
        gchar* id = user_guide_anchor(g_strstrip(text));
        gboolean found = g_strcmp0(id, anchor) == 0;
        g_free(id);
        g_free(text);
        if (found) {
            *iter = line;
            return TRUE;
        }
    } while (gtk_text_iter_forward_line(&line));
    return FALSE;
}

static void update_buttons(HelpGuide* hg) {
    gtk_widget_set_sensitive(hg->back_btn, hg->back->len > 0);
    gtk_widget_set_sensitive(hg->forward_btn, hg->forward->len > 0);
}

/* Render page in the view and show its title */
static void load_page(HelpGuide* hg, const gchar* page) {
    gchar* text = user_guide_read_localized(page);
    if (text == NULL) {
        text = g_strdup_printf(_("# Page Not Found\n\nThe user guide has no page `%s`.\n"), page);
    }
    GtkTextBuffer* buffer = guide_buffer(hg);
    ensure_markdown_tags(buffer);
    render_markdown_to_buffer(buffer, GTK_TEXT_VIEW(hg->view), text, NULL, FALSE, NULL, 0);
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_place_cursor(buffer, &start);
    hg->link = -1;

    gchar* title = user_guide_title(text);
    gtk_label_set_text(GTK_LABEL(hg->title), title != NULL ? title : _("User Guide"));
    g_free(title);
    g_free(text);
    if (page != hg->current) {
        g_free(hg->current);
        hg->current = g_strdup(page);
    }
}

/* Show page at anchor (NULL: its top, or offset when >= 0); record the place
 * left in the back history when push is TRUE */
static void go_to(HelpGuide* hg, const gchar* page, const gchar* anchor, gint offset, gboolean push) {
    if (push && hg->current != NULL) {
        HelpPlace* place = g_new0(HelpPlace, 1);
        place->page = g_strdup(hg->current);
        place->offset = top_offset(hg);
        g_ptr_array_add(hg->back, place);
        g_ptr_array_set_size(hg->forward, 0);
    }
    if (g_strcmp0(page, hg->current) != 0) {
        load_page(hg, page);
    }
    GtkTextIter iter;
    if (anchor == NULL || !find_anchor(hg, anchor, &iter)) {
        gtk_text_buffer_get_iter_at_offset(guide_buffer(hg), &iter, offset > 0 ? offset : 0);
    }
    scroll_to_iter_top(hg, &iter);
    update_buttons(hg);
}

/* Back (step -1) or forward (1) in the history */
static void step_history(HelpGuide* hg, gint step) {
    GPtrArray* from = step < 0 ? hg->back : hg->forward;
    GPtrArray* to = step < 0 ? hg->forward : hg->back;
    if (from->len == 0) return;
    HelpPlace* here = g_new0(HelpPlace, 1);
    here->page = g_strdup(hg->current);
    here->offset = top_offset(hg);
    g_ptr_array_add(to, here);
    HelpPlace* place = g_ptr_array_steal_index(from, from->len - 1);
    go_to(hg, place->page, NULL, place->offset, FALSE);
    place_free(place);
}

/* A link of the page was clicked or chosen with Enter */
static void on_link(GtkTextView* view, const gchar* url, gpointer data) {
    (void)view;
    HelpGuide* hg = data;
    gchar* page = NULL;
    gchar* anchor = NULL;
    switch (user_guide_resolve_link(hg->current, url, &page, &anchor)) {
    case USER_GUIDE_LINK_PAGE:
        go_to(hg, page, anchor, 0, TRUE);
        break;
    case USER_GUIDE_LINK_WEB:
        browser_open(hg->app, url); /* a browser tab, or the default browser */
        break;
    default: {
        gchar* msg = g_strdup_printf(_("Not in the built-in guide: %s"), page != NULL ? page : url);
        app_notice(hg->app, msg, FALSE);
        g_free(msg);
        break;
    }
    }
    g_free(page);
    g_free(anchor);
}

/* Tab / Shift+Tab: select the next / previous link of the page */
static void select_link(HelpGuide* hg, gint step) {
    GtkTextBuffer* buffer = guide_buffer(hg);
    guint n = markdown_link_count(buffer);
    if (n == 0) return;
    gint i;
    if (hg->link < 0) {
        /* From the top of the view: the first link shown (going forward) */
        GtkTextIter top;
        gtk_text_buffer_get_iter_at_offset(buffer, &top, top_offset(hg));
        i = step > 0 ? 0 : (gint)n - 1;
        for (guint k = 0; k < n; k++) {
            GtkTextIter s;
            markdown_link_get(buffer, k, &s, NULL);
            if (gtk_text_iter_compare(&s, &top) >= 0) {
                i = step > 0 ? (gint)k : ((gint)k > 0 ? (gint)k - 1 : (gint)n - 1);
                break;
            }
        }
    } else {
        i = ((hg->link + step) % (gint)n + (gint)n) % (gint)n;
    }
    hg->link = i;
    GtkTextIter s, e;
    markdown_link_get(buffer, (guint)i, &s, &e);
    gtk_text_buffer_select_range(buffer, &s, &e);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(hg->view), &s, 0.1, FALSE, 0.0, 0.0);
}

static void scroll_by(HelpGuide* hg, gdouble lines, gdouble pages) {
    GtkAdjustment* adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(hg->view));
    GtkTextIter first;
    gtk_text_buffer_get_start_iter(guide_buffer(hg), &first);
    GdkRectangle rect;
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(hg->view), &first, &rect);
    gdouble line_h = rect.height > 0 ? rect.height : 14.0;
    gtk_adjustment_set_value(adj, gtk_adjustment_get_value(adj) + lines * line_h +
                                      pages * gtk_adjustment_get_page_size(adj));
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

/* Navigation keys of the global mode scroll the page; Tab / Enter choose
 * and follow links; Shift+H / Shift+L go back / forward; ? shows the cheat
 * sheet */
static gboolean on_key(GtkEventControllerKey* controller, guint keyval, guint keycode,
                       GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    HelpGuide* hg = data;
    AppData* app = hg->app;
    if (games_check_help_key(app, keyval, state)) {
        return TRUE;
    }
    NavPrefix next = NAV_PREFIX_NONE;
    NavAction action = nav_keys_lookup(app_nav_style(app), (NavPrefix)app->nav_prefix, keyval, state, &next);
    app->nav_prefix = next;
    switch (action) {
    case NAV_NONE:
        break;
    case NAV_FORWARD:
    case NAV_BACKWARD:
        return FALSE;
    case NAV_DOWN: scroll_by(hg, 1, 0); return TRUE;
    case NAV_UP: scroll_by(hg, -1, 0); return TRUE;
    case NAV_PAGE_DOWN: scroll_by(hg, 0, 1); return TRUE;
    case NAV_PAGE_UP: scroll_by(hg, 0, -1); return TRUE;
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

    if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK)) != 0) {
        return FALSE;
    }
    switch (keyval) {
    case GDK_KEY_Tab:
        select_link(hg, 1);
        return TRUE;
    case GDK_KEY_ISO_Left_Tab:
        select_link(hg, -1);
        return TRUE;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
        if (hg->link >= 0) {
            gchar* url = g_strdup(markdown_link_get(guide_buffer(hg), (guint)hg->link, NULL, NULL));
            markdown_link_activate(GTK_TEXT_VIEW(hg->view), url);
            g_free(url);
        }
        return TRUE;
    case GDK_KEY_H:
        step_history(hg, -1);
        return TRUE;
    case GDK_KEY_L:
        step_history(hg, 1);
        return TRUE;
    case GDK_KEY_question:
        toggle_help_panel(app);
        return TRUE;
    case GDK_KEY_Escape:
        if (app->help_window != NULL && gtk_widget_get_visible(app->help_window)) {
            toggle_help_panel(app);
            return TRUE;
        }
        return FALSE;
    default:
        return FALSE;
    }
}

/* The mouse's back / forward buttons */
static void on_side_button(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
    if (button != 8 && button != 9) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    step_history(data, button == 8 ? -1 : 1);
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
    go_to(hg, USER_GUIDE_HOME, NULL, 0, TRUE);
    gtk_widget_grab_focus(hg->view);
}

static void on_back_clicked(GtkButton* button, gpointer data) {
    (void)button;
    step_history(data, -1);
}

static void on_forward_clicked(GtkButton* button, gpointer data) {
    (void)button;
    step_history(data, 1);
}

static void guide_free(gpointer data) {
    HelpGuide* hg = data;
    g_ptr_array_unref(hg->back);
    g_ptr_array_unref(hg->forward);
    g_free(hg->current);
    g_clear_object(&hg->label);
    g_free(hg);
}

static GtkWidget* header_button(GtkWidget* bar, const gchar* icon, const gchar* tooltip, GCallback cb, HelpGuide* hg) {
    GtkWidget* btn = icon_button_new(icon, tooltip);
    g_signal_connect(btn, "clicked", cb, hg);
    gtk_box_append(GTK_BOX(bar), btn);
    return btn;
}

/* Put the page in the notebook: after the Git tab and the result pages,
 * before the editor tabs */
static void insert_page(AppData* app, HelpGuide* hg) {
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    GtkWidget* git_page = git_tab_page(app);
    gint after = git_page != NULL ? gtk_notebook_page_num(nb, git_page) : 0;
    if (app->refs_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->refs_page));
    if (app->search_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->search_page));
    gtk_notebook_insert_page(nb, hg->page, hg->label, after + 1);
    app->help_page = hg->page;
    app_emit(app, APP_EVENT_CHROME_CHANGED); /* the Help button */
}

/* Take the page out of the notebook with its tab: an owned ref, or NULL */
static GtkWidget* detach_page(AppData* app) {
    GtkWidget* page = app->help_page;
    if (page == NULL || app->notebook == NULL) return NULL;
    GtkRoot* root = gtk_widget_get_root(page);
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    if (focus != NULL && gtk_widget_is_ancestor(focus, page)) gtk_root_set_focus(root, NULL);
    g_object_ref(page);
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gint n = gtk_notebook_page_num(nb, page);
    app->help_page = NULL;
    if (n >= 0) gtk_notebook_remove_page(nb, n);
    app_emit(app, APP_EVENT_CHROME_CHANGED); /* the Help button */
    return page;
}

/* The Help page, made when missing: after the Git tab, the result pages and
 * the Canvas page, before the editor tabs */
static HelpGuide* ensure_page(AppData* app) {
    HelpGuide* hg = guide_state(app);
    if (hg != NULL || app->notebook == NULL) return hg;
    ensure_css();
    hg = g_new0(HelpGuide, 1);
    hg->app = app;
    hg->back = g_ptr_array_new_with_free_func(place_free);
    hg->forward = g_ptr_array_new_with_free_func(place_free);
    hg->link = -1;

    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(page, "help-guide");
    gtk_widget_set_hexpand(page, TRUE);
    gtk_widget_set_vexpand(page, TRUE);

    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(bar, "help-guide-header");
    hg->back_btn = header_button(bar, "go-previous", _("Back (Shift+H)"), G_CALLBACK(on_back_clicked), hg);
    hg->forward_btn = header_button(bar, "go-next", _("Forward (Shift+L)"), G_CALLBACK(on_forward_clicked), hg);
    header_button(bar, "go-home", _("Contents of the User Guide"), G_CALLBACK(on_contents_clicked), hg);
    hg->title = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(hg->title), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(hg->title), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(hg->title, TRUE);
    gtk_box_append(GTK_BOX(bar), hg->title);
    header_button(bar, "dialog-question", _("Show Keyboard Shortcuts (?)"), G_CALLBACK(on_shortcuts_clicked), hg);
    header_button(bar, "window-close", _("Close Help"), G_CALLBACK(on_close_clicked), hg);
    gtk_box_append(GTK_BOX(page), bar);

    hg->view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(hg->view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(hg->view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(hg->view), GTK_WRAP_WORD_CHAR);
    set_view_monospace(hg->view, FALSE);
    set_view_text_padding(hg->view);
    apply_viewer_background(hg->view);
    markdown_link_attach(GTK_TEXT_VIEW(hg->view), on_link, hg);
    focus_route_mark_nav(hg->view);
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), hg);
    gtk_widget_add_controller(hg->view, keys);
    GtkGesture* side = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(side), 0);
    g_signal_connect(side, "pressed", G_CALLBACK(on_side_button), hg);
    gtk_widget_add_controller(hg->view, GTK_EVENT_CONTROLLER(side));

    GtkWidget* scroll = gtk_scrolled_window_new();
    /* Not GTK_POLICY_NEVER: a wide table or picture would hold the page at its
     * width (cut by the panel); tables scroll in their own frame */
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), hg->view);
    GtkWidget* body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(body), scroll);
    gtk_box_append(GTK_BOX(body), minimap_new_for_view(hg->view));
    gtk_box_append(GTK_BOX(page), body);

    hg->page = page;
    g_object_set_data_full(G_OBJECT(page), PAGE_KEY, hg, guide_free);
    update_buttons(hg);

    hg->label = g_object_ref(icon_label_new("dialog-question", _("Help")));
    GtkGesture* mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(mclick, "released", G_CALLBACK(on_label_middle_click), hg);
    gtk_widget_add_controller(hg->label, GTK_EVENT_CONTROLLER(mclick));
    insert_page(app, hg);
    return hg;
}

void help_guide_show(AppData* app, const gchar* page) {
    if (app == NULL) return;
    gboolean opened = guide_state(app) != NULL;
    HelpGuide* hg = ensure_page(app);
    if (hg == NULL) return;
    if (!opened) {
        go_to(hg, page != NULL ? page : USER_GUIDE_HOME, NULL, 0, FALSE);
    } else if (page != NULL && g_strcmp0(page, hg->current) != 0) {
        go_to(hg, page, NULL, 0, TRUE);
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

void help_guide_workspace_hidden(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    GtkWidget* page = detach_page(app);
    if (page == NULL) return;
    if (ws->help_page != NULL) g_object_unref(ws->help_page);
    ws->help_page = page;
}

void help_guide_workspace_shown(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || ws->help_page == NULL) return;
    GtkWidget* page = ws->help_page;
    ws->help_page = NULL;
    HelpGuide* hg = g_object_get_data(G_OBJECT(page), PAGE_KEY);
    if (hg != NULL && app->notebook != NULL && app->help_page == NULL) insert_page(app, hg);
    g_object_unref(page);
}

void help_guide_workspace_removed(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces && ws == &app->workspaces[app->current_ws]) {
        GtkWidget* page = detach_page(app);
        if (page != NULL) g_object_unref(page);
    }
    g_clear_object(&ws->help_page);
}
