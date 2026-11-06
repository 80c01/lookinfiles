#include "editor.h"
#include "editor_modes.h"
#include "dialog.h"
#include "panels.h"
#include "app_events.h"
#include "file_types.h"
#include "text_encoding.h"
#include "minimap.h"
#include "diff_gutter.h"
#include "pane_geometry.h"
#include "tab_drop.h"
#include "focus_route.h"
#include "nav_history.h"
#include "editor_view.h"
#include "icons.h"

static void collect_editor_notebooks_recursive(GtkWidget* widget, GPtrArray* array) {
    if (widget == NULL) return;
    if (GTK_IS_NOTEBOOK(widget)) {
        g_ptr_array_add(array, widget);
    } else if (GTK_IS_PANED(widget)) {
        collect_editor_notebooks_recursive(gtk_paned_get_start_child(GTK_PANED(widget)), array);
        collect_editor_notebooks_recursive(gtk_paned_get_end_child(GTK_PANED(widget)), array);
    } else if (GTK_IS_BOX(widget)) {
        for (GtkWidget* child = gtk_widget_get_first_child(widget);
             child != NULL;
             child = gtk_widget_get_next_sibling(child)) {
            collect_editor_notebooks_recursive(child, array);
        }
    }
}

GPtrArray* editor_get_all_notebooks(AppData* app) {
    GPtrArray* array = g_ptr_array_new();
    if (app != NULL && app->editor_box != NULL) {
        collect_editor_notebooks_recursive(app->editor_box, array);
    }
    return array;
}

static void on_tab_dropped(GtkNotebook* target, GtkWidget* child, PaneZone zone,
                           gint index, gpointer data);
static void on_tab_drag_done(GtkNotebook* source, gpointer data);
static EditorTab* editor_tab_of_page(AppData* app, GtkWidget* child);
static GtkNotebook* editor_tab_notebook(AppData* app, EditorTab* tab);

/* Editor tabs can be reordered and dragged to another notebook */
static void editor_page_set_draggable(GtkNotebook* nb, GtkWidget* child) {
    gtk_notebook_set_tab_reorderable(nb, child, TRUE);
    gtk_notebook_set_tab_detachable(nb, child, TRUE);
}

/* Editor notebooks take the tabs of their group, and terminal tabs */
static gboolean editor_accepts_tab(GtkNotebook* target, GtkNotebook* source, GtkWidget* child,
                                   gpointer data) {
    (void)data;
    if (g_strcmp0(gtk_notebook_get_group_name(target), gtk_notebook_get_group_name(source)) == 0) {
        return TRUE;
    }
#ifdef HAVE_VTE
    return VTE_IS_TERMINAL(child);
#else
    (void)child;
    return FALSE;
#endif
}

/* Shift+drop: the dragged editor tab and the one shown in target exchange
 * notebooks, each taking the other's position */
static gboolean editor_swap_tabs(GtkNotebook* target, GtkWidget* child, gboolean do_it,
                                 gpointer data) {
    AppData* app = (AppData*)data;
    EditorTab* a = editor_tab_of_page(app, child);
    gint cur = gtk_notebook_get_current_page(target);
    GtkWidget* other = cur >= 0 ? gtk_notebook_get_nth_page(target, cur) : NULL;
    EditorTab* b = other != NULL ? editor_tab_of_page(app, other) : NULL;
    if (a == NULL || b == NULL || a == b) return FALSE;
    GtkNotebook* source = editor_tab_notebook(app, a);
    if (source == NULL || source == target) return FALSE;
    if (!do_it) return TRUE;

    gint pos_a = gtk_notebook_page_num(source, a->container);
    gint pos_b = gtk_notebook_page_num(target, b->container);
    editor_move_tab_to(app, a, target, pos_b, FALSE);
    editor_move_tab_to(app, b, source, pos_a, FALSE);
    app->last_focused_editor = a;
    if (a->view != NULL) gtk_widget_grab_focus(a->view);
    return TRUE;
}

void editor_notebook_enable_tab_drop(AppData* app, GtkWidget* nb) {
    if (app == NULL || nb == NULL) return;
    /* The preview notebook only takes tabs; editor notebooks also split */
    tab_drop_attach(GTK_NOTEBOOK(nb), nb != app->notebook, on_tab_dropped,
                    on_tab_drag_done, app);
    tab_drop_set_accept(GTK_NOTEBOOK(nb), editor_accepts_tab);
    tab_drop_set_swap(GTK_NOTEBOOK(nb), editor_swap_tabs);
}

GtkWidget* editor_notebook_new(AppData* app) {
    GtkWidget* nb = gtk_notebook_new();
    gtk_widget_set_name(nb, "editor-notebook");
    gtk_widget_set_hexpand(nb, TRUE);
    gtk_widget_set_vexpand(nb, TRUE);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(nb), TRUE);
    gtk_notebook_set_group_name(GTK_NOTEBOOK(nb), "editor-tabs");

    g_signal_connect(nb, "switch-page",
                     G_CALLBACK(on_editor_switch_page_changed), app);
    g_signal_connect(nb, "page-reordered",
                     G_CALLBACK(on_notebook_page_reordered), app);
    editor_notebook_enable_tab_drop(app, nb);
    return nb;
}

static GtkNotebook* editor_target_notebook(AppData* app) {
    if (app == NULL) {
        return NULL;
    }
    if (app->last_focused_editor != NULL && app->last_focused_editor->nb != NULL) {
        for (GList* l = app->editors; l != NULL; l = l->next) {
            if (l->data == app->last_focused_editor) {
                return GTK_NOTEBOOK(app->last_focused_editor->nb);
            }
        }
        app->last_focused_editor = NULL;
    }
    if (app->editor_box != NULL && gtk_widget_get_visible(app->editor_box)) {
        if (app->editor_notebook != NULL) {
            return GTK_NOTEBOOK(app->editor_notebook);
        }
        GPtrArray* arr = editor_get_all_notebooks(app);
        if (arr->len > 0) {
            GtkNotebook* first = GTK_NOTEBOOK(g_ptr_array_index(arr, 0));
            g_ptr_array_unref(arr);
            return first;
        }
        g_ptr_array_unref(arr);
    }
    return (app->notebook != NULL) ? GTK_NOTEBOOK(app->notebook) : NULL;
}

static GtkNotebook* editor_tab_notebook(AppData* app, EditorTab* tab) {
    if (tab != NULL && tab->nb != NULL) {
        return GTK_NOTEBOOK(tab->nb);
    }
    return editor_target_notebook(app);
}

static void editor_cleanup_notebook_tree(AppData* app, GtkWidget* nb_widget, GtkWidget* parent);

/* Remove an empty editor notebook and the split that held it */
static void editor_cleanup_notebook(AppData* app, GtkWidget* nb_widget) {
    if (app == NULL || nb_widget == NULL || nb_widget == app->notebook) return;

    GtkWidget* parent = gtk_widget_get_parent(nb_widget);
    if (parent == NULL) return;

    /* A GtkPaned losing the child it last focused warns when the window focus
     * is elsewhere (e.g. after a tab drag, which focuses the source notebook):
     * clear the focus while the splits are rebuilt, then restore it */
    GtkRoot* root = gtk_widget_get_root(nb_widget);
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    if (focus != NULL) {
        g_object_ref(focus);
        gtk_root_set_focus(root, NULL);
    }
    editor_cleanup_notebook_tree(app, nb_widget, parent);
    if (focus != NULL) {
        if (gtk_widget_get_root(focus) == root) {
            gtk_root_set_focus(root, focus);
        }
        g_object_unref(focus);
    }
}

static void editor_cleanup_notebook_tree(AppData* app, GtkWidget* nb_widget, GtkWidget* parent) {
    if (GTK_IS_BOX(parent)) {
        gtk_box_remove(GTK_BOX(parent), nb_widget);
        if (app->editor_notebook == nb_widget) {
            app->editor_notebook = NULL;
        }
    } else if (GTK_IS_PANED(parent)) {
        GtkPaned* paned = GTK_PANED(parent);
        GtkWidget* sibling = (gtk_paned_get_start_child(paned) == nb_widget) ?
                             gtk_paned_get_end_child(paned) :
                             gtk_paned_get_start_child(paned);
        GtkWidget* grandparent = gtk_widget_get_parent(parent);

        g_object_ref(sibling);
        gtk_paned_set_start_child(paned, NULL);
        gtk_paned_set_end_child(paned, NULL);

        if (GTK_IS_BOX(grandparent)) {
            gtk_box_remove(GTK_BOX(grandparent), parent);
            gtk_box_append(GTK_BOX(grandparent), sibling);
        } else if (GTK_IS_PANED(grandparent)) {
            if (gtk_paned_get_start_child(GTK_PANED(grandparent)) == parent) {
                gtk_paned_set_start_child(GTK_PANED(grandparent), sibling);
            } else {
                gtk_paned_set_end_child(GTK_PANED(grandparent), sibling);
            }
        }
        g_object_unref(sibling);

        if (app->editor_notebook == nb_widget) {
            GPtrArray* arr = editor_get_all_notebooks(app);
            app->editor_notebook = (arr->len > 0) ? GTK_WIDGET(g_ptr_array_index(arr, 0)) : NULL;
            g_ptr_array_unref(arr);
        }
    }
}

