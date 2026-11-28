/* The Welcome tab: first steps for new users (see welcome_tab.h) */
#include "chrome/welcome_tab.h"
#include "chrome/diagnostics_tab.h"
#include "chrome/help_guide.h"
#include "chrome/preferences.h"
#include "chrome/themes.h"
#include "core/app_events.h"
#include "core/app_paths.h"
#include "core/i18n.h"
#include "git/git_tab.h"
#include "theme/icons.h"
#include "theme/theme.h"
#include "theme/theme_store.h"
#include "widgets/focus_route.h"

#define PAGE_KEY "lookin-welcome-tab"

/* Drop-down order of the editor keys */
static const SettingsEditor MODES[] = {SETTINGS_EDITOR_NOTEPAD, SETTINGS_EDITOR_VIM, SETTINGS_EDITOR_EMACS};
static const gchar* const MODE_HINTS[] = {
    N_("The usual keys: arrows, Ctrl+C / Ctrl+V, Ctrl+F to search, Ctrl+S to save."),
    N_("Vim's keys: hjkl, gg / G, / to search, :w to save; the explorer moves with j / k."),
    N_("Emacs's keys: C-n / C-p, C-s to search, C-x C-s to save; the explorer moves with C-n / C-p."),
};

/* An essential shortcut: its keys in Notepad, Vim and Emacs (docs/keyboard.md) */
typedef struct {
    const gchar* keys[G_N_ELEMENTS(MODES)];
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
    {{"Ctrl+Q", "Ctrl+Q", "Ctrl+Q"}, N_("Quit")},
};

typedef struct {
    AppData* app;
    GtkWidget* page;  /* the notebook page (app->welcome_page) */
    GtkWidget* label; /* tab label (owned ref: kept while the page is detached) */
    GtkWidget* theme; /* GtkDropDown of the theme names */
    GtkStringList* theme_names;
    GtkWidget* mode;      /* GtkDropDown: Notepad, Vim, Emacs */
    GtkWidget* mode_hint; /* what the mode chosen is */
    GtkWidget* keys;      /* GtkGrid of KEYS in the mode chosen */
    GtkWidget* startup;   /* GtkCheckButton: [Window] welcome */
    gboolean syncing;     /* the widgets follow the values: no change to apply */
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

/* Index in MODES of the global mode (Org-mode: Emacs) */
static guint mode_index(EditorMode mode) {
    switch (mode) {
    case EDITOR_MODE_VIM: return 1;
    case EDITOR_MODE_EMACS:
    case EDITOR_MODE_ORGMODE: return 2;
    default: return 0;
    }
}

static GtkWidget* text_label(const gchar* text, const gchar* css_class) {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, css_class);
    return label;
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

static void fill_themes(WelcomeTab* st) {
    gchar* dir = theme_store_user_dir();
    GPtrArray* themes = theme_store_list(dir, NULL);
    g_free(dir);
    guint n = g_list_model_get_n_items(G_LIST_MODEL(st->theme_names));
    const gchar* current = themes_current_name();
    guint selected = GTK_INVALID_LIST_POSITION;
    const gchar** names = g_new0(const gchar*, themes->len + 1);
    for (guint i = 0; i < themes->len; i++) {
        const Theme* t = g_ptr_array_index(themes, i);
        names[i] = t->name;
        if (g_strcmp0(t->name, current) == 0) selected = i;
    }
    gtk_string_list_splice(st->theme_names, 0, n, names);
    g_free(names);
    g_ptr_array_unref(themes);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(st->theme), selected);
}

/* The widgets show the current values (also those set elsewhere since) */
static void sync_values(WelcomeTab* st) {
    st->syncing = TRUE;
    fill_themes(st);
    guint mode = mode_index(st->app->default_editor_mode);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(st->mode), mode);
    gtk_label_set_text(GTK_LABEL(st->mode_hint), _(MODE_HINTS[mode]));
    fill_keys(st, mode);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(st->startup), st->app->settings.welcome);
    st->syncing = FALSE;
}

static void on_theme_selected(GObject* dropdown, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    WelcomeTab* st = data;
    if (st->syncing) return;
    const gchar* name = gtk_string_list_get_string(st->theme_names, gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown)));
    if (name == NULL || g_strcmp0(name, themes_current_name()) == 0) return;
    GError* error = NULL;
    if (!themes_apply(st->app, name, TRUE, &error)) {
        app_notice(st->app, error != NULL ? error->message : name, FALSE);
        g_clear_error(&error);
    }
}

static void on_mode_selected(GObject* dropdown, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    WelcomeTab* st = data;
    if (st->syncing) return;
    guint mode = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    if (mode >= G_N_ELEMENTS(MODES)) return;
    preferences_save_editor(st->app, MODES[mode]);
    gtk_label_set_text(GTK_LABEL(st->mode_hint), _(MODE_HINTS[mode]));
    fill_keys(st, mode);
}

