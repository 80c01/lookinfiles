/* The Welcome tab: first steps for new users (see welcome_tab.h) */
#include "chrome/welcome_tab.h"
#include "core/app_events.h"
#include "core/app_paths.h"
#include "core/i18n.h"
#include "git/git_tab.h"
#include "theme/icons.h"
#include "theme/theme.h"
#include "widgets/add_button.h"
#include "widgets/focus_route.h"

#define PAGE_KEY "lookin-welcome-tab"

/* The keys the tab lists: Notepad, Vim, Emacs */
enum { KEYS_NOTEPAD, KEYS_VIM, KEYS_EMACS, KEYS_N };
static const gchar* const MODE_NAMES[KEYS_N] = {"Notepad", "Vim", "Emacs"};
static const gchar* const MODE_HINTS[KEYS_N] = {
    N_("The usual keys: arrows, Ctrl+C / Ctrl+V, Ctrl+F to search, Ctrl+S to save."),
    N_("Vim's keys: hjkl, gg / G, / to search, :w to save; the explorer moves with j / k."),
    N_("Emacs's keys: C-n / C-p, C-s to search, C-x C-s to save; the explorer moves with C-n / C-p."),
};

/* An essential shortcut: its keys in Notepad, Vim and Emacs (docs/keyboard.md) */
typedef struct {
    const gchar* keys[KEYS_N];
    const gchar* action; /* N_() */
} WelcomeKey;

static const WelcomeKey KEYS[] = {
    {{"Down / Up", "j / k", "C-n / C-p"}, N_("Move in the explorer")},
    {{"Enter", "Enter", "Enter"}, N_("Open a folder or a file")},
    {{"Ctrl+P", "Ctrl+P", "C-x C-f"}, N_("Go to a file of the project")},
    {{"Ctrl+Shift+F", "Ctrl+Shift+F", "Ctrl+Shift+F"}, N_("Search in files")},
    {{"Alt+R", "Alt+R", "Alt+R"}, N_("Recent folders and files")},
    {{"Alt+1 … Alt+8", "Alt+1 … Alt+8", "Alt+1 … Alt+8"}, N_("Switch workspace")},
    {{"?", "?", "?"}, N_("Every shortcut (cheat sheet)")},
    {{"F1", "F1", "F1"}, N_("User guide")},
    {{"Ctrl+,", "Ctrl+,", "Ctrl+,"}, N_("Preferences")},
    {{"Alt+T", "Alt+T", "Alt+T"}, N_("Themes and fonts")},
    {{"Ctrl+Q", "Ctrl+Q", "Ctrl+Q"}, N_("Quit")},
};

typedef struct {
    AppData* app;
    GtkWidget* page;      /* the notebook page (app->welcome_page) */
    GtkWidget* label;     /* tab label (owned ref: kept while the page is detached) */
    GtkWidget* mode_pill; /* picture of the top-bar mode pill */
    GtkWidget* keys_title;
    GtkWidget* mode_hint; /* what the keys of the global mode are */
    GtkWidget* keys;      /* GtkGrid of KEYS in the global mode */
} WelcomeTab;

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".welcome { background-color: @bg_view; }"
                  ".welcome .welcome-header { padding: 4px 10px; color: @fg; }"
                  ".welcome .welcome-title { color: @fg_strong; font-size: @size_title; font-weight: bold; }"
                  ".welcome .welcome-text { color: @fg_text; }"
                  ".welcome .welcome-section { color: @fg_strong; font-weight: bold; margin-top: 14px; }"
                  ".welcome .welcome-hint { color: @fg_subtle; font-size: @size_small; }"
                  ".welcome .welcome-key { font-family: @font_code; font-size: @size_code; color: @fg_text;"
                  "  background-color: @bg_block; border-radius: 4px; padding: 1px 6px; }");
}

