/* Editor tab menus: the menu of an editor tab and of a movable page, and
 * the clicks on a tab title (see editor.h) */
#include "editor.h"
#include "editor_private.h"
#include "editor_modes.h"
#include "i18n.h"
#include "diff_gutter.h"

typedef struct {
    AppData* app;
    EditorTab* tab;
    GtkWidget* popover;
} EditorTabMenuData;

static void on_tab_menu_action(GtkWidget* widget, gpointer data) {
    (void)widget;
    gint action = GPOINTER_TO_INT(data);
    EditorTabMenuData* md = (EditorTabMenuData*)g_object_get_data(G_OBJECT(widget), "tab-menu-data");
    if (md == NULL || md->app == NULL || md->tab == NULL) return;
    AppData* app = md->app;
    EditorTab* tab = md->tab;
    GtkWidget* popover = md->popover;
    if (popover != NULL) {
        gtk_popover_popdown(GTK_POPOVER(popover));
    }
    switch (action) {
    case TAB_ACTION_MOVE_RIGHT:
        editor_move_tab_direction(app, tab, EDITOR_MOVE_RIGHT);
        break;
    case TAB_ACTION_MOVE_LEFT:
        editor_move_tab_direction(app, tab, EDITOR_MOVE_LEFT);
        break;
    case TAB_ACTION_MOVE_UP:
        editor_move_tab_direction(app, tab, EDITOR_MOVE_UP);
        break;
    case TAB_ACTION_MOVE_DOWN:
        editor_move_tab_direction(app, tab, EDITOR_MOVE_DOWN);
        break;
    case TAB_ACTION_SPLIT_RIGHT:
        editor_split_tab(app, tab, EDITOR_MOVE_RIGHT);
        break;
    case TAB_ACTION_SPLIT_DOWN:
        editor_split_tab(app, tab, EDITOR_MOVE_DOWN);
        break;
    case TAB_ACTION_LINE_NUMBERS:
        editor_toggle_line_numbers(app, tab);
        break;
    case TAB_ACTION_WRAP:
        editor_mode_toggle_wrap(tab);
        break;
    case TAB_ACTION_CLOSE:
        editor_close_tab(app, tab);
        break;
    case TAB_ACTION_MODE_NOTEPAD:
        editor_set_tab_mode(app, tab, EDITOR_MODE_NOTEPAD);
        break;
    case TAB_ACTION_MODE_EMACS:
        editor_set_tab_mode(app, tab, EDITOR_MODE_EMACS);
        break;
    case TAB_ACTION_MODE_VIM:
        editor_set_tab_mode(app, tab, EDITOR_MODE_VIM);
        break;
    default:
        break;
    }
}

static void tab_menu_item(GtkWidget* box, GtkWidget* popover, AppData* app, EditorTab* tab,
                          const gchar* label, const gchar* shortcut, gint action) {
    GtkWidget* btn = gtk_button_new();
    gtk_widget_set_name(btn, "tab-context-btn");
    gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);

    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(row, TRUE);
    GtkWidget* lbl = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_box_append(GTK_BOX(row), lbl);

    if (shortcut != NULL) {
        GtkWidget* sc = gtk_label_new(shortcut);
        gtk_widget_add_css_class(sc, "shortcut-badge");
        gtk_box_append(GTK_BOX(row), sc);
    }

    gtk_button_set_child(GTK_BUTTON(btn), row);

    EditorTabMenuData* md = g_new0(EditorTabMenuData, 1);
    md->app = app;
    md->tab = tab;
    md->popover = popover;
    g_object_set_data_full(G_OBJECT(btn), "tab-menu-data", md, g_free);

    g_signal_connect(btn, "clicked", G_CALLBACK(on_tab_menu_action), GINT_TO_POINTER(action));
    gtk_box_append(GTK_BOX(box), btn);
}

/* A mode entry of the tab menu: a leading dot marks the tab's mode (the dot
 * of the other entries is transparent so the names stay aligned) */
static void tab_menu_mode_item(GtkWidget* box, GtkWidget* popover, AppData* app, EditorTab* tab,
                               const gchar* name, gboolean current, gint action) {
    tab_menu_item(box, popover, app, tab, name, NULL, action);
    GtkWidget* btn = gtk_widget_get_last_child(box);
    GtkWidget* row = gtk_button_get_child(GTK_BUTTON(btn));
    GtkWidget* dot = gtk_label_new("\u25CF");
    gtk_widget_set_opacity(dot, current ? 1.0 : 0.0);
    gtk_box_prepend(GTK_BOX(row), dot);
    gtk_box_set_spacing(GTK_BOX(row), 6);
    if (current) {
        gtk_widget_add_css_class(btn, "menu-item-current");
    }
}

