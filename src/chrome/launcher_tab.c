/* The Applications tab: the applications a new tab can hold (see launcher_tab.h) */
#include "chrome/launcher_tab.h"
#include "core/i18n.h"
#include "editor/editor.h"
#include "jupyter/ipynb_tab.h"
#include "notes/notes.h"
#include "preview/browser.h"
#include "theme/icons.h"
#include "theme/theme.h"
#include "widgets/focus_route.h"

#define LAUNCHER_KEY "lookin-launcher-tab"

typedef enum {
    LAUNCH_WEB,
    LAUNCH_JUPYTER,
    LAUNCH_CANVAS,
    LAUNCH_ORG,
    LAUNCH_MARKDOWN,
    LAUNCH_VIM,
    LAUNCH_EMACS,
    LAUNCH_NOTEPAD
} LaunchKind;

typedef struct {
    const gchar* id;
    LaunchKind kind;
    const gchar* label;     /* N_() */
    const gchar* desc;      /* N_() */
    const gchar* glyph;     /* glyph_image_set(), or NULL */
    const gchar* file_icon; /* file name of a file-type icon, or NULL */
    const gchar* missing;   /* N_(): why it is not available */
} LauncherApp;

static const LauncherApp APPS[] = {
    {"web", LAUNCH_WEB, N_("Web Browser"), N_("Web pages in a tab"), "globe", NULL,
     N_("Needs WebKitGTK 6.0 (WebView2 on Windows)")},
    {"jupyter", LAUNCH_JUPYTER, N_("Jupyter Notebook"), N_("A new notebook in memory, run cell by cell"), NULL,
     "notebook.ipynb", NULL},
    {"canvas", LAUNCH_CANVAS, N_("Canvas"), N_("A new Canvas note: text boxes and pictures on a page"), "canvas",
     NULL, NULL},
    {"org", LAUNCH_ORG, N_("Org-mode"), N_("An empty Org-mode document"), NULL, "notes.org", NULL},
    {"markdown", LAUNCH_MARKDOWN, N_("Markdown"), N_("An empty Markdown document"), NULL, "README.md", NULL},
    {"vim", LAUNCH_VIM, N_("Vim"), N_("An empty editor tab with the Vim keys"), "document-edit", NULL, NULL},
    {"emacs", LAUNCH_EMACS, N_("Emacs"), N_("An empty editor tab with the Emacs keys"), "document-edit", NULL, NULL},
    {"notepad", LAUNCH_NOTEPAD, N_("Notepad"), N_("An empty editor tab with the usual keys"), "text-x-generic", NULL,
     NULL},
};

typedef struct {
    AppData* app;
    GtkWidget* page;
    GtkWidget* label; /* the tab header, kept while its workspace is hidden */
    gint hidden_at;   /* its position in the preview notebook when hidden */
    GtkWidget* entry;
    GtkWidget* list;
    GtkWidget* empty;
    GtkCustomFilter* filter;
    GtkSingleSelection* sel;
    gchar* query; /* casefolded search text, NULL when empty */
} LauncherTab;

static const LauncherApp* app_of_id(const gchar* id) {
    for (gsize i = 0; i < G_N_ELEMENTS(APPS); i++) {
        if (g_strcmp0(APPS[i].id, id) == 0) return &APPS[i];
    }
    return NULL;
}

static gboolean app_available(const LauncherApp* a) {
    switch (a->kind) {
    case LAUNCH_WEB:
        return browser_available();
    default:
        return TRUE;
    }
}

static const LauncherApp* app_of_item(gpointer item) {
    return item != NULL ? app_of_id(gtk_string_object_get_string(GTK_STRING_OBJECT(item))) : NULL;
}

static LauncherTab* tab_of(GtkWidget* page) {
    return page != NULL ? g_object_get_data(G_OBJECT(page), LAUNCHER_KEY) : NULL;
}

static void tab_free(gpointer data) {
    LauncherTab* lt = data;
    g_clear_object(&lt->label);
    g_free(lt->query);
    g_free(lt);
}

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".launcher { background-color: @bg_view; }"
                  ".launcher listview { background: none; }"
                  ".launcher row { padding: 6px 10px; border-radius: 6px; }"
                  ".launcher .launcher-title { color: @fg_strong; }"
                  ".launcher .launcher-desc { color: @fg_subtle; font-size: @size_small; }"
                  ".launcher .launcher-empty { color: @fg_subtle; }");
}

