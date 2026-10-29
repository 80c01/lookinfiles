#include "editor.h"
#include "dialog.h"
#include "panels.h"
#include "app_events.h"
#include "text_style.h"
#include "file_types.h"
#include "minimap.h"

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

static GtkWidget* create_editor_notebook(AppData* app) {
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

static void editor_cleanup_notebook(AppData* app, GtkWidget* nb_widget) {
    if (app == NULL || nb_widget == NULL || nb_widget == app->notebook) return;

    GtkWidget* parent = gtk_widget_get_parent(nb_widget);
    if (parent == NULL) return;

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
    gboolean dedicated = (dedicated_count > 0);
    app->editor_dedicated = dedicated;
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
        app->workspaces[app->current_ws].editor_dedicated = dedicated;
    }
    if (app->editor_box != NULL) {
        gtk_widget_set_visible(app->editor_box, dedicated);
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
        gint p = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
        gint base_offset = (app->git_tab_box != NULL) ? 2 : 1;
        if (p >= base_offset) {
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

static void on_editor_focus_enter(GtkEventControllerFocus* ctrl, gpointer data) {
    (void)ctrl;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL && tab->app != NULL) {
        AppData* app = (AppData*)tab->app;
        app->last_focused_editor = tab;
        update_active_panel_highlight(app);
        app_emit(app, APP_EVENT_STATUS_CHANGED);
        editor_modes_update_status(tab);
    }
}

static gboolean on_editor_view_key_press(GtkEventControllerKey* controller, guint keyval,
                                         guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    EditorTab* tab = (EditorTab*)data;
    if (tab != NULL && tab->app != NULL) {
        if (editor_modes_handle_key((AppData*)tab->app, tab, keyval, keycode, state)) {
            return TRUE;
        }
    }
    return FALSE;
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
    case TAB_ACTION_DEDICATED:
        editor_move_tab_dedicated(app, tab);
        break;
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

void editor_tab_show_context_menu(AppData* app, EditorTab* tab, GtkWidget* target_widget) {
    if (app == NULL || tab == NULL || target_widget == NULL) return;

    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_parent(popover, target_widget);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), TRUE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(box, 4);
    gtk_widget_set_margin_end(box, 4);
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);

    gboolean is_docked = (tab->nb == app->notebook);
    if (is_docked) {
        tab_menu_item(box, popover, app, tab, "Move to Dedicated Panel", "Ctrl+E", TAB_ACTION_DEDICATED);
    } else {
        tab_menu_item(box, popover, app, tab, "Dock to Main Panel", "Ctrl+E", TAB_ACTION_DEDICATED);
    }

    GtkWidget* sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep1, 4);
    gtk_widget_set_margin_bottom(sep1, 4);
    gtk_box_append(GTK_BOX(box), sep1);

    tab_menu_item(box, popover, app, tab, "Move Right", "Ctrl+Alt+Right", TAB_ACTION_MOVE_RIGHT);
    tab_menu_item(box, popover, app, tab, "Move Left", "Ctrl+Alt+Left", TAB_ACTION_MOVE_LEFT);
    tab_menu_item(box, popover, app, tab, "Move Down", "Ctrl+Alt+Down", TAB_ACTION_MOVE_DOWN);
    tab_menu_item(box, popover, app, tab, "Move Up", "Ctrl+Alt+Up", TAB_ACTION_MOVE_UP);

    GtkWidget* sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep2, 4);
    gtk_widget_set_margin_bottom(sep2, 4);
    gtk_box_append(GTK_BOX(box), sep2);

    tab_menu_item(box, popover, app, tab, "Mode: Notepad", NULL, TAB_ACTION_MODE_NOTEPAD);
    tab_menu_item(box, popover, app, tab, "Mode: Emacs", NULL, TAB_ACTION_MODE_EMACS);
    tab_menu_item(box, popover, app, tab, "Mode: Vim", NULL, TAB_ACTION_MODE_VIM);

    GtkWidget* sep3 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep3, 4);
    gtk_widget_set_margin_bottom(sep3, 4);
    gtk_box_append(GTK_BOX(box), sep3);

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