static void editor_update_panel_visibilities(AppData* app) {
    if (app == NULL) return;
    gint dedicated_count = 0;
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = (EditorTab*)l->data;
        if (t != NULL && t->nb != NULL && t->nb != app->notebook) {
            dedicated_count++;
        }
    }
    /* Other tabs (terminals) keep the panel too */
    GPtrArray* nbs = editor_get_all_notebooks(app);
    for (guint i = 0; i < nbs->len && dedicated_count == 0; i++) {
        dedicated_count += gtk_notebook_get_n_pages(GTK_NOTEBOOK(g_ptr_array_index(nbs, i)));
    }
    g_ptr_array_unref(nbs);
    gboolean dedicated = (dedicated_count > 0);
    app->editor_dedicated = dedicated;
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
        app->workspaces[app->current_ws].editor_dedicated = dedicated;
    }
    if (app->editor_box != NULL) {
        gtk_widget_set_visible(app->editor_box, dedicated);
    }
    /* The dedicated panel was the only one shown (explorer hidden while
     * editing): bring the explorer back rather than leave an empty window */
    if (!dedicated && !app->switching_ws && app->left_vbox != NULL &&
        !gtk_widget_get_visible(app->left_vbox) &&
        (app->viewer_box == NULL || !gtk_widget_get_visible(app->viewer_box)) &&
        (app->term_scroll == NULL || !gtk_widget_get_visible(app->term_scroll))) {
        app->applying_layout = TRUE;
        gtk_widget_set_visible(app->left_vbox, TRUE);
        if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
            app->workspaces[app->current_ws].files_vis = TRUE;
        }
        if (app->tree_view != NULL) {
            gtk_widget_grab_focus(app->tree_view);
        }
    }
    if (app->editor_paned != NULL) {
        gboolean preview_vis = (app->viewer_box != NULL && gtk_widget_get_visible(app->viewer_box));
        gtk_widget_set_visible(app->editor_paned, preview_vis || dedicated);
    }
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
}

static void sync_editors_order_from_notebook(AppData* app, GtkNotebook* nb) {
    if (app == NULL || nb == NULL || app->editors == NULL) {
        return;
    }
    gint n_pages = gtk_notebook_get_n_pages(nb);
    for (gint p = 0; p < n_pages; p++) {
        GtkWidget* child = gtk_notebook_get_nth_page(nb, p);
        if (child == NULL) continue;
        for (GList* l = app->editors; l != NULL; l = l->next) {
            EditorTab* tab = (EditorTab*)l->data;
            if (tab != NULL && tab->container == child) {
                tab->nb = GTK_WIDGET(nb);
                break;
            }
        }
    }
}

static void on_notebook_page_reordered_impl(GtkNotebook* notebook, GtkWidget* child,
                                           guint page_num, gpointer data) {
    (void)child;
    (void)page_num;
    sync_editors_order_from_notebook((AppData*)data, notebook);
}

void on_notebook_page_reordered(GtkNotebook* notebook, GtkWidget* child,
                                guint page_num, gpointer data) {
    on_notebook_page_reordered_impl(notebook, child, page_num, data);
}

/* Show or hide the line numbers of tab (NULL: the current editor tab) */
void editor_toggle_line_numbers(AppData* app, EditorTab* tab) {
    if (tab == NULL) {
        tab = editor_current_tab(app);
    }
    if (tab == NULL || tab->view == NULL) {
        return;
    }
    gboolean shown = diff_gutter_get_line_numbers(tab->view);
    diff_gutter_set_line_numbers(tab->view, shown ? -1 : 0);
}

EditorTab* editor_current_tab(AppData* app) {
    if (app == NULL || app->editors == NULL) {
        return NULL;
    }
    if (app->last_focused_editor != NULL) {
        for (GList* l = app->editors; l != NULL; l = l->next) {
            if (l->data == app->last_focused_editor) {
                return (EditorTab*)l->data;
            }
        }
        app->last_focused_editor = NULL;
    }

    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = (EditorTab*)l->data;
        if (t != NULL && t->view != NULL && gtk_widget_has_focus(t->view)) {
            app->last_focused_editor = t;
            return t;
        }
    }

    if (app->editor_box != NULL && gtk_widget_get_visible(app->editor_box)) {
        GPtrArray* nbs = editor_get_all_notebooks(app);
        for (guint i = 0; i < nbs->len; i++) {
            GtkNotebook* nb = GTK_NOTEBOOK(g_ptr_array_index(nbs, i));
            gint p = gtk_notebook_get_current_page(nb);
            if (p >= 0) {
                GtkWidget* child = gtk_notebook_get_nth_page(nb, p);
                for (GList* l = app->editors; l != NULL; l = l->next) {
                    EditorTab* t = (EditorTab*)l->data;
                    if (t != NULL && t->container == child) {
                        g_ptr_array_unref(nbs);
                        return t;
                    }
                }
            }
        }
        g_ptr_array_unref(nbs);
    }

    if (app->notebook != NULL) {
        /* Pages other than editor tabs (Preview, Git, References) match no tab */
        gint p = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
        if (p >= 0) {
            GtkWidget* child = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), p);
            for (GList* l = app->editors; l != NULL; l = l->next) {
                EditorTab* t = (EditorTab*)l->data;
                if (t != NULL && t->container == child) {
                    return t;
                }
            }
        }
    }

    return (EditorTab*)app->editors->data;
}


void on_editor_modified_changed(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL && tab->is_buffer && tab->path != NULL) {
        /* Automatically back buffer to ~/.config file so user doesn't lose changes */
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(tab->buffer, &start, &end);
        gchar* text = gtk_text_buffer_get_text(tab->buffer, &start, &end, TRUE);
        g_file_set_contents(tab->path, text, -1, NULL);
        g_free(text);
        gtk_text_buffer_set_modified(tab->buffer, FALSE);
    }
    editor_tab_label_update(tab);
    /* Mode line flags (Emacs -U:**-, Vim [+]) follow saves right away */
    editor_modes_update_status(tab);
}

void editor_apply_language(GtkTextBuffer* buffer, const gchar* name) {
#ifdef HAVE_GSV
    GtkSourceLanguageManager* mgr = gtk_source_language_manager_get_default();
    gchar* ctype = g_content_type_guess(name, NULL, 0, NULL);
    GtkSourceLanguage* lang =
        gtk_source_language_manager_guess_language(mgr, name, ctype);
    g_free(ctype);
    gtk_source_buffer_set_language(GTK_SOURCE_BUFFER(buffer), lang);
    gtk_source_buffer_set_highlight_syntax(GTK_SOURCE_BUFFER(buffer),
                                           lang != NULL);
#else
    (void)buffer;
    (void)name;
#endif
}

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

    tab_menu_item(box, popover, app, tab, "Move Right", "Ctrl+Alt+Right", TAB_ACTION_MOVE_RIGHT);
    tab_menu_item(box, popover, app, tab, "Move Left", "Ctrl+Alt+Left", TAB_ACTION_MOVE_LEFT);
    tab_menu_item(box, popover, app, tab, "Move Down", "Ctrl+Alt+Down", TAB_ACTION_MOVE_DOWN);
    tab_menu_item(box, popover, app, tab, "Move Up", "Ctrl+Alt+Up", TAB_ACTION_MOVE_UP);

    GtkWidget* sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep2, 4);
    gtk_widget_set_margin_bottom(sep2, 4);
    gtk_box_append(GTK_BOX(box), sep2);

    /* The only place where a tab's mode changes: the current one is marked */
    gboolean org = (tab->mode == EDITOR_MODE_ORGMODE);
    GtkWidget* mode_lbl = gtk_label_new("Tab Mode");
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
                  diff_gutter_get_line_numbers(tab->view) ? "Hide Line Numbers" : "Show Line Numbers",
                  "Ctrl+Alt+N", TAB_ACTION_LINE_NUMBERS);
    tab_menu_item(box, popover, app, tab, editor_mode_get_wrap(tab) ? "Don't Wrap Lines" : "Wrap Lines",
                  NULL, TAB_ACTION_WRAP);

    GtkWidget* sep_split = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep_split, 4);
    gtk_widget_set_margin_bottom(sep_split, 4);
    gtk_box_append(GTK_BOX(box), sep_split);
    /* The keys of the tab's mode (Notepad has none) */
    gboolean vim = (tab->mode == EDITOR_MODE_VIM);
    gboolean emacs = (tab->mode == EDITOR_MODE_EMACS || org);
    tab_menu_item(box, popover, app, tab, "Split Right", vim ? "Ctrl+W v" : emacs ? "C-x 3" : NULL,
                  TAB_ACTION_SPLIT_RIGHT);
    tab_menu_item(box, popover, app, tab, "Split Down", vim ? "Ctrl+W s" : emacs ? "C-x 2" : NULL,
                  TAB_ACTION_SPLIT_DOWN);

    GtkWidget* sep4 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep4, 4);
    gtk_widget_set_margin_bottom(sep4, 4);
    gtk_box_append(GTK_BOX(box), sep4);

    tab_menu_item(box, popover, app, tab, "Close Tab", "Ctrl+W", TAB_ACTION_CLOSE);

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect_swapped(popover, "closed", G_CALLBACK(gtk_widget_unparent), popover);
    gtk_popover_popup(GTK_POPOVER(popover));
}