static WelcomeTab* tab_state(AppData* app) {
    return app != NULL && app->welcome_page != NULL ? g_object_get_data(G_OBJECT(app->welcome_page), PAGE_KEY)
                                                    : NULL;
}

/* The keys listed for the global mode (Org-mode: Emacs) */
static guint keys_index(EditorMode mode) {
    switch (mode) {
    case EDITOR_MODE_VIM: return KEYS_VIM;
    case EDITOR_MODE_EMACS:
    case EDITOR_MODE_ORGMODE: return KEYS_EMACS;
    default: return KEYS_NOTEPAD;
    }
}

static GtkWidget* text_label(const gchar* text, const gchar* css_class) {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, css_class);
    return label;
}

/* A picture of a button of the window: the same widget and style, which
 * follows the theme, the icon set and the language, but takes no click */
static GtkWidget* picture_of(GtkWidget* button) {
    gtk_widget_set_can_target(button, FALSE);
    gtk_widget_set_focusable(button, FALSE);
    gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(button, GTK_ALIGN_START);
    return button;
}

/* A pill of the top bar: icon and label, no frame (as topbar.c draws them) */
static GtkWidget* pill_picture(const gchar* icon, const gchar* label) {
    GtkWidget* btn = gtk_button_new();
    if (label != NULL) {
        icon_button_set_content(btn, icon, label);
    } else {
        gtk_button_set_child(GTK_BUTTON(btn), icon_image_new(icon));
    }
    gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
    gtk_widget_add_css_class(btn, "status-pill");
    return picture_of(btn);
}

/* --- The values --- */

static void fill_keys(WelcomeTab* st, guint mode) {
    GtkWidget* child;
    while ((child = gtk_widget_get_first_child(st->keys)) != NULL) {
        gtk_grid_remove(GTK_GRID(st->keys), child);
    }
    for (guint i = 0; i < G_N_ELEMENTS(KEYS); i++) {
        GtkWidget* key = gtk_label_new(KEYS[i].keys[mode]);
        gtk_widget_add_css_class(key, "welcome-key");
        gtk_widget_set_halign(key, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(st->keys), key, 0, (gint)i, 1, 1);
        gtk_grid_attach(GTK_GRID(st->keys), text_label(_(KEYS[i].action), "welcome-text"), 1, (gint)i, 1, 1);
    }
}

/* The page shows the global mode of now (also when changed since) */
static void sync_values(WelcomeTab* st) {
    guint mode = keys_index(st->app->default_editor_mode);
    icon_button_set_content(st->mode_pill, "input-keyboard", MODE_NAMES[mode]);
    gchar* title = g_strdup_printf(_("Essential shortcuts (%s)"), MODE_NAMES[mode]);
    gtk_label_set_text(GTK_LABEL(st->keys_title), title);
    g_free(title);
    gtk_label_set_text(GTK_LABEL(st->mode_hint), _(MODE_HINTS[mode]));
    fill_keys(st, mode);
}

static void on_page_map(GtkWidget* page, gpointer data) {
    (void)page;
    sync_values(data);
}

/* --- The page --- */

static void tab_free(gpointer data) {
    WelcomeTab* st = data;
    g_clear_object(&st->label);
    g_free(st);
}

static void close_page(AppData* app) {
    if (app->welcome_page == NULL || app->notebook == NULL) return;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    GtkWidget* closed = app->welcome_page;
    GtkWidget* focus_back = focus_route_return_target(app, closed);
    g_object_ref(closed);
    gint num = gtk_notebook_page_num(nb, closed);
    app->welcome_page = NULL;
    if (num >= 0) gtk_notebook_remove_page(nb, num);
    focus_route_return(app, focus_back, closed);
    g_object_unref(closed);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
}

static void on_close_clicked(GtkButton* button, gpointer data) {
    (void)button;
    close_page(((WelcomeTab*)data)->app);
}

static void on_label_middle_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    close_page(((WelcomeTab*)data)->app);
}