static void editor_move_tab_to_notebook(AppData* app, EditorTab* tab, GtkNotebook* new_nb, gint target_pos) {
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

    gtk_notebook_remove_page(old_nb, old_page);

    gint new_page = -1;
    if (target_pos < 0) {
        new_page = gtk_notebook_append_page(new_nb, tab->container, tab->tab_label);
    } else {
        new_page = gtk_notebook_insert_page(new_nb, tab->container, tab->tab_label, target_pos);
    }
    gtk_notebook_set_tab_reorderable(new_nb, tab->container, TRUE);
    gtk_notebook_set_current_page(new_nb, new_page);

    g_object_unref(tab->container);
    g_object_unref(tab->tab_label);

    tab->nb = GTK_WIDGET(new_nb);
    app->last_focused_editor = tab;

    if (GTK_WIDGET(old_nb) != app->notebook && gtk_notebook_get_n_pages(old_nb) == 0) {
        editor_cleanup_notebook(app, GTK_WIDGET(old_nb));
    }

    editor_update_panel_visibilities(app);
    if (tab->view != NULL) {
        gtk_widget_grab_focus(tab->view);
    }
    editor_minimap_update_visibility(tab);
    minimap_update_visibility(app);
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
            GtkWidget* nb = create_editor_notebook(app);
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
    float best_dist = 1e9f;

    graphene_rect_t cur_bounds;
    gboolean has_cur_bounds = gtk_widget_compute_bounds(GTK_WIDGET(cur_nb), app->editor_box, &cur_bounds);
    float cur_cx = has_cur_bounds ? (cur_bounds.origin.x + cur_bounds.size.width / 2.0f) : 0.0f;
    float cur_cy = has_cur_bounds ? (cur_bounds.origin.y + cur_bounds.size.height / 2.0f) : 0.0f;

    if (has_cur_bounds) {
        for (guint i = 0; i < all_nbs->len; i++) {
            GtkWidget* candidate = GTK_WIDGET(g_ptr_array_index(all_nbs, i));
            if (candidate == GTK_WIDGET(cur_nb)) continue;

            graphene_rect_t cand_bounds;
            if (!gtk_widget_compute_bounds(candidate, app->editor_box, &cand_bounds)) continue;

            float cand_cx = cand_bounds.origin.x + cand_bounds.size.width / 2.0f;
            float cand_cy = cand_bounds.origin.y + cand_bounds.size.height / 2.0f;

            switch (dir) {
            case EDITOR_MOVE_RIGHT:
                if (cand_cx > cur_cx) {
                    float dist = cand_cx - cur_cx;
                    if (dist < best_dist) {
                        best_dist = dist;
                        best_target = candidate;
                    }
                }
                break;
            case EDITOR_MOVE_LEFT:
                if (cand_cx < cur_cx) {
                    float dist = cur_cx - cand_cx;
                    if (dist < best_dist) {
                        best_dist = dist;
                        best_target = candidate;
                    }
                }
                break;
            case EDITOR_MOVE_DOWN:
                if (cand_cy > cur_cy) {
                    float dist = cand_cy - cur_cy;
                    if (dist < best_dist) {
                        best_dist = dist;
                        best_target = candidate;
                    }
                }
                break;
            case EDITOR_MOVE_UP:
                if (cand_cy < cur_cy) {
                    float dist = cur_cy - cand_cy;
                    if (dist < best_dist) {
                        best_dist = dist;
                        best_target = candidate;
                    }
                }
                break;
            }
        }
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

    GtkWidget* parent = gtk_widget_get_parent(GTK_WIDGET(cur_nb));
    if (parent == NULL) return;

    GtkOrientation orient = (dir == EDITOR_MOVE_RIGHT || dir == EDITOR_MOVE_LEFT)
        ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
    GtkWidget* new_paned = gtk_paned_new(orient);
    gtk_widget_set_hexpand(new_paned, TRUE);
    gtk_widget_set_vexpand(new_paned, TRUE);
    wire_paned_controllers(new_paned, app);

    GtkWidget* new_nb = create_editor_notebook(app);
    GtkWidget* cur_w = GTK_WIDGET(cur_nb);
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

    if (dir == EDITOR_MOVE_RIGHT || dir == EDITOR_MOVE_DOWN) {
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

    editor_move_tab_to_notebook(app, tab, GTK_NOTEBOOK(new_nb), -1);
}

/* GTK scrolls only until the cursor is visible: back at the start of a long
 * line the left padding stays scrolled away, and the cursor rides under the
 * overlay scrollbars at the right and bottom edges. After GTK has scrolled,
 * keep the padding (plus TEXT_SCROLLBAR_CLEARANCE right and bottom) around
 * the cursor, and show the line start with its padding when it fits. */
static void clamp_adjustment(GtkAdjustment* adj, gdouble value) {
    gdouble lower = gtk_adjustment_get_lower(adj);
    gdouble max = MAX(lower, gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj));
    value = CLAMP(value, lower, max);
    if (value != gtk_adjustment_get_value(adj)) {
        gtk_adjustment_set_value(adj, value);
    }
}

static gboolean keep_cursor_clear(gpointer data) {
    GtkTextView* view = GTK_TEXT_VIEW(data);
    GtkAdjustment* hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(view));
    GtkAdjustment* vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
    if (hadj == NULL || vadj == NULL || !gtk_widget_get_mapped(GTK_WIDGET(view))) {
        return G_SOURCE_REMOVE;
    }
    GdkRectangle c; /* buffer coordinates, like the adjustments */
    gtk_text_view_get_cursor_locations(view, NULL, &c, NULL);
    const gint pad_left = PREVIEW_TEXT_PADDING_X;
    const gint pad_right = PREVIEW_TEXT_PADDING_X + TEXT_SCROLLBAR_CLEARANCE;
    const gint pad_top = PREVIEW_TEXT_PADDING_Y;
    const gint pad_bottom = PREVIEW_TEXT_PADDING_Y + TEXT_SCROLLBAR_CLEARANCE;
    gint cw = MAX(c.width, 2);

    gdouble hv = gtk_adjustment_get_value(hadj);
    gdouble hp = gtk_adjustment_get_page_size(hadj);
    if (c.x + cw + pad_right <= hp) {
        clamp_adjustment(hadj, gtk_adjustment_get_lower(hadj)); /* line start fits: show it */
    } else if (c.x + cw + pad_right > hv + hp) {
        clamp_adjustment(hadj, c.x + cw + pad_right - hp);
    } else if (c.x - pad_left < hv) {
        clamp_adjustment(hadj, c.x - pad_left);
    }

    gdouble vv = gtk_adjustment_get_value(vadj);
    gdouble vp = gtk_adjustment_get_page_size(vadj);
    if (c.y + c.height + pad_bottom > vv + vp) {
        clamp_adjustment(vadj, c.y + c.height + pad_bottom - vp);
    } else if (c.y - pad_top < vv) {
        clamp_adjustment(vadj, c.y - pad_top);
    }
    return G_SOURCE_REMOVE;
}