static void on_tab_label_right_click(GtkGestureClick* gesture, gint n_press,
                                     gdouble x, gdouble y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    EditorTab* tab = (EditorTab*)user_data;
    if (tab != NULL && tab->app != NULL && tab->tab_label != NULL) {
        editor_tab_show_context_menu((AppData*)tab->app, tab, tab->tab_label);
    }
}

static void on_tab_label_left_click(GtkGestureClick* gesture, gint n_press,
                                    gdouble x, gdouble y, gpointer user_data) {
    (void)gesture; (void)n_press; (void)x; (void)y;
    EditorTab* tab = (EditorTab*)user_data;
    if (tab != NULL && tab->app != NULL) {
        AppData* app = (AppData*)tab->app;
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
        editor_close_tab((AppData*)tab->app, tab);
    }
}

/* GTK selects the neighbour page when the current one is removed: when that
 * lands on the Git tab of the preview notebook (no editor tab left beside
 * it), show the preview instead */
static void editor_after_current_removed(AppData* app, GtkNotebook* nb, gboolean was_current) {
    if (was_current && GTK_WIDGET(nb) == app->notebook && app->git_tab_box != NULL &&
        gtk_notebook_get_current_page(nb) == gtk_notebook_page_num(nb, app->git_tab_box)) {
        gtk_notebook_set_current_page(nb, 0);
    }
}

/* During a tab drag GTK still uses the notebook left, so
 * editor_cleanup_after_drop() removes it later (cleanup FALSE) */
void editor_move_tab_to(AppData* app, EditorTab* tab, GtkNotebook* new_nb, gint target_pos,
                        gboolean cleanup) {
    if (app == NULL || tab == NULL || new_nb == NULL) return;
    GtkNotebook* old_nb = editor_tab_notebook(app, tab);
    if (old_nb == new_nb) return;

    gint old_page = gtk_notebook_page_num(old_nb, tab->container);
    if (old_page < 0 && old_nb != GTK_NOTEBOOK(app->notebook) && app->notebook != NULL) {
        old_nb = GTK_NOTEBOOK(app->notebook);
        old_page = gtk_notebook_page_num(old_nb, tab->container);
    }
    if (old_page < 0) return;

    g_object_ref(tab->container);
    g_object_ref(tab->tab_label);

    /* The focus may pass through the old notebook's next page meanwhile */
    focus_route_hold_history(app, TRUE);
    gboolean was_current = (gtk_notebook_get_current_page(old_nb) == old_page);
    if (gtk_notebook_get_tab_detachable(old_nb, tab->container)) {
        gtk_notebook_detach_tab(old_nb, tab->container);
    } else {
        gtk_notebook_remove_page(old_nb, old_page);
    }
    editor_after_current_removed(app, old_nb, was_current);

    gint new_page = -1;
    if (target_pos < 0) {
        new_page = gtk_notebook_append_page(new_nb, tab->container, tab->tab_label);
    } else {
        new_page = gtk_notebook_insert_page(new_nb, tab->container, tab->tab_label, target_pos);
    }
    editor_page_set_draggable(new_nb, tab->container);
    gtk_notebook_set_current_page(new_nb, new_page);

    g_object_unref(tab->container);
    g_object_unref(tab->tab_label);

    tab->nb = GTK_WIDGET(new_nb);
    app->last_focused_editor = tab;

    if (cleanup && GTK_WIDGET(old_nb) != app->notebook && gtk_notebook_get_n_pages(old_nb) == 0) {
        editor_cleanup_notebook(app, GTK_WIDGET(old_nb));
    }

    editor_update_panel_visibilities(app);
    focus_route_hold_history(app, FALSE);
    if (tab->view != NULL) {
        gtk_widget_grab_focus(tab->view);
    }
    editor_minimap_update_visibility(tab);
    minimap_update_visibility(app);
}

static void editor_move_tab_to_notebook(AppData* app, EditorTab* tab, GtkNotebook* new_nb,
                                        gint target_pos) {
    editor_move_tab_to(app, tab, new_nb, target_pos, TRUE);
}

void editor_move_tab_dedicated(AppData* app, EditorTab* tab) {
    if (app == NULL) return;
    if (tab == NULL) {
        tab = editor_current_tab(app);
    }
    if (tab == NULL) return;

    GtkNotebook* cur_nb = editor_tab_notebook(app, tab);
    if (cur_nb == NULL) return;

    if (GTK_WIDGET(cur_nb) == app->notebook) {
        GtkNotebook* target_nb = NULL;
        if (app->editor_notebook != NULL) {
            target_nb = GTK_NOTEBOOK(app->editor_notebook);
        } else {
            GPtrArray* arr = editor_get_all_notebooks(app);
            if (arr->len > 0) {
                target_nb = GTK_NOTEBOOK(g_ptr_array_index(arr, 0));
            }
            g_ptr_array_unref(arr);
        }

        if (target_nb == NULL) {
            GtkWidget* nb = editor_notebook_new(app);
            gtk_box_append(GTK_BOX(app->editor_box), nb);
            app->editor_notebook = nb;
            target_nb = GTK_NOTEBOOK(nb);
        }

        editor_move_tab_to_notebook(app, tab, target_nb, -1);
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
    } else {
        editor_move_tab_to_notebook(app, tab, GTK_NOTEBOOK(app->notebook), -1);
    }
}

/* Split editor notebook nb: a new empty notebook takes the `side` half of its
 * place (PANE_ZONE_LEFT/RIGHT/TOP/BOTTOM). Returns it, or NULL. */
static GtkWidget* editor_split_notebook(AppData* app, GtkNotebook* nb, PaneZone side) {
    GtkWidget* parent = gtk_widget_get_parent(GTK_WIDGET(nb));
    if (parent == NULL) return NULL;

    GtkOrientation orient = (side == PANE_ZONE_RIGHT || side == PANE_ZONE_LEFT)
        ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
    GtkWidget* new_paned = gtk_paned_new(orient);
    gtk_widget_set_hexpand(new_paned, TRUE);
    gtk_widget_set_vexpand(new_paned, TRUE);
    wire_paned_controllers(new_paned, app);

    GtkWidget* new_nb = editor_notebook_new(app);
    GtkWidget* cur_w = GTK_WIDGET(nb);
    g_object_ref(cur_w);

    if (GTK_IS_BOX(parent)) {
        gtk_box_remove(GTK_BOX(parent), cur_w);
        gtk_box_append(GTK_BOX(parent), new_paned);
    } else if (GTK_IS_PANED(parent)) {
        if (gtk_paned_get_start_child(GTK_PANED(parent)) == cur_w) {
            gtk_paned_set_start_child(GTK_PANED(parent), new_paned);
        } else {
            gtk_paned_set_end_child(GTK_PANED(parent), new_paned);
        }
    }

    if (side == PANE_ZONE_RIGHT || side == PANE_ZONE_BOTTOM) {
        gtk_paned_set_start_child(GTK_PANED(new_paned), cur_w);
        gtk_paned_set_resize_start_child(GTK_PANED(new_paned), TRUE);
        gtk_paned_set_shrink_start_child(GTK_PANED(new_paned), TRUE);
        gtk_paned_set_end_child(GTK_PANED(new_paned), new_nb);
        gtk_paned_set_resize_end_child(GTK_PANED(new_paned), TRUE);
        gtk_paned_set_shrink_end_child(GTK_PANED(new_paned), TRUE);
    } else {
        gtk_paned_set_start_child(GTK_PANED(new_paned), new_nb);
        gtk_paned_set_resize_start_child(GTK_PANED(new_paned), TRUE);
        gtk_paned_set_shrink_start_child(GTK_PANED(new_paned), TRUE);
        gtk_paned_set_end_child(GTK_PANED(new_paned), cur_w);
        gtk_paned_set_resize_end_child(GTK_PANED(new_paned), TRUE);
        gtk_paned_set_shrink_end_child(GTK_PANED(new_paned), TRUE);
    }
    g_object_unref(cur_w);

    return new_nb;
}

