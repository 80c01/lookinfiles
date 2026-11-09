/* Editor notebooks: the notebook tree of the dedicated panel, moving,
 * splitting and dropping editor tabs and movable pages (see editor.h) */
#include "editor/editor.h"
#include "editor/editor_private.h"
#include "widgets/pane_geometry.h"
#include "widgets/panels.h"
#include "core/app_events.h"
#include "widgets/minimap.h"
#include "widgets/tab_drop.h"
#include "widgets/focus_route.h"
#include "git/git_tab.h"

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

/* Editor tabs can be reordered and dragged to another notebook */
void editor_page_set_draggable(GtkNotebook* nb, GtkWidget* child) {
    gtk_notebook_set_tab_reorderable(nb, child, TRUE);
    gtk_notebook_set_tab_detachable(nb, child, TRUE);
}

#define MOVABLE_PAGE_KEY "editor-movable-page"

void editor_page_set_movable(GtkWidget* page) {
    g_object_set_data(G_OBJECT(page), MOVABLE_PAGE_KEY, GINT_TO_POINTER(1));
}

gboolean editor_page_is_movable(GtkWidget* page) {
    if (page == NULL) return FALSE;
#ifdef HAVE_VTE
    if (VTE_IS_TERMINAL(page)) return TRUE;
#endif
    return g_object_get_data(G_OBJECT(page), MOVABLE_PAGE_KEY) != NULL;
}