/* Typing moves the cursor without "mark-set" */
static void on_editor_text_changed(GtkTextBuffer* buffer, gpointer data) {
    (void)buffer;
    g_idle_add_full(G_PRIORITY_LOW, keep_cursor_clear, g_object_ref(data), g_object_unref);
}

static void on_editor_cursor_moved(GtkTextBuffer* buffer, const GtkTextIter* location,
                                   GtkTextMark* mark, gpointer data) {
    (void)location;
    if (mark != gtk_text_buffer_get_insert(buffer)) {
        return;
    }
    /* Low priority: after the redraw in which GTK scrolls to the cursor */
    g_idle_add_full(G_PRIORITY_LOW, keep_cursor_clear, g_object_ref(data), g_object_unref);
}

void editor_open_file(AppData* app, const gchar* name, const gchar* full) {
    if (is_image_file(name) || is_pdf_file(name) || is_video_file(name) || is_audio_file(name)) {
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

    /* Already open: switch to its existing tab */
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = (EditorTab*)l->data;
        if (g_strcmp0(t->path, full) == 0) {
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

    struct stat st;
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
    const gchar* cfg = g_get_user_config_dir();
    tab->is_buffer = (cfg != NULL && g_str_has_prefix(full, cfg));
    if (is_org_file(name)) {
        tab->mode = EDITOR_MODE_ORGMODE;
    } else {
        tab->mode = EDITOR_MODE_NOTEPAD;
    }
#ifdef HAVE_GSV
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(gtk_source_buffer_new(NULL));
#else
    GtkTextBuffer* buffer = gtk_text_buffer_new(NULL);
#endif
    gtk_text_buffer_set_text(buffer, contents, -1);
#ifdef HAVE_GSV
    GtkWidget* view =
        gtk_source_view_new_with_buffer(GTK_SOURCE_BUFFER(buffer));
#else
    GtkWidget* view = gtk_text_view_new_with_buffer(buffer);
#endif
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), PREVIEW_TEXT_PADDING_X);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), PREVIEW_TEXT_PADDING_X + TEXT_SCROLLBAR_CLEARANCE);
#if GTK_CHECK_VERSION(3, 18, 0)
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), PREVIEW_TEXT_PADDING_Y);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view), PREVIEW_TEXT_PADDING_Y + TEXT_SCROLLBAR_CLEARANCE);
#endif
    gchar* detect_name = g_path_get_basename(full);
    if (detect_name == NULL || detect_name[0] == '\0') {
        g_free(detect_name);
        detect_name = g_strdup(name);
    }
    editor_apply_language(buffer, detect_name);
    set_view_monospace(view, is_code_file(detect_name));
