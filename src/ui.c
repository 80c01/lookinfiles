#include "ui.h"
#include "dialog.h"
#include "layouts.h"
#include "panels.h"
#include "app_events.h"
#include "workspace.h"
#include "file_list.h"
#include "terminal.h"
#include "notes.h"
#include "editor.h"
#include "editor_modes.h"


static void on_window_notify_focus_widget(GObject* gobject, GParamSpec* pspec, gpointer user_data) {
    (void)gobject;
    (void)pspec;
    AppData* app = (AppData*)user_data;
    if (app != NULL) {
        update_active_panel_highlight(app);
    }
}
void wire_focus_tracking(AppData* app) {
    if (app == NULL || app->window == NULL) {
        return;
    }
    g_signal_connect(app->window, "notify::focus-widget",
                     G_CALLBACK(on_window_notify_focus_widget), app);
    update_active_panel_highlight(app);
}

void focus_adjacent_panel(gint step, AppData* app) {
    GPtrArray* panels = g_ptr_array_new();
    gint count;
    gint current = -1;

    if (app->left_vbox == NULL || gtk_widget_get_visible(app->left_vbox)) {
        g_ptr_array_add(panels, GTK_WIDGET(app->tree_view));
    }
    if (app->viewer_box == NULL || gtk_widget_get_visible(app->viewer_box)) {
        EditorTab* cur_tab = editor_current_tab(app);
        if (cur_tab != NULL && cur_tab->nb == app->notebook && cur_tab->view != NULL) {
            g_ptr_array_add(panels, cur_tab->view);
        } else {
            g_ptr_array_add(panels, app->text_view);
        }
    }
    if (app->editor_box != NULL && gtk_widget_get_visible(app->editor_box)) {
        GPtrArray* arr = editor_get_all_notebooks(app);
        for (guint i = 0; i < arr->len; i++) {
            GtkNotebook* nb = GTK_NOTEBOOK(g_ptr_array_index(arr, i));
            gint cur_p = gtk_notebook_get_current_page(nb);
            if (cur_p >= 0) {
                GtkWidget* child = gtk_notebook_get_nth_page(nb, cur_p);
                for (GList* l = app->editors; l != NULL; l = l->next) {
                    EditorTab* t = (EditorTab*)l->data;
                    if (t != NULL && t->container == child && t->view != NULL) {
                        g_ptr_array_add(panels, t->view);
                        break;
                    }
                }
            }
        }
        g_ptr_array_unref(arr);
    }
#ifdef HAVE_VTE
    if (gtk_widget_get_visible(app->term_scroll)) {
        GPtrArray* panes = term_visible_panes(app);
        for (guint i = 0; i < panes->len; i++) {
            g_ptr_array_add(panels, g_ptr_array_index(panes, i));
        }
        g_ptr_array_unref(panes);
    }
#endif
    count = (gint)panels->len;
    if (count == 0) {
        g_ptr_array_unref(panels);
        return;
    }

    /* Which panel owns the focus? Walk the focused widget's ancestors */
    GtkWidget* w = gtk_window_get_focus(app->window);
    while (w != NULL && current < 0) {
        guint i;
        for (i = 0; i < panels->len; i++) {
            if (g_ptr_array_index(panels, i) == w) {
                current = (gint)i;
                break;
            }
        }
        w = gtk_widget_get_parent(w);
    }
    if (current < 0) {
        current = 0; /* unknown focus: count as first panel */
    }

    gtk_widget_grab_focus(GTK_WIDGET(g_ptr_array_index(
        panels, (current + step + count) % count)));
    g_ptr_array_unref(panels);
}