/* ------------------------------------------------------------------------- */
/* Closing                                                                   */
/* ------------------------------------------------------------------------- */

static GtkNotebook* notebook_of(GtkWidget* page) {
    GtkWidget* nb = gtk_widget_get_ancestor(page, GTK_TYPE_NOTEBOOK);
    return nb != NULL ? GTK_NOTEBOOK(nb) : NULL;
}

/* Take the page out of its notebook; give_back: the focus goes where it
 * was before the tab (Ctrl+W), else it is left to the tab opened instead */
static void close_page(LauncherTab* lt, gboolean give_back) {
    AppData* app = lt->app;
    GtkWidget* page = g_object_ref(lt->page);
    if (notebook_of(page) != NULL) {
        GtkRoot* root = gtk_widget_get_root(page);
        GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
        GtkWidget* back = give_back ? focus_route_return_target(app, page) : NULL;
        if (focus != NULL && gtk_widget_is_ancestor(focus, page)) gtk_root_set_focus(root, NULL);
        editor_remove_page(app, page); /* an editor notebook left empty goes */
        if (give_back) focus_route_return(app, back, page);
    }
    g_object_unref(page);
}

/* ------------------------------------------------------------------------- */
/* Opening an application                                                    */
/* ------------------------------------------------------------------------- */

static void open_app(LauncherTab* lt, const LauncherApp* a) {
    if (a == NULL || !app_available(a)) return;
    AppData* app = lt->app;
    GtkNotebook* nb = notebook_of(lt->page);
    if (nb == NULL) return;
    g_object_ref(nb);
    gint at = gtk_notebook_page_num(nb, lt->page);
    switch (a->kind) {
    case LAUNCH_WEB:
        browser_new_tab_in(app, nb);
        break;
    case LAUNCH_JUPYTER:
        ipynb_tab_new_untitled(app, nb);
        break;
    case LAUNCH_CANVAS: {
        GtkWidget* canvas = notes_new_canvas(app);
        if (canvas != NULL) editor_move_page_to(app, canvas, nb, -1);
        break;
    }
    case LAUNCH_ORG:
        editor_open_untitled(app, nb, EDITOR_MODE_ORGMODE);
        break;
    case LAUNCH_MARKDOWN:
        editor_open_untitled_as(app, nb, app->default_editor_mode, ".md");
        break;
    case LAUNCH_VIM:
        editor_open_untitled(app, nb, EDITOR_MODE_VIM);
        break;
    case LAUNCH_EMACS:
        editor_open_untitled(app, nb, EDITOR_MODE_EMACS);
        break;
    case LAUNCH_NOTEPAD:
        editor_open_untitled(app, nb, EDITOR_MODE_NOTEPAD);
        break;
    }
    /* The new tab takes the place of this one */
    gint cur = gtk_notebook_get_current_page(nb);
    GtkWidget* opened = cur >= 0 ? gtk_notebook_get_nth_page(nb, cur) : NULL;
    if (opened == lt->page) opened = NULL; /* nothing opened */
    if (opened != NULL) g_object_ref(opened);
    if (opened != NULL && at >= 0) gtk_notebook_reorder_child(nb, opened, at);
    g_object_unref(nb);
    close_page(lt, FALSE); /* lt is freed */
    GtkRoot* root = opened != NULL ? gtk_widget_get_root(opened) : NULL;
    if (root != NULL && gtk_root_get_focus(root) == NULL) gtk_widget_child_focus(opened, GTK_DIR_TAB_FORWARD);
    if (opened != NULL) g_object_unref(opened);
}

static void open_selected(LauncherTab* lt) {
    open_app(lt, app_of_item(gtk_single_selection_get_selected_item(lt->sel)));
}

/* ------------------------------------------------------------------------- */
/* The list                                                                  */
/* ------------------------------------------------------------------------- */

static gboolean contains_folded(const gchar* text, const gchar* query) {
    gchar* folded = g_utf8_casefold(text, -1);
    gboolean found = strstr(folded, query) != NULL;
    g_free(folded);
    return found;
}