#ifdef HAVE_GSV
    apply_vscode_scheme(buffer, view);
#endif
    g_free(detect_name);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);

    tab->buffer = buffer;
    tab->scroll = scroll;
    tab->view = view;

    GtkEventController* focus_ctrl = gtk_event_controller_focus_new();
    g_signal_connect(focus_ctrl, "enter", G_CALLBACK(on_editor_focus_enter), tab);
    gtk_widget_add_controller(view, focus_ctrl);

    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(key_ctrl, GTK_PHASE_CAPTURE);
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_editor_view_key_press), tab);
    gtk_widget_add_controller(view, key_ctrl);

    editor_minimap_init(tab);

    GtkWidget* container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(container, TRUE);
    gtk_widget_set_vexpand(container, TRUE);

    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(hbox, TRUE);
    gtk_widget_set_vexpand(hbox, TRUE);
    gtk_box_append(GTK_BOX(hbox), scroll);
    if (tab->minimap != NULL) {
        gtk_box_append(GTK_BOX(hbox), tab->minimap);
    }
    gtk_box_append(GTK_BOX(container), hbox);

    /* Status line for this editor */
    tab->status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(tab->status_bar, "editor-statusbar");
    GtkWidget* stat_lbl = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(stat_lbl), 0.0f);
    gtk_widget_set_hexpand(stat_lbl, TRUE);
    if (is_org_file(name) || tab->mode == EDITOR_MODE_ORGMODE) {
        gtk_label_set_text(GTK_LABEL(stat_lbl), "Org-mode (Emacs style) | Tab/Shift+Tab: fold | C-RET/C-SPC: checkbox | M-RET: heading | S-<->: TODO");
    } else if (tab->mode == EDITOR_MODE_VIM) {
        gtk_label_set_text(GTK_LABEL(stat_lbl), "VIM -- NORMAL --");
    } else if (tab->mode == EDITOR_MODE_EMACS) {
        gtk_label_set_text(GTK_LABEL(stat_lbl), "Emacs mode");
    } else {
        gtk_label_set_text(GTK_LABEL(stat_lbl), "Notepad mode");
    }
    g_object_set_data(G_OBJECT(tab->status_bar), "label", stat_lbl);
    gtk_box_append(GTK_BOX(tab->status_bar), stat_lbl);
    gtk_box_append(GTK_BOX(container), tab->status_bar);
    tab->container = container;
    editor_modes_init_tab(app, tab);
    gtk_text_buffer_set_modified(tab->buffer, FALSE);
    tab->tab_label = gtk_label_new("");
    editor_tab_label_update(tab);

    GtkGesture* tab_rclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(tab_rclick), GDK_BUTTON_SECONDARY);
    g_signal_connect(tab_rclick, "pressed", G_CALLBACK(on_tab_label_right_click), tab);
    gtk_widget_add_controller(tab->tab_label, GTK_EVENT_CONTROLLER(tab_rclick));

    GtkGesture* tab_lclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(tab_lclick), GDK_BUTTON_PRIMARY);
    g_signal_connect(tab_lclick, "pressed", G_CALLBACK(on_tab_label_left_click), tab);
    gtk_widget_add_controller(tab->tab_label, GTK_EVENT_CONTROLLER(tab_lclick));

    gint page = gtk_notebook_append_page(target_nb, container, tab->tab_label);
    gtk_notebook_set_tab_reorderable(target_nb, container, TRUE);
    g_object_ref(tab->container);
    g_object_ref(tab->tab_label);
    gtk_notebook_set_current_page(target_nb, page);
    gtk_widget_grab_focus(view);

    editor_minimap_update_visibility(tab);

    gtk_text_buffer_set_modified(tab->buffer, FALSE);
    g_signal_connect(buffer, "mark-set", G_CALLBACK(on_editor_cursor_moved), view);
    g_signal_connect_after(buffer, "changed", G_CALLBACK(on_editor_text_changed), view);
    g_signal_connect(buffer, "modified-changed",
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
    g_free(contents);
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
                app_emit(app, APP_EVENT_STATUS_CHANGED);
                break;
            }
        }
    }
    minimap_update_visibility(app);
}

