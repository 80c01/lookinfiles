#include "ui.h"
#include "app_events.h"
#include "workspace.h"
#include "file_list.h"
#include "terminal.h"
#include "notes.h"
#include "editor.h"


gboolean window_is_fullscreen(GtkWindow* win) {
    return gtk_window_is_fullscreen(win);
}

void toggle_fullscreen(GtkWindow* window) {
    if (window_is_fullscreen(window)) {
        gtk_window_unfullscreen(window);
    } else {
        gtk_window_fullscreen(window);
    }
}


gint get_workspace_total_width(AppData* app) {
    if (app == NULL) {
        return 0;
    }
    if (app->workspace_hbox != NULL) {
        gint w = gtk_widget_get_width(app->workspace_hbox);
        if (w > 60) {
            return w;
        }
    }
    if (app->window != NULL) {
        gint w = gtk_widget_get_width(GTK_WIDGET(app->window));
        if (w > 60) {
            return w;
        }
    }
    if (app->main_paned != NULL) {
        gint mw = gtk_widget_get_width(app->main_paned);
        gboolean help_vis = (app->help_window != NULL) && gtk_widget_get_visible(app->help_window);
        gint help_w = (help_vis && app->help_window != NULL) ? gtk_widget_get_width(app->help_window) : 0;
        if (help_vis && help_w <= 0) {
            help_w = 400;
        }
        return mw + help_w;
    }
    return 0;
}


/* Update the active-panel CSS highlight based on which panel owns keyboard focus */
void update_active_panel_highlight(AppData* app) {
    if (app == NULL || app->window == NULL) {
        return;
    }

    GtkWidget* focused = gtk_window_get_focus(app->window);
    GtkWidget* target_panel = NULL;

    GtkWidget* w = focused;
    while (w != NULL) {
        if (app->left_vbox != NULL && w == app->left_vbox) {
            target_panel = app->left_vbox;
            break;
        }
        if (app->viewer_box != NULL && w == app->viewer_box) {
            target_panel = app->viewer_box;
            break;
        }
        if (app->editor_box != NULL && w == app->editor_box) {
            target_panel = app->editor_box;
            break;
        }
        if (app->term_scroll != NULL && w == app->term_scroll) {
            target_panel = app->term_scroll;
            break;
        }
        if (app->help_window != NULL && w == app->help_window) {
            target_panel = app->help_window;
            break;
        }
        w = gtk_widget_get_parent(w);
    }

    if (app->active_panel == target_panel) {
        return;
    }

    if (app->active_panel != NULL) {
        gtk_widget_remove_css_class(app->active_panel, "panel-focused");
    }

    app->active_panel = target_panel;

    if (app->active_panel != NULL) {
        gtk_widget_add_css_class(app->active_panel, "panel-focused");
    }
}

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

/* Show or refresh small overlay badge listing w x h of panes */
void update_pane_dims(GtkWidget* paned_widget, AppData* app) {
    GtkPaned* paned = GTK_PANED(paned_widget);
    GtkWidget* c1 = gtk_paned_get_start_child(paned);
    GtkWidget* c2 = gtk_paned_get_end_child(paned);
    if (c1 == NULL || c2 == NULL ||
        !gtk_widget_get_visible(c1) || !gtk_widget_get_visible(c2)) {
        return; /* nothing to drag on that side (panel hidden) */
    }
    gint w1 = gtk_widget_get_width(c1);
    gint h1 = gtk_widget_get_height(c1);
    gint w2 = gtk_widget_get_width(c2);
    gint h2 = gtk_widget_get_height(c2);
    if (w1 <= 0 || w2 <= 0) {
        return; /* not laid out yet */
    }

    gchar* text = g_strdup_printf("%d \xc3\x97 %d  |  %d \xc3\x97 %d",
                                  w1, h1, w2, h2);
    gtk_label_set_text(GTK_LABEL(app->dim_label), text);
    g_free(text);

    gtk_widget_set_visible(app->dim_window, TRUE);
}