/* Put the page in the notebook, right after the Git tab */
static void insert_page(AppData* app, WelcomeTab* st) {
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    GtkWidget* git_page = git_tab_page(app);
    gint after = git_page != NULL ? gtk_notebook_page_num(nb, git_page) : 0;
    gtk_notebook_insert_page(nb, st->page, st->label, after + 1);
    app->welcome_page = st->page;
    app_emit(app, APP_EVENT_CHROME_CHANGED);
}

/* Take the page out of the notebook with its tab: an owned ref, or NULL */
static GtkWidget* detach_page(AppData* app) {
    GtkWidget* page = app->welcome_page;
    if (page == NULL || app->notebook == NULL) return NULL;
    GtkRoot* root = gtk_widget_get_root(page);
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    if (focus != NULL && gtk_widget_is_ancestor(focus, page)) gtk_root_set_focus(root, NULL);
    g_object_ref(page);
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gint n = gtk_notebook_page_num(nb, page);
    app->welcome_page = NULL;
    if (n >= 0) gtk_notebook_remove_page(nb, n);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    return page;
}

static GtkWidget* section(GtkWidget* body, const gchar* title) {
    GtkWidget* label = text_label(title, "welcome-section");
    gtk_box_append(GTK_BOX(body), label);
    return label;
}

/* A grid of how-to rows: a picture of what to click, then what it does */
static GtkWidget* howto_grid(GtkWidget* body) {
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_margin_top(grid, 4);
    gtk_box_append(GTK_BOX(body), grid);
    return grid;
}

static void howto_row(GtkWidget* grid, gint row, GtkWidget* picture, const gchar* text) {
    gtk_grid_attach(GTK_GRID(grid), picture, 0, row, 1, 1);
    GtkWidget* label = text_label(text, "welcome-text");
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_grid_attach(GTK_GRID(grid), label, 1, row, 1, 1);
}

static void build_body(WelcomeTab* st, GtkWidget* body) {
    gchar* title = g_strdup_printf(_("Welcome to %s"), APP_NAME);
    gtk_box_append(GTK_BOX(body), text_label(title, "welcome-title"));
    g_free(title);
    gtk_box_append(GTK_BOX(body),
                   text_label(_("The explorer is on the left, the preview of the selected file on the right, "
                                "with editor tabs and terminals beside it. The buttons pictured below are in "
                                "the top bar, at the right, and at the end of the tab bars."),
                              "welcome-text"));

    section(body, _("Make it yours"));
    GtkWidget* grid = howto_grid(body);
    st->mode_pill = pill_picture("input-keyboard", MODE_NAMES[KEYS_NOTEPAD]);
    howto_row(grid, 0, st->mode_pill,
              _("Editor keys: click the mode in the top bar and choose Notepad, Vim or Emacs. "
                "Preferences → Editor → Mode keeps the choice for the next launches."));
    howto_row(grid, 1, pill_picture("applications-system", NULL),
              _("Preferences (Ctrl+,): the gear of the top bar. Themes and fonts are in Appearance "
                "(Alt+T opens it there), the window size and this tab at startup in Window."));

    st->keys_title = section(body, "");
    st->mode_hint = text_label(NULL, "welcome-hint");
    gtk_box_append(GTK_BOX(body), st->mode_hint);
    st->keys = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(st->keys), 4);
    gtk_grid_set_column_spacing(GTK_GRID(st->keys), 12);
    gtk_widget_set_margin_top(st->keys, 4);
    gtk_box_append(GTK_BOX(body), st->keys);

    section(body, _("Next steps"));
    grid = howto_grid(body);
    howto_row(grid, 0, pill_picture("dialog-question", _("Help")),
              _("The user guide (F1): Help, at the right of the top bar. "
                "A right click on it lists every shortcut (?)."));
    howto_row(grid, 1, picture_of(add_button_new(NULL)),
              _("New tabs: the + at the end of a tab bar opens a terminal, an editor, Diagnostics "
                "(the optional libraries this computer has: terminal, syntax highlighting, PDF pages...) "
                "or this Welcome tab again."));

    gtk_box_append(GTK_BOX(body),
                   text_label(_("To show this tab at every startup: Preferences → Window → "
                                "Show the Welcome tab at startup."),
                              "welcome-hint"));
    gtk_widget_set_margin_top(gtk_widget_get_last_child(body), 14);
}