/* Whether moving a tab of cur_nb in direction dir does something: the same
 * rules as editor_move_tab_direction() and editor_move_page_direction() */
static gboolean notebook_can_move_tab(AppData* app, GtkNotebook* cur_nb, EditorMoveDirection dir) {
    if (cur_nb == NULL) return FALSE;
    if (GTK_WIDGET(cur_nb) == app->notebook) return dir != EDITOR_MOVE_LEFT;
    if (dir == EDITOR_MOVE_LEFT || editor_neighbor_notebook(app, cur_nb, dir) != NULL) return TRUE;
    /* Otherwise the move splits the notebook, which needs another tab to stay */
    return gtk_notebook_get_n_pages(cur_nb) > 1;
}

/* The move items of a tab menu, by direction */
static const struct {
    const char* name;
    const char* key;
    EditorMoveDirection dir;
    gint action;
} TAB_MOVES[] = {
    {NC_("editor tab", "Move Right"), "Ctrl+Alt+Right", EDITOR_MOVE_RIGHT, TAB_ACTION_MOVE_RIGHT},
    {NC_("editor tab", "Move Left"), "Ctrl+Alt+Left", EDITOR_MOVE_LEFT, TAB_ACTION_MOVE_LEFT},
    {NC_("editor tab", "Move Down"), "Ctrl+Alt+Down", EDITOR_MOVE_DOWN, TAB_ACTION_MOVE_DOWN},
    {NC_("editor tab", "Move Up"), "Ctrl+Alt+Up", EDITOR_MOVE_UP, TAB_ACTION_MOVE_UP},
};

void editor_tab_show_context_menu(AppData* app, EditorTab* tab, GtkWidget* target_widget) {
    if (app == NULL || tab == NULL || target_widget == NULL) return;

    GtkWidget* popover = gtk_popover_new();
    /* The expanding menu rows must not make the tab expand to the strip width */
    gtk_widget_set_hexpand(popover, FALSE);
    gtk_widget_set_vexpand(popover, FALSE);
    gtk_widget_set_parent(popover, target_widget);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), TRUE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(box, 4);
    gtk_widget_set_margin_end(box, 4);
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);

    /* Only the moves that would do something */
    GtkNotebook* cur_nb = editor_tab_notebook(app, tab);
    gboolean any_move = FALSE;
    for (gsize i = 0; i < G_N_ELEMENTS(TAB_MOVES); i++) {
        if (!notebook_can_move_tab(app, cur_nb, TAB_MOVES[i].dir)) continue;
        tab_menu_item(box, popover, app, tab, g_dpgettext2(GETTEXT_PACKAGE, "editor tab", TAB_MOVES[i].name),
                      TAB_MOVES[i].key, TAB_MOVES[i].action);
        any_move = TRUE;
    }

    if (any_move) {
        GtkWidget* sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_set_margin_top(sep2, 4);
        gtk_widget_set_margin_bottom(sep2, 4);
        gtk_box_append(GTK_BOX(box), sep2);
    }

    /* The only place where a tab's mode changes: the current one is marked */
    gboolean org = (tab->mode == EDITOR_MODE_ORGMODE);
    GtkWidget* mode_lbl = gtk_label_new(_("Tab Mode"));
    gtk_widget_add_css_class(mode_lbl, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(mode_lbl), 0.0f);
    gtk_widget_set_margin_start(mode_lbl, 8);
    gtk_widget_set_margin_bottom(mode_lbl, 2);
    gtk_box_append(GTK_BOX(box), mode_lbl);
    tab_menu_mode_item(box, popover, app, tab, "Notepad", tab->mode == EDITOR_MODE_NOTEPAD,
                       TAB_ACTION_MODE_NOTEPAD);
    tab_menu_mode_item(box, popover, app, tab, org ? "Emacs (Org-mode)" : "Emacs",
                       tab->mode == EDITOR_MODE_EMACS || org, TAB_ACTION_MODE_EMACS);
    tab_menu_mode_item(box, popover, app, tab, "Vim", tab->mode == EDITOR_MODE_VIM, TAB_ACTION_MODE_VIM);

    GtkWidget* sep3 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep3, 4);
    gtk_widget_set_margin_bottom(sep3, 4);
    gtk_box_append(GTK_BOX(box), sep3);

    tab_menu_item(box, popover, app, tab,
                  diff_gutter_get_line_numbers(tab->view) ? _("Hide Line Numbers") : _("Show Line Numbers"),
                  "Ctrl+Alt+N", TAB_ACTION_LINE_NUMBERS);
    tab_menu_item(box, popover, app, tab, editor_mode_get_wrap(tab) ? _("Don't Wrap Lines") : _("Wrap Lines"),
                  NULL, TAB_ACTION_WRAP);

    GtkWidget* sep_split = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep_split, 4);
    gtk_widget_set_margin_bottom(sep_split, 4);
    gtk_box_append(GTK_BOX(box), sep_split);
    /* The keys of the tab's mode (Notepad has none) */
    gboolean vim = (tab->mode == EDITOR_MODE_VIM);
    gboolean emacs = (tab->mode == EDITOR_MODE_EMACS || org);
    tab_menu_item(box, popover, app, tab, _("Split Right"), vim ? "Ctrl+W v" : emacs ? "C-x 3" : NULL,
                  TAB_ACTION_SPLIT_RIGHT);
    tab_menu_item(box, popover, app, tab, _("Split Down"), vim ? "Ctrl+W s" : emacs ? "C-x 2" : NULL,
                  TAB_ACTION_SPLIT_DOWN);

    GtkWidget* sep4 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep4, 4);
    gtk_widget_set_margin_bottom(sep4, 4);
    gtk_box_append(GTK_BOX(box), sep4);

    tab_menu_item(box, popover, app, tab, _("Close Tab"), "Ctrl+W", TAB_ACTION_CLOSE);

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect_swapped(popover, "closed", G_CALLBACK(gtk_widget_unparent), popover);
    gtk_popover_popup(GTK_POPOVER(popover));
}