gboolean pane_dims_idle(gpointer data) {
    AppData* app = (AppData*)data;
    if (app->panning && app->dims_paned != NULL) {
        update_pane_dims(app->dims_paned, app);
    }
    return FALSE; /* one-shot */
}

void on_paned_pressed(GtkGestureClick* gesture, gint n_press,
                      gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    AppData* app = (AppData*)data;
    GtkWidget* widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    if (!GTK_IS_PANED(widget)) {
        return;
    }
    GtkWidget* c1 = gtk_paned_get_start_child(GTK_PANED(widget));
    GtkWidget* c2 = gtk_paned_get_end_child(GTK_PANED(widget));
    if (c1 == NULL || c2 == NULL ||
        !gtk_widget_get_visible(c1) || !gtk_widget_get_visible(c2)) {
        return;
    }
    GtkWidget* picked = gtk_widget_pick(widget, x, y, GTK_PICK_DEFAULT);
    gboolean on_own_sep = (picked != NULL &&
                           g_strcmp0(gtk_widget_get_css_name(picked), "separator") == 0 &&
                           gtk_widget_get_parent(picked) == widget);
    if (!on_own_sep) {
        /* Also check grab tolerance around this paned's divider position,
         * unless the click landed on a nested child paned's separator. */
        if (picked != NULL &&
            g_strcmp0(gtk_widget_get_css_name(picked), "separator") == 0 &&
            gtk_widget_get_parent(picked) != widget) {
            return;
        }
        GtkOrientation o = gtk_orientable_get_orientation(GTK_ORIENTABLE(widget));
        gint sep_pos = gtk_paned_get_position(GTK_PANED(widget));
        gdouble coord = (o == GTK_ORIENTATION_VERTICAL) ? y : x;
        if (sep_pos <= 0 || ABS((gint)coord - sep_pos) > 16) {
            return;
        }
    }
    app->panning = TRUE;
    app->dims_paned = widget;
    update_pane_dims(widget, app);
}

static void record_paned_fraction(AppData* app, GtkWidget* widget) {
    if (app == NULL || widget == NULL || !GTK_IS_PANED(widget) ||
        app->switching_ws || app->applying_layout ||
        app->current_ws < 0 || app->current_ws >= app->num_workspaces) {
        return;
    }
    VirtualLayout* ws = &app->workspaces[app->current_ws];

    if (widget == app->main_paned) {
        if (ws->files_vis && (ws->preview_vis || ws->editor_dedicated || ws->term_vis)) {
            gint mw = gtk_widget_get_width(app->main_paned);
            gint pos = gtk_paned_get_position(GTK_PANED(app->main_paned));
            gint total_w = get_workspace_total_width(app);
            if (total_w <= 0) {
                total_w = mw;
            }
            if (total_w > 60 && pos > 0 && pos < total_w) {
                ws->main_frac = CLAMP((gdouble)pos / total_w, 0.05, 0.95);
                if (app->right_paned != NULL && (ws->preview_vis || ws->editor_dedicated) && ws->term_vis &&
                    ws->right_orient == GTK_ORIENTATION_HORIZONTAL) {
                    gint rw = mw - pos;
                    if (rw > 40) {
                        app->applying_layout = TRUE;
                        gtk_paned_set_position(GTK_PANED(app->right_paned),
                                               (gint)(ws->right_frac * rw + 0.5));
                        app->applying_layout = FALSE;
                    }
                }
            }
        }
    } else if (widget == app->editor_paned) {
        if (ws->preview_vis && ws->editor_dedicated) {
            gint len = gtk_widget_get_width(app->editor_paned);
            gint pos = gtk_paned_get_position(GTK_PANED(app->editor_paned));
            if (len > 40 && pos > 0 && pos < len) {
                ws->editor_frac = CLAMP((gdouble)pos / len, 0.05, 0.95);
                app->editor_frac = ws->editor_frac;
            }
        }
    } else if (widget == app->right_paned) {
        if ((ws->preview_vis || ws->editor_dedicated) && ws->term_vis) {
            gint len = (ws->right_orient == GTK_ORIENTATION_VERTICAL)
                           ? gtk_widget_get_height(app->right_paned)
                           : gtk_widget_get_width(app->right_paned);
            gint pos = gtk_paned_get_position(GTK_PANED(app->right_paned));
            if (len > 40 && pos > 0 && pos < len) {
                ws->right_frac = CLAMP((gdouble)pos / len, 0.05, 0.95);
                app->right_frac = ws->right_frac;
            }
        }
    } else {
        GtkWidget* c1 = gtk_paned_get_start_child(GTK_PANED(widget));
        GtkWidget* c2 = gtk_paned_get_end_child(GTK_PANED(widget));
        if (c1 != NULL && c2 != NULL &&
            gtk_widget_get_width(c1) > 0 && gtk_widget_get_width(c2) > 0) {
            GtkOrientation o = gtk_orientable_get_orientation(GTK_ORIENTABLE(widget));
            gint len = (o == GTK_ORIENTATION_VERTICAL)
                           ? gtk_widget_get_height(widget)
                           : gtk_widget_get_width(widget);
            gint pos = gtk_paned_get_position(GTK_PANED(widget));
            if (len > 40 && pos > 0 && pos < len) {
                gdouble* frac = g_new(gdouble, 1);
                *frac = CLAMP((gdouble)pos / len, 0.05, 0.95);
                g_object_set_data_full(G_OBJECT(widget), "term-paned-frac", frac, g_free);
            }
        }
    }
}