/* Editor notebooks take the tabs of their group, terminal and Canvas tabs */
static gboolean editor_accepts_tab(GtkNotebook* target, GtkNotebook* source, GtkWidget* child,
                                   gpointer data) {
    (void)data;
    if (g_strcmp0(gtk_notebook_get_group_name(target), gtk_notebook_get_group_name(source)) == 0) {
        return TRUE;
    }
    return editor_page_is_movable(child);
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

GtkNotebook* editor_target_notebook(AppData* app) {
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

GtkNotebook* editor_tab_notebook(AppData* app, EditorTab* tab) {
    if (tab != NULL && tab->nb != NULL) {
        return GTK_NOTEBOOK(tab->nb);
    }
    return editor_target_notebook(app);
}

static void editor_cleanup_notebook_tree(AppData* app, GtkWidget* nb_widget, GtkWidget* parent);

/* Remove an empty editor notebook and the split that held it */
void editor_cleanup_notebook(AppData* app, GtkWidget* nb_widget) {
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

void editor_update_panel_visibilities(AppData* app) {
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
        app_layout_hold(app);
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
void editor_after_current_removed(AppData* app, GtkNotebook* nb, gboolean was_current) {
    GtkWidget* git_page = git_tab_page(app);
    if (was_current && GTK_WIDGET(nb) == app->notebook && git_page != NULL &&
        gtk_notebook_get_current_page(nb) == gtk_notebook_page_num(nb, git_page)) {
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
    minimap_view_update_visibility(tab->minimap);
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
GtkWidget* editor_split_notebook(AppData* app, GtkNotebook* nb, PaneZone side) {
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

    GtkWidget* best_target = editor_neighbor_notebook(app, cur_nb, dir);
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

/* The editor notebook next to cur_nb in direction dir, or NULL */
GtkWidget* editor_neighbor_notebook(AppData* app, GtkNotebook* cur_nb, EditorMoveDirection dir) {
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
    return best_target;
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
GtkNotebook* editor_notebook_of_page(GtkWidget* child) {
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

/* Move child, a page that is not an editor tab (a terminal, a canvas), into
 * to at index (-1: append), with its tab label */
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

/* A tab other than an editor tab (a terminal, a canvas) dropped on target */
static void editor_page_dropped(AppData* app, GtkNotebook* target, GtkWidget* child,
                                PaneZone zone, gint index) {
    GtkNotebook* source = editor_notebook_of_page(child);
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

void editor_move_page_to(AppData* app, GtkWidget* child, GtkNotebook* to, gint index) {
    GtkNotebook* from = (app != NULL && child != NULL && to != NULL) ? editor_notebook_of_page(child) : NULL;
    if (from == NULL || from == to) return;
    editor_move_page(app, from, to, child, index);
}

/* The notebook of the editor panel new pages go to: the one of the editor
 * tab used last, else the first one (made when there is none) */
static GtkNotebook* editor_panel_notebook(AppData* app) {
    EditorTab* last = g_list_find(app->editors, app->last_focused_editor) ? app->last_focused_editor : NULL;
    if (last != NULL && last->nb != NULL && last->nb != app->notebook) {
        return GTK_NOTEBOOK(last->nb);
    }
    if (app->editor_notebook != NULL) {
        return GTK_NOTEBOOK(app->editor_notebook);
    }
    GPtrArray* arr = editor_get_all_notebooks(app);
    GtkWidget* nb = (arr->len > 0) ? g_ptr_array_index(arr, 0) : NULL;
    g_ptr_array_unref(arr);
    if (nb == NULL) {
        nb = editor_notebook_new(app);
        gtk_box_append(GTK_BOX(app->editor_box), nb);
    }
    app->editor_notebook = nb;
    return GTK_NOTEBOOK(nb);
}

/* Append child to nb as its current, movable page */
static void add_movable_page(AppData* app, GtkNotebook* nb, GtkWidget* child, GtkWidget* label) {
    editor_page_set_movable(child);
    gint page = gtk_notebook_append_page(nb, child, label);
    editor_page_set_draggable(nb, child);
    gtk_notebook_set_current_page(nb, page);
    editor_update_panel_visibilities(app);
    minimap_update_visibility(app);
}

void editor_add_page(AppData* app, GtkWidget* child, GtkWidget* label) {
    if (app == NULL || child == NULL || app->editor_box == NULL) return;
    app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    add_movable_page(app, editor_panel_notebook(app), child, label);
}

void editor_add_page_like_file(AppData* app, GtkWidget* child, GtkWidget* label) {
    if (app == NULL || child == NULL || app->editor_box == NULL) return;
    GtkNotebook* nb = editor_target_notebook(app);
    if (nb == NULL || GTK_WIDGET(nb) != app->notebook) {
        editor_add_page(app, child, label);
        return;
    }
    add_movable_page(app, nb, child, label);
    if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
        app_layout_begin(app);
        gtk_widget_set_visible(app->viewer_box, TRUE);
        app_emit(app, APP_EVENT_CHROME_CHANGED);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    }
}

/* Add to pages the movable pages of nb that are not editor tabs */
static void collect_other_pages(AppData* app, GtkNotebook* nb, GPtrArray* pages) {
    gint n = gtk_notebook_get_n_pages(nb);
    for (gint i = 0; i < n; i++) {
        GtkWidget* child = gtk_notebook_get_nth_page(nb, i);
        if (editor_page_is_movable(child) && editor_tab_of_page(app, child) == NULL) {
            g_ptr_array_add(pages, child);
        }
    }
}

/* The movable pages that are not editor tabs, in the preview notebook
 * (preview TRUE) or in the editor panel */
static GPtrArray* other_pages(AppData* app, gboolean preview) {
    GPtrArray* pages = g_ptr_array_new();
    if (preview) {
        if (app->notebook != NULL) collect_other_pages(app, GTK_NOTEBOOK(app->notebook), pages);
        return pages;
    }
    GPtrArray* nbs = editor_get_all_notebooks(app);
    for (guint i = 0; i < nbs->len; i++) {
        collect_other_pages(app, GTK_NOTEBOOK(g_ptr_array_index(nbs, i)), pages);
    }
    g_ptr_array_unref(nbs);
    return pages;
}

void editor_move_other_pages(AppData* app, gboolean dedicated) {
    if (app == NULL || app->notebook == NULL || app->editor_box == NULL) return;
    GPtrArray* pages = other_pages(app, dedicated);
    for (guint i = 0; i < pages->len; i++) {
        GtkWidget* child = g_ptr_array_index(pages, i);
        GtkNotebook* from = editor_notebook_of_page(child);
        if (from == NULL) continue;
        GtkNotebook* to = dedicated ? editor_panel_notebook(app) : GTK_NOTEBOOK(app->notebook);
        editor_move_page(app, from, to, child, -1);
        if (!dedicated && gtk_notebook_get_n_pages(from) == 0) {
            editor_cleanup_notebook(app, GTK_WIDGET(from));
        }
    }
    if (dedicated && pages->len > 0) {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
    }
    g_ptr_array_unref(pages);
}

gboolean editor_has_dockable_tabs(AppData* app) {
    if (app == NULL) return FALSE;
    if (app->editors != NULL) return TRUE;
    GPtrArray* pages = other_pages(app, TRUE);
    gboolean found = pages->len > 0;
    g_ptr_array_unref(pages);
    if (!found) {
        pages = other_pages(app, FALSE);
        found = pages->len > 0;
        g_ptr_array_unref(pages);
    }
    return found;
}

gboolean editor_move_page_direction(AppData* app, GtkWidget* child, EditorMoveDirection dir) {
    GtkNotebook* cur_nb = (app != NULL && child != NULL) ? editor_notebook_of_page(child) : NULL;
    if (cur_nb == NULL || !editor_page_is_movable(child) || editor_tab_of_page(app, child) != NULL) return FALSE;
    gboolean in_preview = GTK_WIDGET(cur_nb) == app->notebook;
    if (!in_preview && (app->editor_box == NULL || !gtk_widget_is_ancestor(GTK_WIDGET(cur_nb), app->editor_box))) {
        return FALSE; /* a terminal of the terminal panel */
    }
    app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    if (in_preview) {
        /* As for an editor tab: to the editor panel, except to the left */
        if (dir != EDITOR_MOVE_LEFT) {
            editor_move_page(app, cur_nb, editor_panel_notebook(app), child, -1);
            gtk_notebook_set_current_page(cur_nb, 0);
        }
    } else {
        GtkWidget* target = editor_neighbor_notebook(app, cur_nb, dir);
        if (target != NULL) {
            editor_move_page(app, cur_nb, GTK_NOTEBOOK(target), child, -1);
        } else if (dir == EDITOR_MOVE_LEFT && app->notebook != NULL) {
            editor_move_page(app, cur_nb, GTK_NOTEBOOK(app->notebook), child, -1);
        } else if (dir != EDITOR_MOVE_LEFT && gtk_notebook_get_n_pages(cur_nb) > 1) {
            PaneZone side = dir == EDITOR_MOVE_RIGHT ? PANE_ZONE_RIGHT
                          : dir == EDITOR_MOVE_UP    ? PANE_ZONE_TOP
                                                     : PANE_ZONE_BOTTOM;
            GtkWidget* new_nb = editor_split_notebook(app, cur_nb, side);
            if (new_nb != NULL) editor_move_page(app, cur_nb, GTK_NOTEBOOK(new_nb), child, -1);
        }
        if (gtk_notebook_get_n_pages(cur_nb) == 0) editor_cleanup_notebook(app, GTK_WIDGET(cur_nb));
    }
    editor_update_panel_visibilities(app);
    minimap_update_visibility(app);
    gtk_widget_grab_focus(child);
    return TRUE;
}

void editor_remove_page(AppData* app, GtkWidget* child) {
    GtkNotebook* nb = (app != NULL && child != NULL) ? editor_notebook_of_page(child) : NULL;
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
        /* Terminal and Canvas tabs move like editor tabs; other pages stay */
        if (editor_page_is_movable(child)) {
            editor_page_dropped(app, target, child, zone, index);
        }
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
