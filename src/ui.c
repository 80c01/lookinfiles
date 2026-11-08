#include "ui.h"
#include "canvas/canvas_view.h"
#include "video_tab.h"
#include "help_guide.h"
#include "browser.h"
#include "games.h"
#include "widgets/dialog.h"
#include "layout_switcher.h"
#include "preferences.h"
#include "theme/themes.h"
#include "widgets/panels.h"
#include "core/app_events.h"
#include "workspace.h"
#include "file_list.h"
#include "terminal.h"
#include "notes.h"
#include "editor.h"
#include "editor_modes.h"
#include "focus_route.h"
#include "quick_open.h"
#include "project_search.h"
#include "session.h"


static void on_window_notify_focus_widget(GObject* gobject, GParamSpec* pspec, gpointer user_data) {
    (void)gobject;
    (void)pspec;
    AppData* app = (AppData*)user_data;
    if (app != NULL) {
        /* A key sequence started elsewhere does not carry over (g, C-x, Ctrl+W) */
        focus_route_focus_changed(app);
        GtkWidget* focus = app->window != NULL ? gtk_window_get_focus(GTK_WINDOW(app->window)) : NULL;
        for (GList* l = app->editors; l != NULL; l = l->next) {
            EditorTab* t = (EditorTab*)l->data;
            if (t != NULL && t->container != NULL &&
                (focus == NULL || !gtk_widget_is_ancestor(focus, t->container))) {
                editor_modes_cancel_pending(t);
            }
        }
        /* The editor mode shown in the status bar follows the focused panel */
        if (update_active_panel_highlight(app)) {
            app_emit(app, APP_EVENT_STATUS_CHANGED);
        }
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

/* Panels in focus order: the explorer list, the page shown in each notebook
 * (preview notebook first), the terminal panes. Pages get the focus with
 * focus_route_focus_page() (editor view, preview text, list of a list page). */
void focus_adjacent_panel(gint step, AppData* app) {
    GPtrArray* panels = g_ptr_array_new();
    GPtrArray* pages = g_ptr_array_new(); /* the panels that are notebook pages */
    gint current = -1;

    if (app->left_vbox == NULL || gtk_widget_get_visible(app->left_vbox)) {
        g_ptr_array_add(panels, GTK_WIDGET(app->tree_view));
    }
    GPtrArray* nbs = g_ptr_array_new();
    if (app->viewer_box == NULL || gtk_widget_get_visible(app->viewer_box)) {
        g_ptr_array_add(nbs, app->notebook);
    }
    if (app->editor_box != NULL && gtk_widget_get_visible(app->editor_box)) {
        GPtrArray* arr = editor_get_all_notebooks(app);
        for (guint i = 0; i < arr->len; i++) {
            g_ptr_array_add(nbs, g_ptr_array_index(arr, i));
        }
        g_ptr_array_unref(arr);
    }
    for (guint i = 0; i < nbs->len; i++) {
        GtkNotebook* nb = GTK_NOTEBOOK(g_ptr_array_index(nbs, i));
        gint p = gtk_notebook_get_current_page(nb);
        GtkWidget* page = p >= 0 ? gtk_notebook_get_nth_page(nb, p) : NULL;
        if (page != NULL) {
            g_ptr_array_add(panels, page);
            g_ptr_array_add(pages, page);
        }
    }
    g_ptr_array_unref(nbs);
#ifdef HAVE_VTE
    if (gtk_widget_get_visible(app->term_scroll)) {
        GPtrArray* panes = term_visible_panes(app);
        for (guint i = 0; i < panes->len; i++) {
            g_ptr_array_add(panels, g_ptr_array_index(panes, i));
        }
        g_ptr_array_unref(panes);
    }
#endif
    gint count = (gint)panels->len;
    if (count == 0) {
        g_ptr_array_unref(panels);
        g_ptr_array_unref(pages);
        return;
    }

    /* Which panel owns the focus? A focused tab bar counts as its page */
    FocusInfo info;
    focus_route_get(app, &info);
    GtkWidget* w = info.page != NULL ? info.page : info.focus;
    while (w != NULL && current < 0) {
        for (guint i = 0; i < panels->len; i++) {
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

    GtkWidget* target = GTK_WIDGET(g_ptr_array_index(panels, (current + step + count) % count));
    gboolean is_page = FALSE;
    for (guint i = 0; i < pages->len; i++) {
        if (g_ptr_array_index(pages, i) == target) is_page = TRUE;
    }
    if (is_page) {
        focus_route_focus_page(app, target);
    } else {
        gtk_widget_grab_focus(target);
    }
    g_ptr_array_unref(panels);
    g_ptr_array_unref(pages);
}

void focus_next_panel(AppData* app) {
    focus_adjacent_panel(1, app);
}

void focus_prev_panel(AppData* app) {
    focus_adjacent_panel(-1, app);
}

void close_shown_tab(AppData* app) {
    FocusInfo info;
    focus_route_get(app, &info);
    if (!(info.area == FOCUS_AREA_EDITOR && info.tab != NULL) &&
        (canvas_view_close_shown(app) || video_tab_close_shown(app) || help_guide_close_shown(app) || browser_close_shown(app) || games_tab_close_shown(app))) {
        return;
    }
    EditorTab* t = info.area == FOCUS_AREA_EDITOR && info.tab != NULL ? info.tab : editor_current_tab(app);
    if (t != NULL && t->container != NULL && gtk_widget_get_mapped(t->container)) {
        editor_close_tab(app, t);
    }
}

/* The keyboard focus is in a terminal (its pane or a tab of the editor area) */
static gboolean focus_in_terminal(AppData* app) {
#ifdef HAVE_VTE
    GtkWidget* focus = app->window != NULL ? gtk_window_get_focus(GTK_WINDOW(app->window)) : NULL;
    return focus != NULL && gtk_widget_get_ancestor(focus, VTE_TYPE_TERMINAL) != NULL;
#else
    (void)app;
    return FALSE;
#endif
}

gboolean ui_confirm_quit(AppData* app) {
    for (gint i = 0; i < app->num_workspaces; i++) {
        if (!editor_confirm_discard_workspace(app, app->workspaces[i].editors)) {
            return FALSE;
        }
    }
    canvas_view_save(app);
    session_save_now(app);
    return TRUE;
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
        if (ui_confirm_quit(app) && app->main_loop != NULL && g_main_loop_is_running(app->main_loop)) {
            g_main_loop_quit(app->main_loop);
        }
        return TRUE;
    }

    /* Keys the focused editor tab needs (Org-mode C-c prefixes, Alt+arrows) */
    if (editor_modes_claims_key(app, keyval, state)) {
        return FALSE;
    }

    /* Ctrl+= / Ctrl+- / Ctrl+0 zoom the focused Canvas page */
    if (canvas_view_claims_key(app, keyval, state)) {
        return FALSE;
    }

    /* Where the focus is decides what the keys below do */
    FocusInfo info;
    focus_route_get(app, &info);

    /* The focus in an editor tab (or on its tab) of a mode with its own keys */
    EditorTab* cur_tab = info.area == FOCUS_AREA_EDITOR ? info.tab : NULL;
    gboolean is_modal_tab = (cur_tab != NULL &&
                             (cur_tab->mode == EDITOR_MODE_EMACS ||
                              cur_tab->mode == EDITOR_MODE_ORGMODE ||
                              cur_tab->mode == EDITOR_MODE_VIM));

    /* Emacs and Org-mode tabs keep C-r (search backward), C-S-f / C-S-b
     * (select forward / backward) and M-l (downcase word) */
    gboolean is_emacs_tab = is_modal_tab && (cur_tab->mode == EDITOR_MODE_EMACS ||
                                             cur_tab->mode == EDITOR_MODE_ORGMODE);

    /* F1: the user guide (Help tab), from anywhere */
    if (keyval == GDK_KEY_F1 && (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK)) == 0) {
        help_guide_show(app, NULL);
        return TRUE;
    }

#ifdef HAVE_VTE
    /* Ctrl+PgDn / Ctrl+PgUp in a terminal: its pane's next / previous tab */
    if ((state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_ALT_MASK)) == GDK_CONTROL_MASK &&
        (keyval == GDK_KEY_Page_Down || keyval == GDK_KEY_Page_Up) &&
        terminal_step_tab(app, keyval == GDK_KEY_Page_Down ? 1 : -1)) {
        return TRUE;
    }
#endif

    /* Ctrl+Shift+F: search the files of the project (Search tab); before
     * the navigation keys: Vim's Ctrl+F pages down whatever Shift */
    if (!is_emacs_tab && (state & GDK_CONTROL_MASK) && (state & GDK_SHIFT_MASK) && !(state & GDK_ALT_MASK) &&
        (keyval == GDK_KEY_f || keyval == GDK_KEY_F)) {
        project_search_show(app);
        return TRUE;
    }

    /* Quick open: Emacs C-x C-f in the explorer, the viewer and the list pages */
    if (info.nav_keys &&
        nav_keys_lookup(app_nav_style(app), (NavPrefix)app->nav_prefix, keyval, state, NULL) == NAV_QUICK_OPEN) {
        app->nav_prefix = NAV_PREFIX_NONE;
        quick_open_show(app);
        return TRUE;
    }

    /* Navigation keys of the global mode go to the explorer, the viewer and
     * the list pages (Emacs C-s / C-r search there, Vim Ctrl+W starts a
     * window command) */
    if (info.nav_keys &&
        nav_keys_lookup(app_nav_style(app), (NavPrefix)app->nav_prefix, keyval, state, NULL) != NAV_NONE) {
        return FALSE;
    }

    /* Ctrl+= / Ctrl+- / Ctrl+0: interface and code fonts larger, smaller,
     * back to the theme's sizes (Ctrl+Shift+- is Notepad's forward) */
    if ((state & GDK_CONTROL_MASK) && !(state & GDK_ALT_MASK)) {
        gboolean shift = (state & GDK_SHIFT_MASK) != 0;
        if (keyval == GDK_KEY_equal || keyval == GDK_KEY_plus || keyval == GDK_KEY_KP_Add) {
            themes_zoom(app, 1);
            return TRUE;
        }
        if (!shift && (keyval == GDK_KEY_minus || keyval == GDK_KEY_KP_Subtract)) {
            themes_zoom(app, -1);
            return TRUE;
        }
        if (!shift && (keyval == GDK_KEY_0 || keyval == GDK_KEY_KP_0)) {
            themes_zoom(app, 0);
            return TRUE;
        }
        /* Ctrl+,: Preferences, on the last section shown */
        if (!shift && keyval == GDK_KEY_comma) {
            preferences_show(app, NULL);
            return TRUE;
        }
    }

    /* Alt+Left / Alt+Right: panel focus cycling */
    if ((state & GDK_ALT_MASK) != 0 &&
        (keyval == GDK_KEY_Left || keyval == GDK_KEY_Right)) {
        focus_adjacent_panel(keyval == GDK_KEY_Right ? 1 : -1, app);
        return TRUE;
    }

    /* Alt+1 .. Alt+8: switch virtual workspace; Alt++ / Alt+-: add/remove workspace
     * (not with Ctrl: Ctrl+Alt+- goes back in the code navigation history) */
    if ((state & GDK_ALT_MASK) != 0 && (state & GDK_CONTROL_MASK) == 0) {
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
        /* Alt+T: Preferences on Appearance (themes and fonts) */
        if ((keyval == GDK_KEY_t || keyval == GDK_KEY_T) && !(state & GDK_CONTROL_MASK)) {
            preferences_show(app, PREFERENCES_APPEARANCE);
            return TRUE;
        }
        /* Alt+R: recent folders and files */
        if (keyval == GDK_KEY_r || keyval == GDK_KEY_R) {
            quick_open_show_recent(app);
            return TRUE;
        }
        /* Alt+L: workspace layout switcher (Ctrl+Alt+L moves editor tabs;
         * Emacs tabs keep M-l) */
        if (!is_emacs_tab && (keyval == GDK_KEY_l || keyval == GDK_KEY_L) && !(state & GDK_CONTROL_MASK)) {
            layout_switcher_show(app);
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

    /* Ctrl+Shift+B: a new browser tab (Emacs tabs keep it: C-S-b selects
     * backward) */
    if (!is_emacs_tab && (state & GDK_CONTROL_MASK) && (state & GDK_SHIFT_MASK) && !(state & GDK_ALT_MASK) &&
        (keyval == GDK_KEY_b || keyval == GDK_KEY_B)) {
        browser_open(app, NULL);
        return TRUE;
    }

    /* Ctrl+P: quick open (Vim and Emacs tabs keep Ctrl+P; Emacs C-p in the
     * explorer is a navigation key, handled above) */
    /* A terminal keeps Ctrl+P (the shell's previous command) */
    if (!is_modal_tab && (state & GDK_CONTROL_MASK) && !(state & (GDK_SHIFT_MASK | GDK_ALT_MASK)) &&
        (keyval == GDK_KEY_p || keyval == GDK_KEY_P) && !focus_in_terminal(app)) {
        quick_open_show(app);
        return TRUE;
    }

    if (!is_modal_tab) {
        /* Ctrl+S: save current editor tab; Ctrl+W: close current editor tab */
        /* A terminal keeps Ctrl+S (the shell's forward history search) */
        if ((state & GDK_CONTROL_MASK) &&
            !(state & GDK_SHIFT_MASK) &&
            (keyval == GDK_KEY_s || keyval == GDK_KEY_S) && !focus_in_terminal(app)) {
            return editor_save_current(app);
        }
        /* A terminal keeps Ctrl+W (the shell erases the word before) */
        if ((state & GDK_CONTROL_MASK) &&
            (keyval == GDK_KEY_w || keyval == GDK_KEY_W) && !focus_in_terminal(app)) {
            close_shown_tab(app);
            return TRUE;
        }
    }

    /* Ctrl+Alt+Arrow / Ctrl+Alt+HJKL: move the editor tab, or the Canvas tab
     * with the focus */
    if ((state & GDK_CONTROL_MASK) && (state & GDK_ALT_MASK)) {
        EditorMoveDirection dir = (keyval == GDK_KEY_Right || keyval == GDK_KEY_l || keyval == GDK_KEY_L) ? EDITOR_MOVE_RIGHT
                                : (keyval == GDK_KEY_Left || keyval == GDK_KEY_h || keyval == GDK_KEY_H)  ? EDITOR_MOVE_LEFT
                                : (keyval == GDK_KEY_Down || keyval == GDK_KEY_j || keyval == GDK_KEY_J)  ? EDITOR_MOVE_DOWN
                                : (keyval == GDK_KEY_Up || keyval == GDK_KEY_k || keyval == GDK_KEY_K)    ? EDITOR_MOVE_UP
                                                                                                          : (EditorMoveDirection)-1;
        if ((gint)dir >= 0 && !focus_in_terminal(app)) {
            FocusInfo info;
            focus_route_get(app, &info);
            if (info.tab == NULL && info.page != NULL && editor_move_page_direction(app, info.page, dir)) {
                return TRUE;
            }
        }
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
        /* Ctrl+Alt+N: line numbers of the current editor tab */
        if (keyval == GDK_KEY_n || keyval == GDK_KEY_N) {
            editor_toggle_line_numbers(app, NULL);
            return TRUE;
        }
    }

    /* Ctrl+Shift+R: toggle Remote (SSH) view */
    if ((state & GDK_CONTROL_MASK) &&
        (state & GDK_SHIFT_MASK) &&
        (keyval == GDK_KEY_r || keyval == GDK_KEY_R)) {
        toggle_remote_view(app);
        return TRUE;
    }

    /* Ctrl+R or F2: rename current note / notebook if notes view is active */
    if ((!is_emacs_tab && (state & GDK_CONTROL_MASK) && !(state & GDK_SHIFT_MASK) &&
         (keyval == GDK_KEY_r || keyval == GDK_KEY_R)) ||
        keyval == GDK_KEY_F2) {
        if (app->sidebar_view == SIDEBAR_VIEW_NOTES) {
            notes_edit_selected(app);
            return TRUE;
        }
    }

    /* Ctrl+T: new terminal tab; Ctrl+Shift+T: new terminal pane below the
     * focused one */
    if ((state & GDK_CONTROL_MASK) == 0 ||
        (keyval != GDK_KEY_t && keyval != GDK_KEY_T)) {
        return FALSE;
    }

    terminal_open(app, keyval == GDK_KEY_T /* new row below? */, NULL);
    return TRUE;
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
    workspace_minimap_redraw(app);
}