gboolean on_window_key_press(GtkEventControllerKey* controller, guint keyval,
                             guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    AppData* app = (AppData*)data;

    /* A dialog is open: its keys only (window shortcuts would act behind it) */
    if (app_dialog_is_open(app->window)) {
        return FALSE;
    }

    /* Ctrl+Q: quit */
    if ((state & GDK_CONTROL_MASK) &&
        (keyval == GDK_KEY_q || keyval == GDK_KEY_Q)) {
        for (gint i = 0; i < app->num_workspaces; i++) {
            if (!editor_confirm_discard_workspace(app, app->workspaces[i].editors)) {
                return TRUE;
            }
        }
        if (app->main_loop != NULL && g_main_loop_is_running(app->main_loop)) {
            g_main_loop_quit(app->main_loop);
        }
        return TRUE;
    }

    /* Keys the focused editor tab needs (Org-mode C-c prefixes, Alt+arrows) */
    if (editor_modes_claims_key(app, keyval, state)) {
        return FALSE;
    }

    /* Alt+Left / Alt+Right: panel focus cycling */
    if ((state & GDK_ALT_MASK) != 0 &&
        (keyval == GDK_KEY_Left || keyval == GDK_KEY_Right)) {
        focus_adjacent_panel(keyval == GDK_KEY_Right ? 1 : -1, app);
        return TRUE;
    }

    /* Alt+1 .. Alt+8: switch virtual workspace; Alt++ / Alt+-: add/remove workspace */
    if ((state & GDK_ALT_MASK) != 0) {
        if (keyval >= GDK_KEY_1 && keyval <= GDK_KEY_8) {
            gint idx = (gint)(keyval - GDK_KEY_1);
            if (idx < app->num_workspaces) {
                workspace_switch(app, idx);
            }
            return TRUE;
        }
        if (keyval == GDK_KEY_plus || keyval == GDK_KEY_equal || keyval == GDK_KEY_KP_Add) {
            workspace_add(app);
            return TRUE;
        }
        if (keyval == GDK_KEY_minus || keyval == GDK_KEY_KP_Subtract) {
            workspace_remove(app, app->current_ws);
            return TRUE;
        }
        /* Alt+L: saved workspace layouts (Ctrl+Alt+L moves editor tabs) */
        if ((keyval == GDK_KEY_l || keyval == GDK_KEY_L) && !(state & GDK_CONTROL_MASK)) {
            layouts_show_dialog(app);
            return TRUE;
        }
    }

    /* Ctrl+Shift+L: stack the panels or restore side-by-side layout.
     * Shift makes the keyval uppercase, so accept both l and L. */
    if ((state & GDK_CONTROL_MASK) &&
        (state & GDK_SHIFT_MASK) &&
        (keyval == GDK_KEY_l || keyval == GDK_KEY_L)) {
        layout_toggle(app);
        return TRUE;
    }

    GtkWidget* focus_w = (app->window != NULL) ? gtk_window_get_focus(GTK_WINDOW(app->window)) : NULL;
    EditorTab* cur_tab = app->last_focused_editor;
    gboolean is_modal_tab = (cur_tab != NULL && focus_w != NULL &&
                             (cur_tab->mode == EDITOR_MODE_EMACS ||
                              cur_tab->mode == EDITOR_MODE_ORGMODE ||
                              cur_tab->mode == EDITOR_MODE_VIM) &&
                             (focus_w == cur_tab->view ||
                              gtk_widget_is_ancestor(focus_w, cur_tab->container)));

    if (!is_modal_tab) {
        /* Ctrl+S: save current editor tab; Ctrl+W: close current editor tab */
        if ((state & GDK_CONTROL_MASK) &&
            !(state & GDK_SHIFT_MASK) &&
            (keyval == GDK_KEY_s || keyval == GDK_KEY_S)) {
            return editor_save_current(app);
        }
        if ((state & GDK_CONTROL_MASK) &&
            (keyval == GDK_KEY_w || keyval == GDK_KEY_W)) {
            editor_close_current(app);
            return TRUE;
        }
    }

    /* Ctrl+Alt+Arrow / Ctrl+Alt+VIM / Ctrl+Alt+D: move editor tab */
    if ((state & GDK_CONTROL_MASK) && (state & GDK_ALT_MASK)) {
        if (keyval == GDK_KEY_Right || keyval == GDK_KEY_l || keyval == GDK_KEY_L) {
            editor_move_tab_direction(app, NULL, EDITOR_MOVE_RIGHT);
            return TRUE;
        }
        if (keyval == GDK_KEY_Left || keyval == GDK_KEY_h || keyval == GDK_KEY_H) {
            editor_move_tab_direction(app, NULL, EDITOR_MOVE_LEFT);
            return TRUE;
        }
        if (keyval == GDK_KEY_Down || keyval == GDK_KEY_j || keyval == GDK_KEY_J) {
            editor_move_tab_direction(app, NULL, EDITOR_MOVE_DOWN);
            return TRUE;
        }
        if (keyval == GDK_KEY_Up || keyval == GDK_KEY_k || keyval == GDK_KEY_K) {
            editor_move_tab_direction(app, NULL, EDITOR_MOVE_UP);
            return TRUE;
        }
        if (keyval == GDK_KEY_d || keyval == GDK_KEY_D) {
            editor_move_tab_dedicated(app, NULL);
            return TRUE;
        }
        /* Ctrl+Alt+N: line numbers of the current editor tab */
        if (keyval == GDK_KEY_n || keyval == GDK_KEY_N) {
            editor_toggle_line_numbers(app, NULL);
            return TRUE;
        }
    }

    /* Ctrl+E: move active editor tab to dedicated panel right of preview, or dock back */
    if ((state & GDK_CONTROL_MASK) &&
        !(state & GDK_SHIFT_MASK) &&
        (keyval == GDK_KEY_e || keyval == GDK_KEY_E)) {
        editor_move_tab_dedicated(app, NULL);
        return TRUE;
    }

    /* Ctrl+Shift+R: toggle Remote (SSH) view */
    if ((state & GDK_CONTROL_MASK) &&
        (state & GDK_SHIFT_MASK) &&
        (keyval == GDK_KEY_r || keyval == GDK_KEY_R)) {
        toggle_remote_view(app);
        return TRUE;
    }

    /* Ctrl+R or F2: rename current note / notebook if notes view is active */
    if (((state & GDK_CONTROL_MASK) && !(state & GDK_SHIFT_MASK) && (keyval == GDK_KEY_r || keyval == GDK_KEY_R)) ||
        keyval == GDK_KEY_F2) {
        if (app->sidebar_view == SIDEBAR_VIEW_NOTES) {
            notes_edit_selected(app);
            return TRUE;
        }
    }

    /* Ctrl+T: terminal pane beside last running one;
     * Ctrl+Shift+T: new terminal row below running ones */
    if ((state & GDK_CONTROL_MASK) == 0 ||
        (keyval != GDK_KEY_t && keyval != GDK_KEY_T)) {
        return FALSE;
    }

#ifdef HAVE_VTE
    /* Each pane gets its own shell in the current directory */
    gboolean was_term_vis = gtk_widget_get_visible(app->term_scroll);
    if (!was_term_vis) {
        app_emit(app, APP_EVENT_LAYOUT_CHANGED);
        app->applying_layout = TRUE;
    }
    if (app->right_paned != NULL) {
        gtk_widget_set_visible(app->right_paned, TRUE);
    }
    if (!was_term_vis) {
        gtk_widget_set_visible(app->term_scroll, TRUE);
    }
    term_spawn_pane(app, keyval == GDK_KEY_T /* new row below? */);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    if (!was_term_vis) {
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    }
#else
    /* No VTE installed: the panel is a notice instead of a terminal */
    gboolean was_term_vis = gtk_widget_get_visible(app->term_scroll);
    app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    app->applying_layout = TRUE;
    gtk_widget_set_visible(app->term_scroll, !was_term_vis);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
#endif
    return TRUE;
}