GtkWidget* editor_notebook_split(AppData* app, GtkNotebook* nb, gboolean vertical) {
    return editor_split_notebook(app, nb, vertical ? PANE_ZONE_BOTTOM : PANE_ZONE_RIGHT);
}

void editor_remove_empty_notebooks(AppData* app) {
    GPtrArray* nbs = editor_get_all_notebooks(app);
    for (guint i = 0; i < nbs->len; i++) {
        GtkWidget* nb = g_ptr_array_index(nbs, i);
        if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(nb)) == 0) {
            editor_cleanup_notebook(app, nb);
        }
    }
    g_ptr_array_unref(nbs);
    editor_update_panel_visibilities(app);
}

void editor_move_tab_direction(AppData* app, EditorTab* tab, EditorMoveDirection dir) {
    if (app == NULL) return;
    if (tab == NULL) {
        tab = editor_current_tab(app);
    }
    if (tab == NULL) return;

    GtkNotebook* cur_nb = editor_tab_notebook(app, tab);
    if (cur_nb == NULL) return;

    if (GTK_WIDGET(cur_nb) == app->notebook) {
        if (dir == EDITOR_MOVE_LEFT) {
            return;
        }
        editor_move_tab_dedicated(app, tab);
        return;
    }

    GPtrArray* all_nbs = editor_get_all_notebooks(app);
    GtkWidget* best_target = NULL;

    graphene_rect_t cur_bounds;
    if (gtk_widget_compute_bounds(GTK_WIDGET(cur_nb), app->editor_box, &cur_bounds)) {
        PaneRect cur = {cur_bounds.origin.x, cur_bounds.origin.y,
                        cur_bounds.size.width, cur_bounds.size.height};
        PaneRect* rects = g_new(PaneRect, MAX(all_nbs->len, 1));
        GtkWidget** widgets = g_new(GtkWidget*, MAX(all_nbs->len, 1));
        guint n = 0;
        for (guint i = 0; i < all_nbs->len; i++) {
            GtkWidget* candidate = GTK_WIDGET(g_ptr_array_index(all_nbs, i));
            if (candidate == GTK_WIDGET(cur_nb)) continue;
            graphene_rect_t b;
            if (!gtk_widget_compute_bounds(candidate, app->editor_box, &b)) continue;
            rects[n] = (PaneRect){b.origin.x, b.origin.y, b.size.width, b.size.height};
            widgets[n] = candidate;
            n++;
        }
        PaneDirection pdir = dir == EDITOR_MOVE_RIGHT ? PANE_DIR_RIGHT
                           : dir == EDITOR_MOVE_LEFT  ? PANE_DIR_LEFT
                           : dir == EDITOR_MOVE_UP    ? PANE_DIR_UP
                                                      : PANE_DIR_DOWN;
        gint idx = pane_find_neighbor(&cur, rects, n, pdir);
        if (idx >= 0) {
            best_target = widgets[idx];
        }
        g_free(rects);
        g_free(widgets);
    }
    g_ptr_array_unref(all_nbs);

    if (best_target != NULL) {
        editor_move_tab_to_notebook(app, tab, GTK_NOTEBOOK(best_target), -1);
        return;
    }

    if (dir == EDITOR_MOVE_LEFT) {
        editor_move_tab_dedicated(app, tab);
        return;
    }

    gint n_pages = gtk_notebook_get_n_pages(cur_nb);
    if (n_pages <= 1) {
        return;
    }

    PaneZone side = dir == EDITOR_MOVE_RIGHT ? PANE_ZONE_RIGHT
                  : dir == EDITOR_MOVE_UP    ? PANE_ZONE_TOP
                                             : PANE_ZONE_BOTTOM;
    GtkWidget* new_nb = editor_split_notebook(app, cur_nb, side);
    if (new_nb == NULL) return;
    editor_move_tab_to_notebook(app, tab, GTK_NOTEBOOK(new_nb), -1);
}

static EditorTab* editor_tab_of_page(AppData* app, GtkWidget* child) {
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = (EditorTab*)l->data;
        if (t != NULL && t->container == child) {
            return t;
        }
    }
    return NULL;
}

/* The notebook holding child as a page, or NULL */
static GtkNotebook* notebook_of_page(GtkWidget* child) {
    GtkWidget* parent = gtk_widget_get_parent(child);
    GtkWidget* nb = parent != NULL ? gtk_widget_get_ancestor(parent, GTK_TYPE_NOTEBOOK) : NULL;
    if (nb != NULL && gtk_notebook_page_num(GTK_NOTEBOOK(nb), child) >= 0) {
        return GTK_NOTEBOOK(nb);
    }
    return NULL;
}

static gboolean editor_cleanup_dropped_idle(gpointer data) {
    GtkWidget* nb = GTK_WIDGET(data);
    AppData* app = g_object_get_data(G_OBJECT(nb), "app");
    if (app != NULL && app->editor_box != NULL && gtk_widget_is_ancestor(nb, app->editor_box) &&
        gtk_notebook_get_n_pages(GTK_NOTEBOOK(nb)) == 0) {
        editor_cleanup_notebook(app, nb);
        editor_update_panel_visibilities(app);
    }
    g_object_unref(nb);
    return G_SOURCE_REMOVE;
}

/* source, an editor notebook, lost its last tab to a drop: remove it once the
 * drop is over, not when the tab drag ends (GTK may report that late or not
 * at all) */
static void editor_cleanup_after_drop(AppData* app, GtkNotebook* source) {
    if (GTK_WIDGET(source) == app->notebook || app->editor_box == NULL ||
        !gtk_widget_is_ancestor(GTK_WIDGET(source), app->editor_box) ||
        gtk_notebook_get_n_pages(source) > 0) {
        return;
    }
    g_object_set_data(G_OBJECT(source), "app", app);
    g_idle_add(editor_cleanup_dropped_idle, g_object_ref(source));
}

#ifdef HAVE_VTE
/* Move child, a page that is not an editor tab (a terminal), into to at
 * index (-1: append), with its tab label */
static void editor_move_page(AppData* app, GtkNotebook* from, GtkNotebook* to, GtkWidget* child,
                             gint index) {
    GtkWidget* label = gtk_notebook_get_tab_label(from, child);
    g_object_ref(child);
    g_object_ref(label);
    focus_route_hold_history(app, TRUE);
    gint page = gtk_notebook_page_num(from, child);
    gboolean was_current = (gtk_notebook_get_current_page(from) == page);
    if (gtk_notebook_get_tab_detachable(from, child)) {
        gtk_notebook_detach_tab(from, child);
    } else {
        gtk_notebook_remove_page(from, page);
    }
    editor_after_current_removed(app, from, was_current);
    page = (index < 0) ? gtk_notebook_append_page(to, child, label)
                       : gtk_notebook_insert_page(to, child, label, index);
    editor_page_set_draggable(to, child);
    gtk_notebook_set_current_page(to, page);
    focus_route_hold_history(app, FALSE);
    g_object_unref(label);
    g_object_unref(child);
}

/* A tab other than an editor tab (a terminal) dropped on target */
static void editor_page_dropped(AppData* app, GtkNotebook* target, GtkWidget* child,
                                PaneZone zone, gint index) {
    GtkNotebook* source = notebook_of_page(child);
    if (source == NULL) return;
    if (zone == PANE_ZONE_CENTER) {
        if (source == target) {
            gtk_notebook_reorder_child(target, child, index);
            gtk_notebook_set_current_page(target, gtk_notebook_page_num(target, child));
        } else {
            editor_move_page(app, source, target, child, index);
        }
    } else {
        if (source == target && gtk_notebook_get_n_pages(target) <= 1) return;
        GtkWidget* new_nb = editor_split_notebook(app, target, zone);
        if (new_nb == NULL) return;
        editor_move_page(app, source, GTK_NOTEBOOK(new_nb), child, -1);
    }
    editor_cleanup_after_drop(app, source);
    editor_update_panel_visibilities(app);
    gtk_widget_grab_focus(child);
    minimap_update_visibility(app);
}
#endif