static gboolean filter_func(gpointer item, gpointer data) {
    LauncherTab* lt = data;
    const LauncherApp* a = app_of_item(item);
    if (a == NULL) return FALSE;
    if (lt->query == NULL) return TRUE;
    return contains_folded(_(a->label), lt->query) || contains_folded(_(a->desc), lt->query) ||
           contains_folded(a->id, lt->query);
}

static void on_search_changed(GtkSearchEntry* entry, gpointer data) {
    LauncherTab* lt = data;
    const gchar* text = gtk_editable_get_text(GTK_EDITABLE(entry));
    gchar* stripped = g_strstrip(g_strdup(text != NULL ? text : ""));
    g_clear_pointer(&lt->query, g_free);
    if (stripped[0] != '\0') lt->query = g_utf8_casefold(stripped, -1);
    g_free(stripped);
    gtk_filter_changed(GTK_FILTER(lt->filter), GTK_FILTER_CHANGE_DIFFERENT);
    guint n = g_list_model_get_n_items(G_LIST_MODEL(lt->sel));
    gtk_widget_set_visible(lt->empty, n == 0);
    if (n > 0) {
        gtk_single_selection_set_selected(lt->sel, 0);
        gtk_list_view_scroll_to(GTK_LIST_VIEW(lt->list), 0, GTK_LIST_SCROLL_NONE, NULL);
    }
}

static void on_entry_activate(GtkSearchEntry* entry, gpointer data) {
    (void)entry;
    open_selected(data);
}

static void on_list_activate(GtkListView* list, guint position, gpointer data) {
    (void)list;
    LauncherTab* lt = data;
    gpointer item = g_list_model_get_item(G_LIST_MODEL(lt->sel), position);
    const LauncherApp* a = app_of_item(item);
    if (item != NULL) g_object_unref(item); /* the string list keeps it */
    open_app(lt, a);
}

/* Up / Down / Page Up / Page Down from the search field move in the list,
 * Escape closes the tab */
static gboolean on_entry_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType state,
                             gpointer data) {
    (void)ctrl;
    (void)keycode;
    LauncherTab* lt = data;
    if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK)) != 0) return FALSE;
    if (keyval == GDK_KEY_Escape) {
        close_page(lt, TRUE);
        return TRUE;
    }
    gint step = 0;
    switch (keyval) {
    case GDK_KEY_Down:
    case GDK_KEY_KP_Down:
        step = 1;
        break;
    case GDK_KEY_Up:
    case GDK_KEY_KP_Up:
        step = -1;
        break;
    case GDK_KEY_Page_Down:
    case GDK_KEY_KP_Page_Down:
        step = 5;
        break;
    case GDK_KEY_Page_Up:
    case GDK_KEY_KP_Page_Up:
        step = -5;
        break;
    default:
        return FALSE;
    }
    gint n = (gint)g_list_model_get_n_items(G_LIST_MODEL(lt->sel));
    if (n == 0) return TRUE;
    guint sel = gtk_single_selection_get_selected(lt->sel);
    gint pos = sel == GTK_INVALID_LIST_POSITION ? 0 : (gint)sel + step;
    pos = CLAMP(pos, 0, n - 1);
    gtk_single_selection_set_selected(lt->sel, (guint)pos);
    gtk_list_view_scroll_to(GTK_LIST_VIEW(lt->list), (guint)pos, GTK_LIST_SCROLL_NONE, NULL);
    return TRUE;
}