static void on_tab_label_right_click(GtkGestureClick* gesture, gint n_press,
                                     gdouble x, gdouble y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    EditorTab* tab = (EditorTab*)user_data;
    if (tab != NULL && tab->app != NULL && tab->tab_label != NULL) {
        editor_tab_show_context_menu(tab->app, tab, tab->tab_label);
    }
}

static void on_tab_label_left_click(GtkGestureClick* gesture, gint n_press,
                                    gdouble x, gdouble y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    EditorTab* tab = (EditorTab*)user_data;
    if (tab != NULL && tab->app != NULL) {
        AppData* app = tab->app;
        app->last_focused_editor = tab;
        if (tab->view != NULL) {
            gtk_widget_grab_focus(tab->view);
        }
    }
}

static void on_tab_label_middle_click(GtkGestureClick* gesture, gint n_press,
                                      gdouble x, gdouble y, gpointer user_data) {
    (void)n_press; (void)x; (void)y;
    EditorTab* tab = (EditorTab*)user_data;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    if (tab != NULL && tab->app != NULL) {
        editor_close_tab(tab->app, tab);
    }
}
/* Right click: the tab menu; left click: focus the tab; middle click: close it */
void editor_tab_label_connect(EditorTab* tab) {
    GtkGesture* rclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclick), GDK_BUTTON_SECONDARY);
    g_signal_connect(rclick, "pressed", G_CALLBACK(on_tab_label_right_click), tab);
    gtk_widget_add_controller(tab->tab_label, GTK_EVENT_CONTROLLER(rclick));

    GtkGesture* lclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(lclick), GDK_BUTTON_PRIMARY);
    g_signal_connect(lclick, "pressed", G_CALLBACK(on_tab_label_left_click), tab);
    gtk_widget_add_controller(tab->tab_label, GTK_EVENT_CONTROLLER(lclick));

    GtkGesture* mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(mclick, "released", G_CALLBACK(on_tab_label_middle_click), tab);
    gtk_widget_add_controller(tab->tab_label, GTK_EVENT_CONTROLLER(mclick));
}
/* The tab menu of a movable page */
typedef struct {
    AppData* app;
    GtkWidget* child;
    GtkWidget* popover;
    EditorPageCloseFunc on_close;
    gpointer data;
} PageMenu;