void editor_remove_page(AppData* app, GtkWidget* child) {
    GtkNotebook* nb = (app != NULL && child != NULL) ? notebook_of_page(child) : NULL;
    if (nb == NULL) return;
    gint page = gtk_notebook_page_num(nb, child);
    gboolean was_current = (gtk_notebook_get_current_page(nb) == page);
    if (gtk_notebook_get_tab_detachable(nb, child)) {
        gtk_notebook_detach_tab(nb, child);
    } else {
        gtk_notebook_remove_page(nb, page);
    }
    editor_after_current_removed(app, nb, was_current);
    if (GTK_WIDGET(nb) != app->notebook && gtk_notebook_get_n_pages(nb) == 0) {
        editor_cleanup_notebook(app, GTK_WIDGET(nb));
    }
    editor_update_panel_visibilities(app);
    minimap_update_visibility(app);
}

/* A tab dragged with the mouse was dropped on target (see tab_drop.h) */
static void on_tab_dropped(GtkNotebook* target, GtkWidget* child, PaneZone zone,
                           gint index, gpointer data) {
    AppData* app = (AppData*)data;
    EditorTab* tab = editor_tab_of_page(app, child);
    if (tab == NULL) {
#ifdef HAVE_VTE
        /* Terminal tabs move like editor tabs; other pages stay */
        if (VTE_IS_TERMINAL(child)) {
            editor_page_dropped(app, target, child, zone, index);
        }
#endif
        return;
    }
    GtkNotebook* source = editor_tab_notebook(app, tab);

    if (zone == PANE_ZONE_CENTER) {
        if (source == target) {
            gtk_notebook_reorder_child(target, child, index);
            gtk_notebook_set_current_page(target, gtk_notebook_page_num(target, child));
            return;
        }
        editor_move_tab_to(app, tab, target, index, FALSE);
        editor_cleanup_after_drop(app, source);
        return;
    }
    if (source == target && gtk_notebook_get_n_pages(target) <= 1) return;
    GtkWidget* new_nb = editor_split_notebook(app, target, zone);
    if (new_nb == NULL) return;
    editor_move_tab_to(app, tab, GTK_NOTEBOOK(new_nb), -1, FALSE);
    editor_cleanup_after_drop(app, source);
}

/* The drag of a tab out of source has ended: remove source if left empty */
static void on_tab_drag_done(GtkNotebook* source, gpointer data) {
    AppData* app = (AppData*)data;
    editor_cleanup_after_drop(app, source);
}


EditorTab* editor_open_dedicated(AppData* app, const gchar* name, const gchar* full,
                                 GtkWidget* keep_focus) {
    if (app == NULL || full == NULL) return NULL;
    editor_open_file(app, name, full);
    EditorTab* tab = editor_find_tab(app, full);
    if (tab == NULL) return NULL; /* media, too large, unreadable: previewed only */
    if (editor_tab_notebook(app, tab) == GTK_NOTEBOOK(app->notebook)) {
        editor_move_tab_dedicated(app, tab);
    }
    if (keep_focus != NULL) {
        gtk_widget_grab_focus(keep_focus);
    } else if (tab->view != NULL) {
        gtk_widget_grab_focus(tab->view);
        app->last_focused_editor = tab;
    }
    return tab;
}

/* Give tab (its view built) a page with a title in target_nb, make it current
 * and focus it */
static void editor_tab_add_page(AppData* app, EditorTab* tab, GtkNotebook* target_nb) {
    tab->tab_label = file_type_label_new(tab->name, "");
    editor_tab_label_update(tab);

    GtkGesture* tab_rclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(tab_rclick), GDK_BUTTON_SECONDARY);
    g_signal_connect(tab_rclick, "pressed", G_CALLBACK(on_tab_label_right_click), tab);
    gtk_widget_add_controller(tab->tab_label, GTK_EVENT_CONTROLLER(tab_rclick));

    GtkGesture* tab_lclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(tab_lclick), GDK_BUTTON_PRIMARY);
    g_signal_connect(tab_lclick, "pressed", G_CALLBACK(on_tab_label_left_click), tab);
    gtk_widget_add_controller(tab->tab_label, GTK_EVENT_CONTROLLER(tab_lclick));

    GtkGesture* tab_mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(tab_mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(tab_mclick, "released", G_CALLBACK(on_tab_label_middle_click), tab);
    gtk_widget_add_controller(tab->tab_label, GTK_EVENT_CONTROLLER(tab_mclick));

    gint page = gtk_notebook_append_page(target_nb, tab->container, tab->tab_label);
    editor_page_set_draggable(target_nb, tab->container);
    g_object_ref(tab->container);
    g_object_ref(tab->tab_label);
    gtk_notebook_set_current_page(target_nb, page);
    gtk_widget_grab_focus(tab->view);

    editor_minimap_update_visibility(tab);

    g_signal_connect(tab->buffer, "modified-changed",
                     G_CALLBACK(on_editor_modified_changed), tab);

    app->editors = g_list_append(app->editors, tab);
    app->last_focused_editor = tab;
    if (app->editor_dedicated && app->editor_box != NULL) {
        gtk_widget_set_visible(app->editor_box, TRUE);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    }
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
        app->workspaces[app->current_ws].editors = app->editors;
        app->workspaces[app->current_ws].notebook_page = page;
    }
    minimap_update_visibility(app);
    app_emit(app, APP_EVENT_CHROME_CHANGED); /* the split button shows with a tab */
}