/* Remember the viewer/terminal split as a fraction of pane size */
void layout_save_frac(AppData* app) {
    if (app->right_paned == NULL) {
        return;
    }
    GtkOrientation o =
        gtk_orientable_get_orientation(GTK_ORIENTABLE(app->right_paned));
    gint len = (o == GTK_ORIENTATION_VERTICAL
                    ? gtk_widget_get_height(app->right_paned)
                    : gtk_widget_get_width(app->right_paned));
    if (len > 40) {
        gint pos = gtk_paned_get_position(GTK_PANED(app->right_paned));
        if (pos > 0 && pos < len) {
            app->right_frac = CLAMP((gdouble)pos / len, 0.05, 0.95);
        }
    }
}

/* Restore saved split proportion once new paned is laid out */
gboolean layout_position_idle(gpointer data) {
    AppData* app = (AppData*)data;
    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    return FALSE;
}

/* Assemble viewer and terminal into a pane pair with given orientation */
void layout_build(AppData* app, GtkOrientation orientation) {
    if (app->right_paned != NULL) {
        gtk_orientable_set_orientation(GTK_ORIENTABLE(app->right_paned), orientation);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
        return;
    }

    GtkWidget* right_paned = gtk_paned_new(orientation);
    gtk_widget_set_hexpand(right_paned, TRUE);
    gtk_widget_set_vexpand(right_paned, TRUE);
    GtkWidget* center_child = (app->editor_paned != NULL) ? app->editor_paned : app->viewer_box;
    gtk_paned_set_start_child(GTK_PANED(right_paned), center_child);
    gtk_paned_set_resize_start_child(GTK_PANED(right_paned), FALSE);
    gtk_paned_set_shrink_start_child(GTK_PANED(right_paned), TRUE);
    gtk_paned_set_end_child(GTK_PANED(right_paned), app->term_scroll);
    gtk_paned_set_resize_end_child(GTK_PANED(right_paned), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(right_paned), TRUE);

    /* Min sizes valid for both orientations */
    gtk_widget_set_size_request(app->term_scroll, 120, 80);

    app->right_paned = right_paned;

    wire_paned_controllers(right_paned, app);

    gtk_paned_set_end_child(GTK_PANED(app->main_paned), right_paned);
    gtk_paned_set_resize_end_child(GTK_PANED(app->main_paned), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(app->main_paned), TRUE);

    /* Restore saved split proportions */
    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
}

/* Ctrl+Shift+L: stack terminal pane under viewer or place it beside it */
void layout_toggle(AppData* app) {
    app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    GtkOrientation next_orient =
        gtk_orientable_get_orientation(GTK_ORIENTABLE(app->right_paned)) ==
                GTK_ORIENTATION_HORIZONTAL
            ? GTK_ORIENTATION_VERTICAL
            : GTK_ORIENTATION_HORIZONTAL;
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
        app->workspaces[app->current_ws].right_orient = next_orient;
    }
    layout_build(app, next_orient);
    if (app->ws_minimap != NULL) {
        gtk_widget_queue_draw(app->ws_minimap);
    }
}