static void page_menu_free(gpointer data) {
    PageMenu* pm = data;
    g_object_unref(pm->child);
    g_free(pm);
}

static void on_page_menu_action(GtkButton* button, gpointer data) {
    gint action = GPOINTER_TO_INT(data);
    PageMenu* pm = g_object_get_data(G_OBJECT(gtk_widget_get_parent(GTK_WIDGET(button))), "page-menu");
    if (pm == NULL) return;
    AppData* app = pm->app;
    GtkWidget* child = g_object_ref(pm->child);
    EditorPageCloseFunc on_close = pm->on_close;
    gpointer close_data = pm->data;
    gtk_popover_popdown(GTK_POPOVER(pm->popover));
    switch (action) {
    case TAB_ACTION_MOVE_RIGHT: editor_move_page_direction(app, child, EDITOR_MOVE_RIGHT); break;
    case TAB_ACTION_MOVE_LEFT: editor_move_page_direction(app, child, EDITOR_MOVE_LEFT); break;
    case TAB_ACTION_MOVE_DOWN: editor_move_page_direction(app, child, EDITOR_MOVE_DOWN); break;
    case TAB_ACTION_MOVE_UP: editor_move_page_direction(app, child, EDITOR_MOVE_UP); break;
    case TAB_ACTION_CLOSE:
        if (on_close != NULL) on_close(child, close_data);
        break;
    default: break;
    }
    g_object_unref(child);
}

static void page_menu_item(GtkWidget* box, const gchar* label, const gchar* shortcut, gint action) {
    GtkWidget* btn = gtk_button_new();
    gtk_widget_set_name(btn, "tab-context-btn");
    gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(row, TRUE);
    GtkWidget* lbl = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_box_append(GTK_BOX(row), lbl);
    if (shortcut != NULL) {
        GtkWidget* sc = gtk_label_new(shortcut);
        gtk_widget_add_css_class(sc, "shortcut-badge");
        gtk_box_append(GTK_BOX(row), sc);
    }
    gtk_button_set_child(GTK_BUTTON(btn), row);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_page_menu_action), GINT_TO_POINTER(action));
    gtk_box_append(GTK_BOX(box), btn);
}

void editor_page_show_context_menu(AppData* app, GtkWidget* child, GtkWidget* target_widget,
                                   EditorPageCloseFunc on_close, gpointer data) {
    if (app == NULL || child == NULL || target_widget == NULL) return;
    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_hexpand(popover, FALSE);
    gtk_widget_set_vexpand(popover, FALSE);
    gtk_widget_set_parent(popover, target_widget);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), TRUE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(box, 4);
    gtk_widget_set_margin_end(box, 4);
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);
    PageMenu* pm = g_new0(PageMenu, 1);
    pm->app = app;
    pm->child = g_object_ref(child);
    pm->popover = popover;
    pm->on_close = on_close;
    pm->data = data;
    g_object_set_data_full(G_OBJECT(box), "page-menu", pm, page_menu_free);

    /* Only the moves that would do something (none in the terminal panel) */
    GtkNotebook* cur_nb = editor_notebook_of_page(child);
    gboolean movable = cur_nb != NULL &&
        (GTK_WIDGET(cur_nb) == app->notebook ||
         (app->editor_box != NULL && gtk_widget_is_ancestor(GTK_WIDGET(cur_nb), app->editor_box)));
    gboolean any_move = FALSE;
    for (gsize i = 0; movable && i < G_N_ELEMENTS(TAB_MOVES); i++) {
        if (!notebook_can_move_tab(app, cur_nb, TAB_MOVES[i].dir)) continue;
        page_menu_item(box, g_dpgettext2(GETTEXT_PACKAGE, "editor tab", TAB_MOVES[i].name), TAB_MOVES[i].key,
                       TAB_MOVES[i].action);
        any_move = TRUE;
    }
    if (on_close != NULL) {
        if (any_move) {
            GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
            gtk_widget_set_margin_top(sep, 4);
            gtk_widget_set_margin_bottom(sep, 4);
            gtk_box_append(GTK_BOX(box), sep);
        }
        page_menu_item(box, _("Close Tab"), "Ctrl+W", TAB_ACTION_CLOSE);
    }

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect_swapped(popover, "closed", G_CALLBACK(gtk_widget_unparent), popover);
    gtk_popover_popup(GTK_POPOVER(popover));
}