void editor_open_file(AppData* app, const gchar* name, const gchar* full) {
    FileKind kind = (g_strcmp0(full, app->viewed_path) == 0) ? app->viewed_kind
                                                             : file_kind_of_path(full, name);
    if (file_kind_is_media(kind)) {
        /* Images, PDFs, Videos, and Audio are previewed in the main viewer tab */
        if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
            app_emit(app, APP_EVENT_LAYOUT_CHANGED);
            app->applying_layout = TRUE;
            gtk_widget_set_visible(app->viewer_box, TRUE);
            app_emit(app, APP_EVENT_CHROME_CHANGED);
            app_emit(app, APP_EVENT_LAYOUT_REBUILT);
        }
        if (app->notebook != NULL) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
        }
        return;
    }

    GtkNotebook* target_nb = editor_target_notebook(app);
    if (target_nb == NULL) {
        return;
    }
    nav_history_file_opened(app, full);

    /* Already open: switch to its existing tab */
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = (EditorTab*)l->data;
        if (g_strcmp0(t->path, full) == 0) {
            t = editor_view_active(t); /* split: the view in use */
            GtkNotebook* t_nb = editor_tab_notebook(app, t);
            if (GTK_WIDGET(t_nb) != app->notebook) {
                if (app->editor_box != NULL && !gtk_widget_get_visible(app->editor_box)) {
                    app_emit(app, APP_EVENT_LAYOUT_CHANGED);
                    app->applying_layout = TRUE;
                    gtk_widget_set_visible(app->editor_box, TRUE);
                    app_emit(app, APP_EVENT_CHROME_CHANGED);
                    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
                }
            } else {
                if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
                    app_emit(app, APP_EVENT_LAYOUT_CHANGED);
                    app->applying_layout = TRUE;
                    gtk_widget_set_visible(app->viewer_box, TRUE);
                    app_emit(app, APP_EVENT_CHROME_CHANGED);
                    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
                }
            }
            gint target_page = gtk_notebook_page_num(t_nb, t->container);
            if (target_page >= 0) {
                gtk_notebook_set_current_page(t_nb, target_page);
            }
            if (t->view != NULL) {
                gtk_widget_grab_focus(t->view);
            }
            app->last_focused_editor = t;
            return;
        }
    }

    GStatBuf st;
    if (g_stat(full, &st) != 0) {
        gchar* msg = g_strdup_printf("[ Cannot read: %s ]", name);
        gtk_text_buffer_set_text(app->buffer, msg, -1);
        g_free(msg);
        return;
    }
    if (st.st_size > EDIT_MAX_BYTES) {
        gchar* msg = g_strdup_printf(
            "[ %s is too large to edit (%" G_GINT64_FORMAT
            " bytes, limit %d) ]", name, (gint64)st.st_size, EDIT_MAX_BYTES);
        gtk_text_buffer_set_text(app->buffer, msg, -1);
        g_free(msg);
        return;
    }

    gchar* contents = NULL;
    gsize len = 0;
    if (!g_file_get_contents(full, &contents, &len, NULL)) {
        gchar* msg = g_strdup_printf("[ Cannot read: %s ]", name);
        gtk_text_buffer_set_text(app->buffer, msg, -1);
        g_free(msg);
        return;
    }

    /* A text buffer takes valid UTF-8 only: binary files are not edited (an
     * empty buffer saved would erase them), other encodings are converted */
    if (!g_utf8_validate(contents, (gssize)len, NULL)) {
        if (!file_content_is_text((const guchar*)contents, MIN(len, FILE_SNIFF_BYTES))) {
            g_free(contents);
            gchar* msg = g_strdup_printf("%s is a binary file: previewed, not edited", name);
            app_notice(app, msg, FALSE);
            g_free(msg);
            return;
        }
        const gchar* encoding = NULL;
        gchar* utf8 = text_encoding_to_utf8(contents, len, &encoding);
        g_free(contents);
        contents = utf8;
        len = strlen(contents);
        gchar* msg = g_strdup_printf("%s read as %s: saving writes UTF-8", name,
                                     encoding != NULL ? encoding : "another encoding");
        app_notice(app, msg, FALSE);
        g_free(msg);
    }

    if (app->editor_dedicated) {
        if (app->editor_box != NULL && !gtk_widget_get_visible(app->editor_box)) {
            app_emit(app, APP_EVENT_LAYOUT_CHANGED);
            app->applying_layout = TRUE;
            gtk_widget_set_visible(app->editor_box, TRUE);
            app->editor_dedicated = TRUE;
            if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
                app->workspaces[app->current_ws].editor_dedicated = TRUE;
            }
            app_emit(app, APP_EVENT_CHROME_CHANGED);
            app_emit(app, APP_EVENT_LAYOUT_REBUILT);
        }
    } else {
        if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
            app_emit(app, APP_EVENT_LAYOUT_CHANGED);
            app->applying_layout = TRUE;
            gtk_widget_set_visible(app->viewer_box, TRUE);
            app_emit(app, APP_EVENT_CHROME_CHANGED);
            app_emit(app, APP_EVENT_LAYOUT_REBUILT);
        }
    }

    EditorTab* tab = g_new0(EditorTab, 1);
    tab->path = g_strdup(full);
    tab->name = g_strdup(name);
    tab->app = app;
    tab->nb = GTK_WIDGET(target_nb);
    tab->line_ending = text_line_ending_detect(contents, len);
    const gchar* cfg = g_get_user_config_dir();
    tab->is_buffer = (cfg != NULL && g_str_has_prefix(full, cfg));
    if (is_org_file(name)) {
        tab->mode = EDITOR_MODE_ORGMODE;
    } else {
        tab->mode = app->default_editor_mode;
    }
#ifdef HAVE_GSV
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(gtk_source_buffer_new(NULL));
#else
    GtkTextBuffer* buffer = gtk_text_buffer_new(NULL);
#endif
    gtk_text_buffer_set_text(buffer, contents, -1);
    /* set_text leaves the cursor at the end: start editing at the top */
    GtkTextIter start_iter;
    gtk_text_buffer_get_start_iter(buffer, &start_iter);
    gtk_text_buffer_place_cursor(buffer, &start_iter);
    gchar* detect_name = g_path_get_basename(full);
    if (detect_name == NULL || detect_name[0] == '\0') {
        g_free(detect_name);
        detect_name = g_strdup(name);
    }
    editor_apply_language(buffer, detect_name);
    g_free(detect_name);
    editor_view_build(app, tab, buffer);
    gtk_text_buffer_set_modified(tab->buffer, FALSE);
    editor_tab_add_page(app, tab, target_nb);
    g_free(contents);
}

void editor_split_tab(AppData* app, EditorTab* tab, EditorMoveDirection dir) {
    if (app == NULL) return;
    if (tab == NULL) {
        tab = editor_current_tab(app);
    }
    if (tab == NULL || tab->buffer == NULL || tab->view == NULL) return;
    /* The preview notebook does not split: the dedicated panel does */
    if (editor_tab_notebook(app, tab) == GTK_NOTEBOOK(app->notebook)) {
        editor_move_tab_dedicated(app, tab);
    }
    GtkNotebook* nb = editor_tab_notebook(app, tab);
    if (nb == NULL || GTK_WIDGET(nb) == app->notebook) return;
    PaneZone side = dir == EDITOR_MOVE_LEFT ? PANE_ZONE_LEFT
                  : dir == EDITOR_MOVE_UP   ? PANE_ZONE_TOP
                  : dir == EDITOR_MOVE_DOWN ? PANE_ZONE_BOTTOM
                                            : PANE_ZONE_RIGHT;
    GtkWidget* new_nb = editor_split_notebook(app, nb, side);
    if (new_nb != NULL) {
        editor_add_view(app, tab, GTK_NOTEBOOK(new_nb));
    }
}

EditorTab* editor_add_view(AppData* app, EditorTab* tab, GtkNotebook* nb) {
    EditorTab* twin = g_new0(EditorTab, 1);
    twin->path = g_strdup(tab->path);
    twin->name = g_strdup(tab->name);
    twin->app = app;
    twin->nb = GTK_WIDGET(nb);
    twin->is_buffer = tab->is_buffer;
    twin->line_ending = tab->line_ending;
    twin->mode = tab->mode;
    twin->text_mode = tab->text_mode;
    editor_view_build(app, twin, tab->buffer);
    /* The view looks like the one it was split from */
    diff_gutter_set_line_numbers(twin->view, diff_gutter_get_line_numbers(tab->view) ? 0 : -1);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(twin->view),
                                gtk_text_view_get_wrap_mode(GTK_TEXT_VIEW(tab->view)));
    editor_tab_add_page(app, twin, nb);
    editor_update_panel_visibilities(app);
    /* Both views share the cursor: show it */
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(twin->view), gtk_text_buffer_get_insert(twin->buffer),
                                 0.0, TRUE, 0.0, 0.3);
    editor_modes_update_status(twin);
    return twin;
}

gint editor_close_other_views(AppData* app, EditorTab* tab) {
    if (app == NULL || tab == NULL) return 0;
    gint closed = 0;
    for (EditorTab* t = editor_view_twin(app, tab); t != NULL; t = editor_view_twin(app, tab)) {
        editor_close_tab(app, t); /* no question: tab keeps the changes */
        closed++;
    }
    if (closed > 0 && tab->view != NULL) {
        app->last_focused_editor = tab;
        gtk_widget_grab_focus(tab->view);
    }
    return closed;
}

EditorTab* editor_find_tab(AppData* app, const gchar* path) {
    if (app == NULL || path == NULL) return NULL;
    gchar* want = g_canonicalize_filename(path, NULL);
    EditorTab* found = NULL;
    for (GList* l = app->editors; l != NULL && found == NULL; l = l->next) {
        EditorTab* t = (EditorTab*)l->data;
        if (t->path == NULL) continue;
        gchar* have = g_canonicalize_filename(t->path, NULL);
        if (g_strcmp0(have, want) == 0) found = editor_view_active(t); /* split: the view in use */
        g_free(have);
    }
    g_free(want);
    return found;
}

/* TRUE when the line of iter is (fully) inside the visible part of the view */
static gboolean line_visible(GtkTextView* view, const GtkTextIter* iter) {
    GdkRectangle visible;
    gtk_text_view_get_visible_rect(view, &visible);
    if (visible.height <= 0) return FALSE;
    gint y = 0, h = 0;
    gtk_text_view_get_line_yrange(view, iter, &y, &h);
    return y >= visible.y && y + h <= visible.y + visible.height;
}