static gboolean on_paned_legacy_event(GtkEventControllerLegacy* ctrl,
                                      GdkEvent* event, gpointer data) {
    (void)ctrl;
    AppData* app = (AppData*)data;
    if (app->panning && gdk_event_get_event_type(event) == GDK_BUTTON_RELEASE) {
        if (app->dims_paned != NULL) {
            record_paned_fraction(app, app->dims_paned);
        }
        app->panning = FALSE;
        app->dims_paned = NULL;
        gtk_widget_set_visible(app->dim_window, FALSE);
        app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    }
    return FALSE;
}

void on_paned_notify_position(GObject* object, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    AppData* app = (AppData*)data;
    if (app == NULL || app->switching_ws || app->applying_layout || !app->panning) {
        return;
    }
    GtkWidget* widget = GTK_WIDGET(object);
    if (app->dims_paned == NULL || app->dims_paned == widget) {
        app->dims_paned = widget;
        record_paned_fraction(app, widget);
        g_idle_add(pane_dims_idle, app);
        app_emit(app, APP_EVENT_LAYOUT_CHANGED);
    }
}

void on_paned_released(GtkGestureClick* gesture, gint n_press,
                       gdouble x, gdouble y, gpointer data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    AppData* app = (AppData*)data;
    if (app->panning && app->dims_paned != NULL) {
        record_paned_fraction(app, app->dims_paned);
    }
    app->panning = FALSE;
    app->dims_paned = NULL;
    gtk_widget_set_visible(app->dim_window, FALSE);
    app_emit(app, APP_EVENT_LAYOUT_CHANGED);
}

void wire_paned_controllers(GtkWidget* paned, AppData* app) {
    GtkGesture* click = gtk_gesture_click_new();
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click),
                                               GTK_PHASE_CAPTURE);
    g_signal_connect(click, "pressed", G_CALLBACK(on_paned_pressed), app);
    g_signal_connect(click, "released", G_CALLBACK(on_paned_released), app);
    gtk_widget_add_controller(paned, GTK_EVENT_CONTROLLER(click));

    GtkEventController* legacy = gtk_event_controller_legacy_new();
    gtk_event_controller_set_propagation_phase(legacy, GTK_PHASE_CAPTURE);
    g_signal_connect(legacy, "event", G_CALLBACK(on_paned_legacy_event), app);
    gtk_widget_add_controller(paned, legacy);

    g_signal_connect(paned, "notify::position",
                     G_CALLBACK(on_paned_notify_position), app);
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