static void on_row_setup(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    (void)data;
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* glyph = glyph_image_new("text-x-generic");
    GtkWidget* file_icon = file_type_image_new();
    gtk_widget_set_valign(glyph, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(file_icon, GTK_ALIGN_CENTER);
    GtkWidget* texts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* title = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_widget_add_css_class(title, "launcher-title");
    GtkWidget* desc = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(desc), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(desc), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(desc, "launcher-desc");
    gtk_box_append(GTK_BOX(texts), title);
    gtk_box_append(GTK_BOX(texts), desc);
    gtk_box_append(GTK_BOX(row), glyph);
    gtk_box_append(GTK_BOX(row), file_icon);
    gtk_box_append(GTK_BOX(row), texts);
    g_object_set_data(G_OBJECT(row), "glyph", glyph);
    g_object_set_data(G_OBJECT(row), "file-icon", file_icon);
    g_object_set_data(G_OBJECT(row), "title", title);
    g_object_set_data(G_OBJECT(row), "desc", desc);
    gtk_list_item_set_child(item, row);
}

static void on_row_bind(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    (void)data;
    GtkWidget* row = gtk_list_item_get_child(item);
    const LauncherApp* a = app_of_item(gtk_list_item_get_item(item));
    if (row == NULL || a == NULL) return;
    GtkWidget* glyph = g_object_get_data(G_OBJECT(row), "glyph");
    GtkWidget* file_icon = g_object_get_data(G_OBJECT(row), "file-icon");
    if (a->file_icon != NULL) {
        file_type_image_set(file_icon, a->file_icon, FALSE);
    } else {
        glyph_image_set(glyph, a->glyph);
    }
    gtk_widget_set_visible(file_icon, a->file_icon != NULL);
    gtk_widget_set_visible(glyph, a->file_icon == NULL);
    gboolean available = app_available(a);
    gtk_label_set_text(GTK_LABEL(g_object_get_data(G_OBJECT(row), "title")), _(a->label));
    gtk_label_set_text(GTK_LABEL(g_object_get_data(G_OBJECT(row), "desc")),
                       available || a->missing == NULL ? _(a->desc) : _(a->missing));
    gtk_widget_set_sensitive(row, available);
}

/* ------------------------------------------------------------------------- */
/* The tab                                                                   */
/* ------------------------------------------------------------------------- */

static void on_label_middle_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    LauncherTab* lt = tab_of(GTK_WIDGET(data));
    if (lt != NULL) close_page(lt, TRUE);
}

void launcher_tab_open(AppData* app, GtkNotebook* nb) {
    if (app == NULL || nb == NULL) return;
    ensure_css();
    LauncherTab* lt = g_new0(LauncherTab, 1);
    lt->app = app;

    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(page, "launcher");
    gtk_widget_set_hexpand(page, TRUE);
    gtk_widget_set_vexpand(page, TRUE);
    lt->page = page;
    g_object_set_data_full(G_OBJECT(page), LAUNCHER_KEY, lt, tab_free);

    GtkWidget* entry = gtk_search_entry_new();
    gtk_search_entry_set_search_delay(GTK_SEARCH_ENTRY(entry), 0);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(entry), _("Search the applications"));
    gtk_widget_set_margin_top(entry, 12);
    gtk_widget_set_margin_start(entry, 12);
    gtk_widget_set_margin_end(entry, 12);
    lt->entry = entry;
    g_signal_connect(entry, "search-changed", G_CALLBACK(on_search_changed), lt);
    g_signal_connect(entry, "activate", G_CALLBACK(on_entry_activate), lt);
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_entry_key), lt);
    gtk_widget_add_controller(entry, keys);
    gtk_box_append(GTK_BOX(page), entry);

    GtkStringList* ids = gtk_string_list_new(NULL);
    for (gsize i = 0; i < G_N_ELEMENTS(APPS); i++) gtk_string_list_append(ids, APPS[i].id);
    lt->filter = gtk_custom_filter_new(filter_func, lt, NULL);
    GtkFilterListModel* filtered = gtk_filter_list_model_new(G_LIST_MODEL(ids), GTK_FILTER(lt->filter));
    lt->sel = gtk_single_selection_new(G_LIST_MODEL(filtered));
    gtk_single_selection_set_autoselect(lt->sel, TRUE);
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_row_setup), lt);
    g_signal_connect(factory, "bind", G_CALLBACK(on_row_bind), lt);
    GtkWidget* list = gtk_list_view_new(GTK_SELECTION_MODEL(lt->sel), factory);
    gtk_list_view_set_single_click_activate(GTK_LIST_VIEW(list), TRUE);
    gtk_widget_set_focusable(list, FALSE);
    lt->list = list;
    g_signal_connect(list, "activate", G_CALLBACK(on_list_activate), lt);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_margin_start(scroll, 6);
    gtk_widget_set_margin_end(scroll, 6);
    gtk_box_append(GTK_BOX(page), scroll);

    lt->empty = gtk_label_new(_("No application matches"));
    gtk_widget_add_css_class(lt->empty, "launcher-empty");
    gtk_widget_set_visible(lt->empty, FALSE);
    gtk_widget_set_margin_bottom(lt->empty, 12);
    gtk_box_append(GTK_BOX(page), lt->empty);

    /* Tab header: icon and name; closed with Ctrl+W or a middle click */
    GtkWidget* label = glyph_label_new("view-grid", _("Applications"));
    GtkGesture* mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(mclick, "released", G_CALLBACK(on_label_middle_click), page);
    gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(mclick));
    lt->label = g_object_ref(label);

    gint idx = gtk_notebook_append_page(nb, page, label);
    gtk_notebook_set_tab_reorderable(nb, page, TRUE);
    gtk_notebook_set_tab_detachable(nb, page, TRUE);
    gtk_notebook_set_current_page(nb, idx);
    focus_route_mark_focus(entry);
    gtk_widget_grab_focus(entry);
}