EditorTab* editor_open_at(AppData* app, const gchar* path, gint line, gint index) {
    if (app == NULL || path == NULL) return NULL;
    EditorTab* tab = editor_find_tab(app, path);
    if (tab != NULL) {
        editor_open_file(app, tab->name, tab->path);
    } else {
        if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) return NULL;
        gchar* name = g_path_get_basename(path);
        editor_open_file(app, name, path);
        g_free(name);
        tab = editor_find_tab(app, path);
    }
    if (tab == NULL || tab->buffer == NULL) return NULL;

    gint lines = gtk_text_buffer_get_line_count(tab->buffer);
    line = CLAMP(line, 0, lines - 1);
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_line(tab->buffer, &it, line);
    /* Clamp to the line content, newline excluded */
    GtkTextIter end = it;
    if (!gtk_text_iter_ends_line(&end)) gtk_text_iter_forward_to_line_end(&end);
    gtk_text_iter_set_line_index(&it, CLAMP(index, 0, gtk_text_iter_get_line_index(&end)));
    gtk_text_buffer_place_cursor(tab->buffer, &it);
    if (tab->view != NULL) {
        GtkTextView* view = GTK_TEXT_VIEW(tab->view);
        if (!line_visible(view, &it)) {
            /* scroll_to_mark waits for line validation: works for a tab just opened */
            gtk_text_view_scroll_to_mark(view, gtk_text_buffer_get_insert(tab->buffer), 0.0, TRUE, 0.0, 0.3);
        }
        gtk_widget_grab_focus(tab->view);
    }
    app->last_focused_editor = tab;
    editor_modes_update_status(tab);
    return tab;
}

static gboolean emit_status_changed_idle(gpointer data) {
    AppData* app = (AppData*)data;
    app->status_idle_id = 0;
    app_emit(app, APP_EVENT_STATUS_CHANGED);
    return G_SOURCE_REMOVE;
}

void on_editor_switch_page_changed(GtkNotebook* notebook,
                                   GtkWidget* page, guint page_num,
                                   gpointer data) {
    (void)page;
    AppData* app = (AppData*)data;
    if (app == NULL) return;

    if (!app->switching_ws &&
        app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
        app->workspaces[app->current_ws].notebook_page = (gint)page_num;
    }

    if (GTK_WIDGET(notebook) == app->notebook && page_num == 0 && app->viewed_path != NULL) {
        for (GList* l = app->editors; l != NULL; l = l->next) {
            EditorTab* t = (EditorTab*)l->data;
            if (t != NULL && g_strcmp0(t->path, app->viewed_path) == 0) {
                if (t->buffer != NULL && (t->is_buffer || gtk_text_buffer_get_modified(t->buffer))) {
                    GtkTextIter start, end;
                    gtk_text_buffer_get_bounds(t->buffer, &start, &end);
                    gchar* text = gtk_text_buffer_get_text(t->buffer, &start, &end, TRUE);
                    g_file_set_contents(t->path, text, -1, NULL);
                    g_free(text);
                    gtk_text_buffer_set_modified(t->buffer, FALSE);
                    editor_tab_label_update(t);
                }
                app->file_dirty = FALSE;
                app_emit(app, APP_EVENT_PREVIEW_STALE);
                break;
            }
        }
    }

    GtkWidget* cur_child = gtk_notebook_get_nth_page(notebook, page_num);
    if (cur_child != NULL) {
        for (GList* l = app->editors; l != NULL; l = l->next) {
            EditorTab* t = (EditorTab*)l->data;
            if (t != NULL && t->container == cur_child) {
                app->last_focused_editor = t;
                editor_minimap_update_visibility(t);
                editor_modes_update_status(t);
                break;
            }
        }
    }
    /* Also on the preview page: the bar then shows the default editor mode.
     * "switch-page" runs before the notebook changes its current page and
     * moves the focus, so refresh once the switch is done. */
    if (app->status_idle_id == 0) {
        app->status_idle_id = g_idle_add(emit_status_changed_idle, app);
    }
    minimap_update_visibility(app);
}

gboolean editor_confirm_discard(AppData* app, EditorTab* tab) {
    if (tab == NULL || tab->is_buffer || !gtk_text_buffer_get_modified(tab->buffer)) {
        return TRUE;
    }
    if (editor_view_twin(app, tab) != NULL) {
        return TRUE; /* another view of the file keeps the changes */
    }
    GtkWidget* dlg = app_dialog_new("Unsaved Changes", app ? app->window : NULL,
                                    "_Cancel", APP_RESPONSE_CANCEL,
                                    "Close _Without Saving", APP_RESPONSE_OK,
                                    NULL);
    gchar* message = g_strdup_printf("%s has unsaved changes. Close anyway?", tab->name);
    GtkWidget* label = gtk_label_new(message);
    g_free(message);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 60);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_margin_top(label, 14);
    gtk_widget_set_margin_bottom(label, 14);
    gtk_widget_set_margin_start(label, 16);
    gtk_widget_set_margin_end(label, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), label);
    /* Destructive choice: Enter keeps the changes */
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL);

    gint response = app_dialog_run(APP_DIALOG(dlg),
                                   app_dialog_get_widget_for_response(APP_DIALOG(dlg), APP_RESPONSE_CANCEL));
    app_dialog_destroy(APP_DIALOG(dlg));
    return response == APP_RESPONSE_OK;
}

void editor_close_tab(AppData* app, EditorTab* tab) {
    if (app == NULL || tab == NULL) return;
    if (!editor_confirm_discard(app, tab)) return;

    if (tab->buffer != NULL && tab->is_buffer && tab->path != NULL) {
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(tab->buffer, &start, &end);
        gchar* text = gtk_text_buffer_get_text(tab->buffer, &start, &end, TRUE);
        g_file_set_contents(tab->path, text, -1, NULL);
        g_free(text);
        gtk_text_buffer_set_modified(tab->buffer, FALSE);
    }

    gchar* closed_path = g_strdup(tab->path);
    gchar* closed_name = g_strdup(tab->name);

    editor_view_release(tab);

    /* The focus goes back to the pane used before this tab */
    GtkWidget* focus_back = focus_route_return_target(app, tab->container);

    GtkWidget* nb_widget = tab->nb ? tab->nb : GTK_WIDGET(editor_target_notebook(app));
    GtkNotebook* nb = (nb_widget != NULL) ? GTK_NOTEBOOK(nb_widget) : NULL;
    if (nb != NULL) {
        gint page_idx = gtk_notebook_page_num(nb, tab->container);
        if (page_idx >= 0) {
            gboolean was_current = (gtk_notebook_get_current_page(nb) == page_idx);
            gtk_notebook_remove_page(nb, page_idx);
            editor_after_current_removed(app, nb, was_current);
        }
    }

    app->editors = g_list_remove(app->editors, tab);
    if (app->last_focused_editor == tab) {
        app->last_focused_editor = NULL;
    }

    if (nb_widget != NULL && nb_widget != app->notebook && nb != NULL && gtk_notebook_get_n_pages(nb) == 0) {
        editor_cleanup_notebook(app, nb_widget);
    }

    editor_update_panel_visibilities(app);
    focus_route_return(app, focus_back, tab->container);

    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
        app->workspaces[app->current_ws].editors = app->editors;
        gint cur = (app->editor_notebook && gtk_widget_get_visible(app->editor_box))
            ? gtk_notebook_get_current_page(GTK_NOTEBOOK(app->editor_notebook))
            : (app->notebook ? gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook)) : 0);
        app->workspaces[app->current_ws].notebook_page = (cur >= 0 ? cur : 0);
    }
    if (tab->mm_vis_source != 0) {
        g_source_remove(tab->mm_vis_source);
        tab->mm_vis_source = 0;
    }
    minimap_cache_clear(&tab->mm_cache);
    g_object_unref(tab->container);
    g_object_unref(tab->tab_label);
    editor_modes_free_tab(tab);
    g_free(tab->path);
    g_free(tab->name);
    g_free(tab);
    minimap_update_visibility(app);

    if (closed_path != NULL) {
        gboolean is_viewed = (app->viewed_path != NULL && g_strcmp0(app->viewed_path, closed_path) == 0);
        gboolean is_selected = FALSE;
        gchar* sel_path = NULL;
        gchar* sel_name = NULL;

        ExplorerRow* sel_row = (app->tree != NULL) ? explorer_tree_get_selected(app->tree) : NULL;
        if (sel_row != NULL) {
            sel_path = g_strdup(sel_row->path);
            sel_name = g_strdup(sel_row->name);
            is_selected = (g_strcmp0(sel_path, closed_path) == 0);
        }

        if (is_viewed || is_selected) {
            if (!is_viewed) {
                g_free(app->viewed_path);
                g_free(app->viewed_name);
                app->viewed_path = g_strdup(closed_path);
                app->viewed_name = g_path_get_basename(closed_path);
            }
            app->file_dirty = FALSE;
            app_emit(app, APP_EVENT_PREVIEW_STALE);
        }
        g_free(sel_path);
        g_free(sel_name);
        g_free(closed_path);
        g_free(closed_name);
    }
}