static WelcomeTab* ensure_page(AppData* app) {
    WelcomeTab* st = tab_state(app);
    if (st != NULL || app->notebook == NULL) return st;
    ensure_css();
    st = g_new0(WelcomeTab, 1);
    st->app = app;

    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(page, "welcome");
    gtk_widget_set_hexpand(page, TRUE);
    gtk_widget_set_vexpand(page, TRUE);
    st->page = page;

    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(bar, "welcome-header");
    GtkWidget* name = icon_label_new("go-home", _("Welcome"));
    gtk_widget_set_hexpand(name, TRUE);
    gtk_widget_set_halign(name, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(bar), name);
    GtkWidget* close = icon_button_new("window-close", _("Close Welcome"));
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), st);
    gtk_box_append(GTK_BOX(bar), close);
    gtk_box_append(GTK_BOX(page), bar);

    GtkWidget* body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_halign(body, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(body, 18);
    gtk_widget_set_margin_bottom(body, 18);
    gtk_widget_set_margin_start(body, 24);
    gtk_widget_set_margin_end(body, 24);
    build_body(st, body);
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), body);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(page), scroll);
    g_signal_connect(page, "map", G_CALLBACK(on_page_map), st);

    g_object_set_data_full(G_OBJECT(page), PAGE_KEY, st, tab_free);

    st->label = g_object_ref(icon_label_new("go-home", _("Welcome")));
    GtkGesture* mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(mclick, "released", G_CALLBACK(on_label_middle_click), st);
    gtk_widget_add_controller(st->label, GTK_EVENT_CONTROLLER(mclick));

    sync_values(st);
    insert_page(app, st);
    return st;
}

void welcome_tab_show(AppData* app) {
    if (app == NULL) return;
    WelcomeTab* st = ensure_page(app);
    if (st == NULL) return;
    /* The preview panel holds the tab: show it when hidden */
    if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
        app_layout_begin(app);
        gtk_widget_set_visible(app->viewer_box, TRUE);
        app_emit(app, APP_EVENT_CHROME_CHANGED);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    }
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, st->page));
    gtk_widget_grab_focus(st->page);
}

gboolean welcome_tab_close_shown(AppData* app) {
    if (app == NULL || app->welcome_page == NULL || app->notebook == NULL) return FALSE;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    if (gtk_notebook_get_current_page(nb) != gtk_notebook_page_num(nb, app->welcome_page)) return FALSE;
    if (!gtk_widget_get_mapped(app->welcome_page)) return FALSE;
    close_page(app);
    return TRUE;
}

void welcome_tab_workspace_hidden(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    GtkWidget* page = detach_page(app);
    if (page == NULL) return;
    if (ws->welcome_page != NULL) g_object_unref(ws->welcome_page);
    ws->welcome_page = page;
}

void welcome_tab_workspace_shown(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || ws->welcome_page == NULL) return;
    GtkWidget* page = ws->welcome_page;
    ws->welcome_page = NULL;
    WelcomeTab* st = g_object_get_data(G_OBJECT(page), PAGE_KEY);
    if (st != NULL && app->notebook != NULL && app->welcome_page == NULL) insert_page(app, st);
    g_object_unref(page);
}

void welcome_tab_workspace_removed(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces && ws == &app->workspaces[app->current_ws]) {
        GtkWidget* page = detach_page(app);
        if (page != NULL) g_object_unref(page);
    }
    g_clear_object(&ws->welcome_page);
}