gboolean launcher_tab_close_shown(AppData* app) {
    if (app == NULL) return FALSE;
    LauncherTab* lt = NULL;
    GtkWidget* focus = app->window != NULL ? gtk_window_get_focus(GTK_WINDOW(app->window)) : NULL;
    for (GtkWidget* w = focus; w != NULL && lt == NULL; w = gtk_widget_get_parent(w)) lt = tab_of(w);
    if (lt == NULL && app->notebook != NULL) {
        GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
        gint cur = gtk_notebook_get_current_page(nb);
        GtkWidget* page = cur >= 0 ? gtk_notebook_get_nth_page(nb, cur) : NULL;
        lt = tab_of(page);
        if (lt != NULL && !gtk_widget_get_mapped(page)) lt = NULL;
    }
    if (lt == NULL) return FALSE;
    close_page(lt, TRUE);
    return TRUE;
}

/* The Applications tabs of the preview notebook, in their order */
static GPtrArray* preview_pages(AppData* app) {
    GPtrArray* pages = g_ptr_array_new();
    if (app->notebook == NULL) return pages;
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    for (gint i = 0; i < gtk_notebook_get_n_pages(nb); i++) {
        GtkWidget* page = gtk_notebook_get_nth_page(nb, i);
        if (tab_of(page) != NULL) g_ptr_array_add(pages, page);
    }
    return pages;
}

/* Take the page out of the preview notebook: an owned ref */
static GtkWidget* detach_page(LauncherTab* lt) {
    GtkWidget* page = g_object_ref(lt->page);
    GtkNotebook* nb = notebook_of(page);
    if (nb != NULL) {
        GtkRoot* root = gtk_widget_get_root(page);
        GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
        if (focus != NULL && gtk_widget_is_ancestor(focus, page)) gtk_root_set_focus(root, NULL);
        lt->hidden_at = gtk_notebook_page_num(nb, page);
        gtk_notebook_remove_page(nb, lt->hidden_at);
    }
    return page;
}

void launcher_tab_workspace_hidden(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    GPtrArray* pages = preview_pages(app);
    if (pages->len > 0 && ws->launcher_pages == NULL) {
        ws->launcher_pages = g_ptr_array_new_with_free_func(g_object_unref);
    }
    for (guint i = 0; i < pages->len; i++) {
        g_ptr_array_add(ws->launcher_pages, detach_page(tab_of(g_ptr_array_index(pages, i))));
    }
    g_ptr_array_unref(pages);
}

void launcher_tab_workspace_shown(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || ws->launcher_pages == NULL) return;
    GPtrArray* pages = ws->launcher_pages;
    ws->launcher_pages = NULL;
    if (app->notebook != NULL) {
        GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
        for (guint i = 0; i < pages->len; i++) {
            GtkWidget* page = g_ptr_array_index(pages, i);
            LauncherTab* lt = tab_of(page);
            if (lt == NULL || lt->label == NULL) continue;
            gint at = MIN(lt->hidden_at, gtk_notebook_get_n_pages(nb));
            gtk_notebook_insert_page(nb, page, lt->label, at);
            gtk_notebook_set_tab_reorderable(nb, page, TRUE);
            gtk_notebook_set_tab_detachable(nb, page, TRUE);
        }
    }
    g_ptr_array_unref(pages);
}

void launcher_tab_workspace_removed(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces && ws == &app->workspaces[app->current_ws]) {
        GPtrArray* pages = preview_pages(app);
        for (guint i = 0; i < pages->len; i++) g_object_unref(detach_page(tab_of(g_ptr_array_index(pages, i))));
        g_ptr_array_unref(pages);
    }
    g_clear_pointer(&ws->launcher_pages, g_ptr_array_unref);
}