void editor_close_page(AppData* app, gint page) {
    if (app == NULL || page < 0) return;
    GtkNotebook* nb = editor_target_notebook(app);
    if (nb == NULL) return;
    GtkWidget* child = gtk_notebook_get_nth_page(nb, page);
    if (child == NULL) return;
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = (EditorTab*)l->data;
        if (t != NULL && t->container == child) {
            editor_close_tab(app, t);
            return;
        }
    }
}

void editor_close_current(AppData* app) {
    if (app == NULL) return;
    EditorTab* cur = editor_current_tab(app);
    if (cur != NULL) {
        editor_close_tab(app, cur);
    }
}

GtkWidget* editor_detach_tabs(AppData* app) {
    if (app == NULL) return NULL;
    if (app->notebook != NULL) {
        /* Only the editor tabs leave: Preview, Git and References stay */
        GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
        for (gint p = gtk_notebook_get_n_pages(nb) - 1; p >= 0; p--) {
            GtkWidget* child = gtk_notebook_get_nth_page(nb, p);
            for (GList* l = app->editors; l != NULL; l = l->next) {
                EditorTab* t = (EditorTab*)l->data;
                if (t != NULL && t->container == child) {
                    gtk_notebook_remove_page(nb, p);
                    break;
                }
            }
        }
    }
    /* The dedicated panel keeps its notebooks, splits and tabs: the whole
     * content leaves the panel and belongs to the workspace until it returns */
    GtkWidget* panel = NULL;
    if (app->editor_box != NULL) {
        GtkWidget* child = gtk_widget_get_first_child(app->editor_box);
        if (child != NULL) {
            GtkRoot* root = gtk_widget_get_root(child);
            GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
            if (focus != NULL && gtk_widget_is_ancestor(focus, child)) {
                gtk_root_set_focus(root, NULL);
            }
            panel = g_object_ref(child);
            gtk_box_remove(GTK_BOX(app->editor_box), child);
        }
    }
    app->editor_notebook = NULL;
    app->editors = NULL;
    return panel;
}

void editor_attach_tabs(AppData* app, GList* editors, GtkWidget* panel) {
    if (app == NULL) {
        if (panel != NULL) g_object_unref(panel);
        return;
    }
    if (panel != NULL && app->editor_box != NULL) {
        gtk_box_append(GTK_BOX(app->editor_box), panel);
    }
    if (panel != NULL) {
        g_object_unref(panel);
    }
    GPtrArray* arr = editor_get_all_notebooks(app);
    app->editor_notebook = (arr->len > 0) ? GTK_WIDGET(g_ptr_array_index(arr, 0)) : NULL;
    g_ptr_array_unref(arr);

    for (GList* l = editors; l != NULL; l = l->next) {
        EditorTab* tab = (EditorTab*)l->data;
        if (tab != NULL && tab->container != NULL && tab->tab_label != NULL &&
            tab->nb == app->notebook && app->notebook != NULL) {
            GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
            gtk_notebook_append_page(nb, tab->container, tab->tab_label);
            editor_page_set_draggable(nb, tab->container);
        }
        if (tab != NULL) {
            editor_minimap_update_visibility(tab);
        }
    }
    app->editors = editors;
    editor_update_panel_visibilities(app);
}

gboolean editor_confirm_discard_workspace(AppData* app, GList* editors) {
    for (GList* l = editors; l != NULL; l = l->next) {
        EditorTab* tab = (EditorTab*)l->data;
        gboolean seen = FALSE; /* the file of a split view: asked once */
        for (GList* p = editors; p != l && !seen; p = p->next) {
            seen = tab != NULL && ((EditorTab*)p->data)->buffer == tab->buffer;
        }
        if (tab != NULL && !seen && tab->buffer != NULL &&
            gtk_text_buffer_get_modified(tab->buffer)) {
            if (!editor_confirm_discard(app, tab)) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

void editor_free_tabs(AppData* app, GList* editors, gboolean detach_first) {
    if (detach_first && app != NULL) {
        GtkWidget* panel = editor_detach_tabs(app);
        if (panel != NULL) g_object_unref(panel);
    }
    for (GList* l = editors; l != NULL; l = l->next) {
        EditorTab* tab = (EditorTab*)l->data;
        if (tab == NULL) {
            continue;
        }
        /* No pointer to a freed tab may stay behind */
        if (app != NULL && app->last_focused_editor == tab) {
            app->last_focused_editor = NULL;
        }
        editor_view_release(tab);
        if (tab->mm_vis_source != 0) {
            g_source_remove(tab->mm_vis_source);
            tab->mm_vis_source = 0;
        }
        minimap_cache_clear(&tab->mm_cache);
        if (tab->container != NULL) {
            g_object_unref(tab->container);
            tab->container = NULL;
        }
        if (tab->tab_label != NULL) {
            g_object_unref(tab->tab_label);
            tab->tab_label = NULL;
        }
        editor_modes_free_tab(tab);
        g_free(tab->path);
        g_free(tab->name);
        g_free(tab);
    }
    g_list_free(editors);
}

void editor_set_tab_mode(AppData* app, EditorTab* tab, EditorMode mode) {
    if (tab == NULL) return;
    editor_modes_apply_mode(app, tab, mode);
}

void editor_set_dedicated(AppData* app, gboolean dedicated) {
    if (app == NULL) return;
    app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    /* Each move makes the moved tab current: keep the current one instead */
    EditorTab* keep = editor_current_tab(app);

    if (dedicated) {
        GList* to_move = NULL;
        for (GList* l = app->editors; l != NULL; l = l->next) {
            EditorTab* t = (EditorTab*)l->data;
            if (t != NULL && t->nb == app->notebook) {
                to_move = g_list_append(to_move, t);
            }
        }
        for (GList* l = to_move; l != NULL; l = l->next) {
            editor_move_tab_dedicated(app, (EditorTab*)l->data);
        }
        g_list_free(to_move);
    } else {
        GList* to_move = NULL;
        for (GList* l = app->editors; l != NULL; l = l->next) {
            EditorTab* t = (EditorTab*)l->data;
            if (t != NULL && t->nb != app->notebook) {
                to_move = g_list_append(to_move, t);
            }
        }
        for (GList* l = to_move; l != NULL; l = l->next) {
            editor_move_tab_dedicated(app, (EditorTab*)l->data);
        }
        g_list_free(to_move);
    }

    if (keep != NULL && keep->nb != NULL) {
        GtkNotebook* nb = GTK_NOTEBOOK(keep->nb);
        gint page = gtk_notebook_page_num(nb, keep->container);
        if (page >= 0) gtk_notebook_set_current_page(nb, page);
        app->last_focused_editor = keep;
    }

    editor_update_panel_visibilities(app);
    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    minimap_update_visibility(app);

    EditorTab* cur = editor_current_tab(app);
    if (cur != NULL && cur->view != NULL) {
        gtk_widget_grab_focus(cur->view);
        editor_minimap_update_visibility(cur);
    }
}

void editor_toggle_dedicated(AppData* app) {
    if (app == NULL) return;
    editor_set_dedicated(app, !app->editor_dedicated);
}

void editor_save_all_buffers(AppData* app) {
    if (app == NULL) {
        return;
    }
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* tab = (EditorTab*)l->data;
        if (tab != NULL && tab->is_buffer && tab->path != NULL && tab->buffer != NULL) {
            GtkTextIter start, end;
            gtk_text_buffer_get_bounds(tab->buffer, &start, &end);
            gchar* text = gtk_text_buffer_get_text(tab->buffer, &start, &end, TRUE);
            g_file_set_contents(tab->path, text, -1, NULL);
            g_free(text);
            gtk_text_buffer_set_modified(tab->buffer, FALSE);
        }
    }
}

gboolean editor_save_current(AppData* app) {
    EditorTab* tab = editor_current_tab(app);
    if (tab == NULL) {
        return FALSE;
    }
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(tab->buffer, &start, &end);
    gchar* text = gtk_text_buffer_get_text(tab->buffer, &start, &end, TRUE);
    if (!g_file_set_contents(tab->path, text, -1, NULL)) {
        g_printerr("[ Cannot save: %s ]\n", tab->name);
    } else {
        gtk_text_buffer_set_modified(tab->buffer, FALSE);
        editor_tab_label_update(tab);
        app_emit(app, APP_EVENT_FILES_CHANGED);
        if (app->viewed_path != NULL && g_strcmp0(app->viewed_path, tab->path) == 0) {
            app->file_dirty = FALSE;
            app_emit(app, APP_EVENT_PREVIEW_STALE);
        }
        if (tab->view != NULL) {
            gtk_widget_grab_focus(tab->view);
        }
    }
    g_free(text);
    return TRUE;
}