static void on_startup_toggled(GtkCheckButton* check, gpointer data) {
    WelcomeTab* st = data;
    if (st->syncing) return;
    preferences_save_welcome(st->app, gtk_check_button_get_active(check));
}

static void on_guide_clicked(GtkButton* button, gpointer data) {
    (void)button;
    help_guide_show(((WelcomeTab*)data)->app, "getting-started.md");
}

static void on_diagnostics_clicked(GtkButton* button, gpointer data) {
    (void)button;
    diagnostics_tab_show(((WelcomeTab*)data)->app, NULL);
}

static void on_preferences_clicked(GtkButton* button, gpointer data) {
    (void)button;
    preferences_show(((WelcomeTab*)data)->app, NULL);
}

static void on_page_map(GtkWidget* page, gpointer data) {
    (void)page;
    sync_values(data);
}

/* --- The page --- */

static void tab_free(gpointer data) {
    WelcomeTab* st = data;
    g_clear_object(&st->theme_names);
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

static void section(GtkWidget* body, const gchar* title) {
    gtk_box_append(GTK_BOX(body), text_label(title, "welcome-section"));
}

static void link_button(GtkWidget* box, const gchar* label, const gchar* tooltip, GCallback cb,
                              WelcomeTab* st) {
    GtkWidget* button = gtk_button_new_with_mnemonic(label);
    gtk_widget_set_tooltip_text(button, tooltip);
    g_signal_connect(button, "clicked", cb, st);
    gtk_box_append(GTK_BOX(box), button);
}

static void build_body(WelcomeTab* st, GtkWidget* body) {
    gchar* title = g_strdup_printf(_("Welcome to %s"), APP_NAME);
    gtk_box_append(GTK_BOX(body), text_label(title, "welcome-title"));
    g_free(title);
    gtk_box_append(GTK_BOX(body),
                   text_label(_("The explorer is on the left, the preview of the selected file on the right, "
                                "with editor tabs and terminals beside it. Choose how it looks and which keys "
                                "it uses; both stay in the Preferences."),
                              "welcome-text"));

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_margin_top(grid, 14);
    st->theme_names = gtk_string_list_new(NULL);
    st->theme = gtk_drop_down_new(G_LIST_MODEL(g_object_ref(st->theme_names)), NULL);
    gtk_widget_set_tooltip_text(st->theme, _("Applied as you choose it"));
    g_signal_connect(st->theme, "notify::selected", G_CALLBACK(on_theme_selected), st);
    GtkWidget* theme_label = gtk_label_new_with_mnemonic(_("_Theme"));
    gtk_label_set_mnemonic_widget(GTK_LABEL(theme_label), st->theme);
    gtk_label_set_xalign(GTK_LABEL(theme_label), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), theme_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), st->theme, 1, 0, 1, 1);

    static const char* const mode_names[] = {"Notepad", "Vim", "Emacs", NULL};
    st->mode = gtk_drop_down_new_from_strings(mode_names);
    gtk_widget_set_tooltip_text(st->mode, _("Keys of the explorer and of new editor tabs"));
    g_signal_connect(st->mode, "notify::selected", G_CALLBACK(on_mode_selected), st);
    GtkWidget* mode_label = gtk_label_new_with_mnemonic(_("_Editor keys"));
    gtk_label_set_mnemonic_widget(GTK_LABEL(mode_label), st->mode);
    gtk_label_set_xalign(GTK_LABEL(mode_label), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), mode_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), st->mode, 1, 1, 1, 1);
    st->mode_hint = text_label(NULL, "welcome-hint");
    gtk_grid_attach(GTK_GRID(grid), st->mode_hint, 1, 2, 1, 1);
    gtk_box_append(GTK_BOX(body), grid);

    section(body, _("Essential shortcuts"));
    st->keys = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(st->keys), 4);
    gtk_grid_set_column_spacing(GTK_GRID(st->keys), 12);
    gtk_box_append(GTK_BOX(body), st->keys);

    section(body, _("Next steps"));
    GtkWidget* links = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    link_button(links, _("Open the _User Guide"), _("Getting started, in a Help tab (F1)"),
                           G_CALLBACK(on_guide_clicked), st);
    link_button(links, _("Check the Optional _Libraries"),
                _("What this computer has of the terminal, syntax highlighting, PDF pages... (Diagnostics tab)"),
                G_CALLBACK(on_diagnostics_clicked), st);
    link_button(links, _("_Preferences"), _("Fonts, editor tabs, window size, layouts (Ctrl+,)"),
                G_CALLBACK(on_preferences_clicked), st);
    gtk_box_append(GTK_BOX(body), links);

    st->startup = gtk_check_button_new_with_mnemonic(_("_Show this tab at startup"));
    gtk_widget_set_tooltip_text(st->startup, _("Also in Preferences → Window"));
    gtk_widget_set_margin_top(st->startup, 14);
    g_signal_connect(st->startup, "toggled", G_CALLBACK(on_startup_toggled), st);
    gtk_box_append(GTK_BOX(body), st->startup);
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
    gtk_widget_grab_focus(st->theme);
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