gboolean editor_confirm_discard(AppData* app, EditorTab* tab) {
    if (tab == NULL || tab->is_buffer || !gtk_text_buffer_get_modified(tab->buffer)) {
        return TRUE;
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

    g_signal_handlers_disconnect_by_func(tab->buffer,
                                         (gpointer)on_editor_modified_changed,
                                         tab);

    GtkWidget* nb_widget = tab->nb ? tab->nb : GTK_WIDGET(editor_target_notebook(app));
    GtkNotebook* nb = (nb_widget != NULL) ? GTK_NOTEBOOK(nb_widget) : NULL;
    if (nb != NULL) {
        gint page_idx = gtk_notebook_page_num(nb, tab->container);
        if (page_idx >= 0) {
            gboolean was_current = (gtk_notebook_get_current_page(nb) == page_idx);
            gtk_notebook_remove_page(nb, page_idx);
            /* GTK selects the neighbour page: when closing the current tab lands on
             * the Git tab (no editor tab left beside it), show the preview instead */
            if (was_current && nb_widget == app->notebook && app->git_tab_box != NULL &&
                gtk_notebook_get_current_page(nb) == gtk_notebook_page_num(nb, app->git_tab_box)) {
                gtk_notebook_set_current_page(nb, 0);
            }
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
    if (tab->mm_cache != NULL) {
        cairo_surface_destroy(tab->mm_cache);
        tab->mm_cache = NULL;
    }
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

void editor_detach_tabs(AppData* app) {
    if (app == NULL) return;
    if (app->notebook != NULL) {
        gint n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
        gint keep = (app->git_tab_box != NULL) ? 2 : 1;
        while (n > keep) {
            gtk_notebook_remove_page(GTK_NOTEBOOK(app->notebook), n - 1);
            n--;
        }
    }
    if (app->editor_box != NULL) {
        GPtrArray* arr = editor_get_all_notebooks(app);
        for (guint i = 0; i < arr->len; i++) {
            GtkNotebook* nb = GTK_NOTEBOOK(g_ptr_array_index(arr, i));
            gint n = gtk_notebook_get_n_pages(nb);
            while (n > 0) {
                gtk_notebook_remove_page(nb, n - 1);
                n--;
            }
        }
        g_ptr_array_unref(arr);
    }
    app->editors = NULL;
}

void editor_attach_tabs(AppData* app, GList* editors) {
    if (app == NULL) return;
    for (GList* l = editors; l != NULL; l = l->next) {
        EditorTab* tab = (EditorTab*)l->data;
        if (tab != NULL && tab->container != NULL && tab->tab_label != NULL) {
            GtkNotebook* nb = editor_tab_notebook(app, tab);
            if (nb != NULL) {
                gtk_notebook_append_page(nb, tab->container, tab->tab_label);
                gtk_notebook_set_tab_reorderable(nb, tab->container, TRUE);
                editor_minimap_update_visibility(tab);
            }
        }
    }
    app->editors = editors;
    editor_update_panel_visibilities(app);
}

gboolean editor_confirm_discard_workspace(AppData* app, GList* editors) {
    for (GList* l = editors; l != NULL; l = l->next) {
        EditorTab* tab = (EditorTab*)l->data;
        if (tab != NULL && tab->buffer != NULL &&
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
        editor_detach_tabs(app);
    }
    for (GList* l = editors; l != NULL; l = l->next) {
        EditorTab* tab = (EditorTab*)l->data;
        if (tab == NULL) {
            continue;
        }
        g_signal_handlers_disconnect_by_func(tab->buffer,
                                             (gpointer)on_editor_modified_changed,
                                             tab);
        if (tab->mm_vis_source != 0) {
            g_source_remove(tab->mm_vis_source);
            tab->mm_vis_source = 0;
        }
        if (tab->mm_cache != NULL) {
            cairo_surface_destroy(tab->mm_cache);
            tab->mm_cache = NULL;
        }
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
    EditorTab* cur = editor_current_tab(app);
    if (cur != NULL) {
        editor_move_tab_dedicated(app, cur);
    } else {
        editor_set_dedicated(app, !app->editor_dedicated);
    }
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
