#include "ui.h"
#include "file_list.h"
#include "viewer.h"
#include "minimap.h"
#include "terminal.h"
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

/* Cheat sheet entry: section header, key column, description */
typedef struct {
    const gchar* section;
    const gchar* key;
    const gchar* description;
} KeyHelpEntry;

static const KeyHelpEntry KEY_HELP[] = {
    /* File panel */
    {"File panel", "j / k", "Move down / up"},
    {"File panel", "gg / G", "First / last entry"},
    {"File panel", "Ctrl+F / Ctrl+B", "Page down / up"},
    {"File panel", "Space / l / Right", "Expand / collapse directory (or step into first child)"},
    {"File panel", "Left", "Collapse directory or jump to parent"},
    {"File panel", "/", "Search by substring (n / N: next / previous match)"},
    {"File panel", "Enter / Double-click", "Expand/collapse directory, or open file in an editing tab (Ctrl+Enter to enter directory)"},
    {"File panel", "Backspace", "Go to the parent directory"},
    {"File panel", "i / I", "Toggle hidden files"},
    {"File panel", "o", "Cycle sort: name, date, size, extension"},
    {"File panel", "h", "Show / hide this help panel"},
    /* Viewer panel */
    {"Right panel (viewer)", "j / k", "Scroll a line down / up"},
    {"Right panel (viewer)", "Ctrl+F / Ctrl+B", "Scroll a page down / up"},
    {"Right panel (viewer)", "gg / G", "Jump to the start / end of the file"},
    {"Right panel (viewer)", "/", "Search in the file (n / N: next / previous match)"},
    {"Right panel (viewer)", "m", "Toggle rendered preview / raw text view (Markdown, HTML, PDF, images)"},
    {"Editor tabs", "Ctrl+S", "Save the file open in the current editor tab"},
    {"Editor tabs", "Ctrl+W", "Close the current editor tab (asks when unsaved; a bullet marks changed tabs)"},
    /* Global */
    {"Global", "Alt+1 .. Alt+8", "Switch between virtual workspaces (or click/scroll top-bar minimap)"},
    {"Global", "Alt++ / Alt+-", "Add a new virtual workspace (+) or close the current workspace (-)"},
    {"Global", "Ctrl+T", "Open a terminal pane right of the running ones (exit the shell to close it)"},
    {"Global", "Ctrl+Shift+T", "Open a new terminal pane below the running ones"},
    {"Global", "Alt+Left / Alt+Right", "Cycle the keyboard focus between panels"},
    {"Global", "f", "Toggle fullscreen (Ctrl+F keeps its paging role)"},
    {"Global", "Ctrl+Shift+L", "Stack the terminal pane under the viewer, or put it back beside it (the file list stays fixed)"},
    {"Global", "Ctrl+Q", "Quit"},
};

void toggle_help_panel(AppData* app) {
    if (app == NULL || app->help_window == NULL) {
        return;
    }
    workspace_save_current(app);
    gboolean vis = gtk_widget_get_visible(app->help_window);
    app->applying_layout = TRUE;
    gtk_widget_set_visible(app->help_window, !vis);
    update_path_label(app);
    workspace_apply_positions(app);
    if (vis) {
        if (app->left_vbox == NULL || gtk_widget_get_visible(app->left_vbox)) {
            gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
        } else if (app->viewer_box != NULL && gtk_widget_get_visible(app->viewer_box)) {
            gtk_widget_grab_focus(app->text_view);
        }
    }
}

static void on_help_close_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    toggle_help_panel((AppData*)data);
}

/* In the help panel, h or Escape closes it and gives keyboard focus back to the file list */
gboolean on_help_key_press(GtkEventControllerKey* controller, guint keyval,
                           guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    (void)state;
    AppData* app = (AppData*)data;
    if (keyval == GDK_KEY_h || keyval == GDK_KEY_H || keyval == GDK_KEY_Escape) {
        toggle_help_panel(app);
        return TRUE;
    }
    return FALSE;
}

/* Build internal help panel toggled by 'h' or the '? Help' top bar button */
void create_help_panel(GtkWindow* parent, AppData* app) {
    (void)parent;
    GtkWidget* panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(panel, "help-panel");
    gtk_widget_set_vexpand(panel, TRUE);
    gtk_widget_set_hexpand(panel, FALSE);
    gtk_widget_set_size_request(panel, 400, -1);
    gtk_widget_set_visible(panel, FALSE);

    GtkWidget* header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_name(header_box, "help-header");
    GtkWidget* title_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title_label),
                         "<b>⌨  Keyboard Shortcuts</b>");
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(title_label, TRUE);

    GtkWidget* close_btn = gtk_button_new_with_label("✕");
    gtk_button_set_has_frame(GTK_BUTTON(close_btn), FALSE);
    gtk_widget_set_focusable(close_btn, FALSE);
    gtk_widget_add_css_class(close_btn, "help-close-btn");
    gtk_widget_set_tooltip_text(close_btn, "Close help panel (h)");
    g_signal_connect(close_btn, "clicked",
                     G_CALLBACK(on_help_close_clicked), app);

    gtk_box_append(GTK_BOX(header_box), title_label);
    gtk_box_append(GTK_BOX(header_box), close_btn);
    gtk_box_append(GTK_BOX(panel), header_box);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkGrid* grid = GTK_GRID(gtk_grid_new());
    gtk_grid_set_row_spacing(grid, 6);
    gtk_grid_set_column_spacing(grid, 14);
    gtk_widget_set_margin_top(GTK_WIDGET(grid), 12);
    gtk_widget_set_margin_bottom(GTK_WIDGET(grid), 14);
    gtk_widget_set_margin_start(GTK_WIDGET(grid), 14);
    gtk_widget_set_margin_end(GTK_WIDGET(grid), 14);

    const gchar* current_section = NULL;
    gint row = 0;
    for (guint i = 0; i < G_N_ELEMENTS(KEY_HELP); i++) {
        if (current_section == NULL ||
            g_strcmp0(current_section, KEY_HELP[i].section) != 0) {
            current_section = KEY_HELP[i].section;
            gchar* header =
                g_strdup_printf("<span foreground=\"#4fc1ff\" weight=\"bold\">%s</span>",
                                current_section);
            GtkWidget* section_label = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(section_label), header);
            gtk_widget_set_halign(section_label, GTK_ALIGN_START);
            if (row > 0) {
                gtk_widget_set_margin_top(section_label, 10);
            }
            g_free(header);
            gtk_grid_attach(grid, section_label, 0, row++, 2, 1);
        }
        GtkWidget* key_label = gtk_label_new(NULL);
        gchar* key_markup =
            g_strdup_printf("<span foreground=\"#dcdcaa\"><tt><b>%s</b></tt></span>",
                            KEY_HELP[i].key);
        gtk_label_set_markup(GTK_LABEL(key_label), key_markup);
        g_free(key_markup);
        gtk_widget_set_halign(key_label, GTK_ALIGN_START);
        gtk_widget_set_valign(key_label, GTK_ALIGN_START);

        GtkWidget* desc_label = gtk_label_new(KEY_HELP[i].description);
        gtk_label_set_wrap(GTK_LABEL(desc_label), TRUE);
        gtk_label_set_xalign(GTK_LABEL(desc_label), 0.0f);
        gtk_widget_set_hexpand(desc_label, TRUE);
        gtk_widget_set_halign(desc_label, GTK_ALIGN_FILL);
        gtk_widget_set_valign(desc_label, GTK_ALIGN_START);

        gtk_grid_attach(grid, key_label, 0, row, 1, 1);
        gtk_grid_attach(grid, desc_label, 1, row, 1, 1);
        row++;
    }

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(grid));
    gtk_box_append(GTK_BOX(panel), scroll);

    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed",
                     G_CALLBACK(on_help_key_press), app);
    gtk_widget_add_controller(panel, key_ctrl);

    app->help_window = panel;
}

void focus_adjacent_panel(gint step, AppData* app) {
    GPtrArray* panels = g_ptr_array_new();
    gint count;
    gint current = -1;

    if (app->left_vbox == NULL || gtk_widget_get_visible(app->left_vbox)) {
        g_ptr_array_add(panels, GTK_WIDGET(app->tree_view));
    }
    if (app->viewer_box == NULL || gtk_widget_get_visible(app->viewer_box)) {
        g_ptr_array_add(panels, app->text_view);
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
        workspace_save_current(app);
        app->applying_layout = TRUE;
    }
    if (app->right_paned != NULL) {
        gtk_widget_set_visible(app->right_paned, TRUE);
    }
    if (!was_term_vis) {
        gtk_widget_set_visible(app->term_scroll, TRUE);
    }
    term_spawn_pane(app, keyval == GDK_KEY_T /* new row below? */);
    update_path_label(app);
    if (!was_term_vis) {
        workspace_apply_positions(app);
    }
#else
    /* No VTE installed: the panel is a notice instead of a terminal */
    gboolean was_term_vis = gtk_widget_get_visible(app->term_scroll);
    workspace_save_current(app);
    app->applying_layout = TRUE;
    gtk_widget_set_visible(app->term_scroll, !was_term_vis);
    update_path_label(app);
    workspace_apply_positions(app);
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
        if (ws->files_vis && (ws->preview_vis || ws->term_vis)) {
            gint mw = gtk_widget_get_width(app->main_paned);
            gint pos = gtk_paned_get_position(GTK_PANED(app->main_paned));
            if (mw > 60 && pos > 0 && pos < mw) {
                ws->main_frac = CLAMP((gdouble)pos / mw, 0.05, 0.95);
                if (app->right_paned != NULL && ws->preview_vis && ws->term_vis &&
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
    } else if (widget == app->right_paned) {
        if (ws->preview_vis && ws->term_vis) {
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
        workspace_save_current(app);
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
        workspace_save_current(app);
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
    workspace_save_current(app);
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
    workspace_apply_positions(app);
    return FALSE;
}

/* Assemble viewer and terminal into a pane pair with given orientation */
void layout_build(AppData* app, GtkOrientation orientation) {
    if (app->right_paned != NULL) {
        gtk_orientable_set_orientation(GTK_ORIENTABLE(app->right_paned), orientation);
        workspace_apply_positions(app);
        return;
    }

    GtkWidget* right_paned = gtk_paned_new(orientation);
    gtk_widget_set_hexpand(right_paned, TRUE);
    gtk_widget_set_vexpand(right_paned, TRUE);
    gtk_paned_set_start_child(GTK_PANED(right_paned), app->viewer_box);
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
    workspace_apply_positions(app);
}

/* Ctrl+Shift+L: stack terminal pane under viewer or place it beside it */
void layout_toggle(AppData* app) {
    workspace_save_current(app);
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

#define WS_CELL_W   32
#define WS_ADD_W    22
#define WS_CELL_H   20
#define WS_CELL_GAP 5
#define WS_PAD_X    2
#define WS_PAD_Y    2

static GHashTable* clone_ws_paths(GHashTable* src) {
    GHashTable* copy = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (src != NULL) {
        GHashTableIter iter;
        gpointer key;
        g_hash_table_iter_init(&iter, src);
        while (g_hash_table_iter_next(&iter, &key, NULL)) {
            g_hash_table_add(copy, g_strdup((const gchar*)key));
        }
    }
    return copy;
}

static gboolean ws_paths_equal(GHashTable* a, GHashTable* b) {
    if (a == b) {
        return TRUE;
    }
    if (a == NULL || b == NULL) {
        return FALSE;
    }
    if (g_hash_table_size(a) != g_hash_table_size(b)) {
        return FALSE;
    }
    GHashTableIter iter;
    gpointer key;
    g_hash_table_iter_init(&iter, a);
    while (g_hash_table_iter_next(&iter, &key, NULL)) {
        if (!g_hash_table_contains(b, key)) {
            return FALSE;
        }
    }
    return TRUE;
}

static void ws_minimap_update_size(AppData* app) {
    if (app == NULL || app->ws_minimap == NULL) {
        return;
    }
    gboolean has_add = (app->num_workspaces < MAX_VIRTUAL_LAYOUTS);
    gint total_w = WS_PAD_X * 2 + app->num_workspaces * WS_CELL_W +
                   (app->num_workspaces - 1) * WS_CELL_GAP +
                   (has_add ? (WS_CELL_GAP + WS_ADD_W) : 0);
    gint total_h = WS_CELL_H + WS_PAD_Y * 2;
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(app->ws_minimap), total_w);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(app->ws_minimap), total_h);
    gtk_widget_queue_draw(app->ws_minimap);
}

static void init_workspace_slot(AppData* app, gint idx, const gchar* dir) {
    VirtualLayout* ws = &app->workspaces[idx];
    memset(ws, 0, sizeof(VirtualLayout));
    ws->files_vis = TRUE;
    ws->preview_vis = TRUE;
    ws->term_vis = FALSE;
    ws->right_orient = GTK_ORIENTATION_HORIZONTAL;
    ws->main_frac = 0.25;
    ws->right_frac = 0.70;
    ws->base_dir = g_strdup(dir != NULL ? dir : app->base_dir);
    ws->expanded_paths = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    ws->selected_path = NULL;
    ws->show_hidden = app->show_hidden;
    ws->sort_mode = app->sort_mode;
    ws->notebook_page = 0;
    ws->render_raw = FALSE;
#ifdef HAVE_VTE
    if (app->term_stack != NULL) {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_hexpand(box, TRUE);
        gtk_widget_set_vexpand(box, TRUE);
        gtk_stack_add_child(GTK_STACK(app->term_stack), box);
        ws->term_box = box;
    }
    ws->last_focused_vte = NULL;
#endif
}

void workspaces_init(AppData* app) {
    app->num_workspaces = 1;
    app->current_ws = 0;
    app->hover_ws = -1;
    app->switching_ws = FALSE;
    app->applying_layout = FALSE;
    app->dir_scan_pending = FALSE;
    app->layout_tick_id = 0;
    app->layout_apply_frames = 0;
    app->layout_retry_count = 0;
    init_workspace_slot(app, 0, app->base_dir);
#ifdef HAVE_VTE
    app->term_box = app->workspaces[0].term_box;
    if (app->term_stack != NULL && app->term_box != NULL) {
        gtk_stack_set_visible_child(GTK_STACK(app->term_stack), app->term_box);
    }
#endif
}

void workspace_save_current(AppData* app) {
    if (app == NULL || app->switching_ws ||
        app->current_ws < 0 || app->current_ws >= app->num_workspaces ||
        app->left_vbox == NULL || app->viewer_box == NULL || app->term_scroll == NULL) {
        return;
    }
    VirtualLayout* ws = &app->workspaces[app->current_ws];
    ws->files_vis = gtk_widget_get_visible(app->left_vbox);
    ws->preview_vis = gtk_widget_get_visible(app->viewer_box);
    ws->term_vis = gtk_widget_get_visible(app->term_scroll);
    ws->show_hidden = app->show_hidden;
    ws->sort_mode = app->sort_mode;
    ws->render_raw = app->render_raw;
    if (app->notebook != NULL) {
        gint cur_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
        if (cur_page >= 0) {
            ws->notebook_page = cur_page;
        }
    }

    g_free(ws->base_dir);
    ws->base_dir = g_strdup(app->base_dir);

    if (ws->expanded_paths != NULL) {
        g_hash_table_destroy(ws->expanded_paths);
    }
    ws->expanded_paths = clone_ws_paths(app->expanded_paths);

    if (app->selection != NULL && !app->dir_scan_pending) {
        GtkTreeIter it;
        g_free(ws->selected_path);
        ws->selected_path = NULL;
        if (gtk_tree_selection_get_selected(app->selection, NULL, &it)) {
            gtk_tree_model_get(app->model, &it, COL_PATH, &ws->selected_path, -1);
        }
    }

#ifdef HAVE_VTE
    ws->last_focused_vte = app->last_focused_vte;
#endif

    if (app->right_paned != NULL) {
        ws->right_orient =
            gtk_orientable_get_orientation(GTK_ORIENTABLE(app->right_paned));
    }

    if (app->ws_minimap != NULL) {
        gtk_widget_queue_draw(app->ws_minimap);
    }
}

#ifdef HAVE_VTE
static void apply_term_paned_positions_recursive(GtkWidget* widget) {
    if (widget == NULL || !gtk_widget_get_visible(widget)) {
        return;
    }
    if (GTK_IS_PANED(widget)) {
        gdouble* frac = (gdouble*)g_object_get_data(G_OBJECT(widget), "term-paned-frac");
        if (frac != NULL) {
            GtkOrientation o = gtk_orientable_get_orientation(GTK_ORIENTABLE(widget));
            gint len = (o == GTK_ORIENTATION_VERTICAL)
                           ? gtk_widget_get_height(widget)
                           : gtk_widget_get_width(widget);
            if (len > 40) {
                gtk_paned_set_position(GTK_PANED(widget),
                                       (gint)((*frac) * len + 0.5));
            }
        }
    }
    for (GtkWidget* child = gtk_widget_get_first_child(widget);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        apply_term_paned_positions_recursive(child);
    }
}
#endif

static gboolean apply_ws_positions_now(AppData* app) {
    if (app == NULL || app->current_ws < 0 || app->current_ws >= app->num_workspaces) {
        return TRUE;
    }
    VirtualLayout* ws = &app->workspaces[app->current_ws];
    gboolean ready = TRUE;
    gint mw = (app->main_paned != NULL) ? gtk_widget_get_width(app->main_paned) : 0;
    gint mh = (app->main_paned != NULL) ? gtk_widget_get_height(app->main_paned) : 0;

    if (app->main_paned != NULL && ws->files_vis && (ws->preview_vis || ws->term_vis)) {
        if (mw > 60) {
            gtk_paned_set_position(GTK_PANED(app->main_paned),
                                   (gint)(ws->main_frac * mw + 0.5));
        } else {
            ready = FALSE;
        }
    }

    if (app->right_paned != NULL && (ws->preview_vis || ws->term_vis)) {
        gtk_paned_set_resize_start_child(GTK_PANED(app->right_paned), FALSE);
        gtk_paned_set_resize_end_child(GTK_PANED(app->right_paned), TRUE);

        gint len = (ws->right_orient == GTK_ORIENTATION_VERTICAL)
                       ? gtk_widget_get_height(app->right_paned)
                       : gtk_widget_get_width(app->right_paned);
        if (ws->right_orient == GTK_ORIENTATION_HORIZONTAL) {
            gint target_rw = (ws->files_vis && mw > 60)
                                 ? (mw - (gint)(ws->main_frac * mw + 0.5))
                                 : mw;
            if (app->layout_apply_frames <= 1 || len <= 40) {
                len = target_rw;
            }
        } else if (len <= 40 && mh > 40) {
            len = mh;
        }

        if (len > 40) {
            gtk_paned_set_position(GTK_PANED(app->right_paned),
                                   (gint)(ws->right_frac * len + 0.5));
        } else {
            ready = FALSE;
        }
    }

#ifdef HAVE_VTE
    if (ws->term_vis && ws->term_box != NULL) {
        apply_term_paned_positions_recursive(ws->term_box);
    }
#endif
    return ready;
}

static gboolean workspace_apply_positions_tick(GtkWidget* widget,
                                               GdkFrameClock* frame_clock,
                                               gpointer data) {
    (void)widget;
    (void)frame_clock;
    AppData* app = (AppData*)data;
    app->layout_apply_frames++;

    if (app->layout_apply_frames <= 3) {
        gboolean ready = apply_ws_positions_now(app);
        if (!ready && app->layout_apply_frames == 2 && app->layout_retry_count < 10) {
            app->layout_retry_count++;
            app->layout_apply_frames = 1; /* wait until widgets are allocated */
        }
        return G_SOURCE_CONTINUE;
    }

    app->applying_layout = FALSE;
    app->layout_tick_id = 0;
    if (app->ws_minimap != NULL) {
        gtk_widget_queue_draw(app->ws_minimap);
    }
    return G_SOURCE_REMOVE;
}

void workspace_apply_positions(AppData* app) {
    if (app == NULL || app->main_paned == NULL) {
        return;
    }
    app->applying_layout = TRUE;
    app->layout_apply_frames = 0;
    app->layout_retry_count = 0;
    apply_ws_positions_now(app);
    if (app->layout_tick_id == 0) {
        app->layout_tick_id = gtk_widget_add_tick_callback(
            app->main_paned, workspace_apply_positions_tick, app, NULL);
    }
}

void workspace_switch(AppData* app, gint idx) {
    if (app == NULL || idx < 0 || idx >= app->num_workspaces) {
        return;
    }
    if (idx != app->current_ws) {
        workspace_save_current(app);
    }
    app->switching_ws = TRUE;
    app->applying_layout = TRUE;
    app->current_ws = idx;

    VirtualLayout* ws = &app->workspaces[idx];
    gchar* target_path = g_strdup(ws->selected_path);
    app->right_frac = ws->right_frac;

#ifdef HAVE_VTE
    app->term_box = ws->term_box;
    app->last_focused_vte = ws->last_focused_vte;
    if (app->term_stack != NULL && ws->term_box != NULL) {
        gtk_stack_set_visible_child(GTK_STACK(app->term_stack), ws->term_box);
    }
#endif

    if (app->left_vbox != NULL) {
        gtk_widget_set_visible(app->left_vbox, ws->files_vis);
    }
    if (app->viewer_box != NULL) {
        gtk_widget_set_visible(app->viewer_box, ws->preview_vis);
    }
    if (app->right_paned != NULL) {
        gtk_orientable_set_orientation(GTK_ORIENTABLE(app->right_paned), ws->right_orient);
        gtk_widget_set_visible(app->right_paned, ws->preview_vis || ws->term_vis);
    }
    if (app->term_scroll != NULL) {
        gtk_widget_set_visible(app->term_scroll, ws->term_vis);
    }

#ifdef HAVE_VTE
    if (ws->term_vis && ws->term_box != NULL) {
        GPtrArray* panes = term_visible_panes(app);
        if (panes->len == 0) {
            term_spawn_pane(app, FALSE);
        }
        g_ptr_array_unref(panes);
    }
#endif

    gboolean need_rescan = app->dir_scan_pending ||
                           (g_strcmp0(app->base_dir, ws->base_dir) != 0) ||
                           (app->show_hidden != ws->show_hidden) ||
                           (app->sort_mode != ws->sort_mode) ||
                           !ws_paths_equal(app->expanded_paths, ws->expanded_paths);

    app->show_hidden = ws->show_hidden;
    app->sort_mode = ws->sort_mode;
    if (ws->base_dir != NULL && g_strcmp0(app->base_dir, ws->base_dir) != 0) {
        g_free(app->base_dir);
        app->base_dir = g_strdup(ws->base_dir);
        monitor_directory(app);
    }
    if (app->expanded_paths != NULL) {
        g_hash_table_destroy(app->expanded_paths);
    }
    app->expanded_paths = clone_ws_paths(ws->expanded_paths);

    if (app->render_raw != ws->render_raw) {
        app->render_raw = ws->render_raw;
        app->file_dirty = TRUE;
    }
    if (app->notebook != NULL) {
        gint n_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
        gint target_page = (ws->notebook_page >= 0 && ws->notebook_page < n_pages)
                               ? ws->notebook_page
                               : 0;
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), target_page);
    }

    update_path_label(app);

    if (need_rescan) {
        refresh_listing_with_path(app, target_path);
    } else if (target_path != NULL) {
        if (!select_by_path(app, target_path)) {
            select_first_row(app);
        }
    } else {
        select_first_row(app);
    }

    minimap_update_visibility(app);
    workspace_apply_positions(app);
    app->switching_ws = FALSE;
    workspace_save_current(app);
    g_free(target_path);

    if (ws->files_vis && app->tree_view != NULL) {
        gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
    } else if (ws->preview_vis && app->text_view != NULL) {
        gtk_widget_grab_focus(app->text_view);
    }
}

void workspace_add(AppData* app) {
    if (app == NULL || app->num_workspaces >= MAX_VIRTUAL_LAYOUTS) {
        return;
    }
    /* Save the current workspace intact before creating the new one */
    workspace_save_current(app);

    gint new_idx = app->num_workspaces;
    init_workspace_slot(app, new_idx, app->base_dir);
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces) {
        VirtualLayout* cur = &app->workspaces[app->current_ws];
        VirtualLayout* nw = &app->workspaces[new_idx];
        nw->files_vis = cur->files_vis;
        nw->preview_vis = cur->preview_vis;
        nw->right_orient = cur->right_orient;
        nw->main_frac = cur->main_frac;
        nw->right_frac = cur->right_frac;
        nw->notebook_page = cur->notebook_page;
        nw->render_raw = cur->render_raw;
        g_free(nw->selected_path);
        nw->selected_path = g_strdup(cur->selected_path);
        if (nw->expanded_paths != NULL) {
            g_hash_table_destroy(nw->expanded_paths);
        }
        nw->expanded_paths = clone_ws_paths(cur->expanded_paths);
    }
    app->num_workspaces++;
    ws_minimap_update_size(app);
    workspace_switch(app, new_idx);
}

void workspace_remove(AppData* app, gint idx) {
    if (app == NULL || app->num_workspaces <= 1 ||
        idx < 0 || idx >= app->num_workspaces) {
        return;
    }
    if (idx != app->current_ws) {
        workspace_save_current(app);
    }
    VirtualLayout* rm = &app->workspaces[idx];
    g_free(rm->base_dir);
    if (rm->expanded_paths != NULL) {
        g_hash_table_destroy(rm->expanded_paths);
    }
    g_free(rm->selected_path);
#ifdef HAVE_VTE
    if (app->term_stack != NULL && rm->term_box != NULL) {
        gtk_stack_remove(GTK_STACK(app->term_stack), rm->term_box);
    }
#endif

    for (gint i = idx; i < app->num_workspaces - 1; i++) {
        app->workspaces[i] = app->workspaces[i + 1];
    }
    memset(&app->workspaces[app->num_workspaces - 1], 0, sizeof(VirtualLayout));
    app->num_workspaces--;

    gint target = app->current_ws;
    if (target >= app->num_workspaces) {
        target = app->num_workspaces - 1;
    } else if (idx < app->current_ws) {
        target = app->current_ws - 1;
    }
    app->current_ws = target;
    ws_minimap_update_size(app);
    workspace_switch(app, target);
}

static void draw_rounded_rect(cairo_t* cr, gdouble x, gdouble y,
                              gdouble w, gdouble h, gdouble r) {
    if (w <= 0.0 || h <= 0.0) {
        return;
    }
    if (r > w * 0.5) r = w * 0.5;
    if (r > h * 0.5) r = h * 0.5;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI * 0.5, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, G_PI * 0.5);
    cairo_arc(cr, x + r, y + h - r, r, G_PI * 0.5, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, G_PI * 1.5);
    cairo_close_path(cr);
}

static void on_ws_minimap_draw(GtkDrawingArea* area, cairo_t* cr,
                               int width, int height, gpointer data) {
    (void)area;
    (void)width;
    AppData* app = (AppData*)data;
    gdouble start_y = (height - WS_CELL_H) * 0.5;

    for (gint i = 0; i < app->num_workspaces; i++) {
        VirtualLayout* ws = &app->workspaces[i];
        gboolean is_active = (i == app->current_ws);
        gboolean is_hover = (i == app->hover_ws);
        gdouble bx = WS_PAD_X + i * (WS_CELL_W + WS_CELL_GAP);
        gdouble by = start_y;

        /* Outer desktop frame */
        draw_rounded_rect(cr, bx, by, WS_CELL_W, WS_CELL_H, 3.0);
        if (is_active) {
            cairo_set_source_rgb(cr, 0.11, 0.16, 0.21);
        } else if (is_hover) {
            cairo_set_source_rgb(cr, 0.15, 0.17, 0.19);
        } else {
            cairo_set_source_rgb(cr, 0.08, 0.09, 0.10);
        }
        cairo_fill_preserve(cr);

        if (is_active) {
            cairo_set_source_rgb(cr, 0.0, 0.48, 0.80); /* #007acc */
            cairo_set_line_width(cr, 1.5);
        } else if (is_hover) {
            cairo_set_source_rgb(cr, 0.45, 0.50, 0.56);
            cairo_set_line_width(cr, 1.0);
        } else {
            cairo_set_source_rgb(cr, 0.22, 0.24, 0.27);
            cairo_set_line_width(cr, 1.0);
        }
        cairo_stroke(cr);

        /* Inner miniature panes */
        gdouble ix = bx + 2.5;
        gdouble iy = by + 2.5;
        gdouble iw = WS_CELL_W - 5.0;
        gdouble ih = WS_CELL_H - 5.0;
        gdouble alpha = is_active ? 0.82 : (is_hover ? 0.58 : 0.38);

        gdouble rx = ix;
        gdouble rw = iw;

        if (ws->files_vis) {
            gdouble fw = (ws->preview_vis || ws->term_vis)
                             ? (iw * CLAMP(ws->main_frac, 0.22, 0.72) - 0.8)
                             : iw;
            cairo_set_source_rgba(cr, 0.31, 0.76, 1.0, alpha); /* Files: cyan-blue */
            cairo_rectangle(cr, ix, iy, fw, ih);
            cairo_fill(cr);
            rx = ix + fw + 1.5;
            rw = iw - fw - 1.5;
        }

        if (rw > 1.0 && (ws->preview_vis || ws->term_vis)) {
            if (ws->preview_vis && ws->term_vis) {
                if (ws->right_orient == GTK_ORIENTATION_VERTICAL) {
                    gdouble ph = ih * CLAMP(ws->right_frac, 0.25, 0.75) - 0.8;
                    /* Top: Preview */
                    cairo_set_source_rgba(cr, 0.78, 0.82, 0.88, alpha);
                    cairo_rectangle(cr, rx, iy, rw, ph);
                    cairo_fill(cr);
                    /* Bottom: Terminal */
                    cairo_set_source_rgba(cr, 0.14, 0.82, 0.55, alpha);
                    cairo_rectangle(cr, rx, iy + ph + 1.5, rw, ih - ph - 1.5);
                    cairo_fill(cr);
                } else {
                    gdouble pw = rw * CLAMP(ws->right_frac, 0.25, 0.75) - 0.8;
                    /* Left: Preview */
                    cairo_set_source_rgba(cr, 0.78, 0.82, 0.88, alpha);
                    cairo_rectangle(cr, rx, iy, pw, ih);
                    cairo_fill(cr);
                    /* Right: Terminal */
                    cairo_set_source_rgba(cr, 0.14, 0.82, 0.55, alpha);
                    cairo_rectangle(cr, rx + pw + 1.5, iy, rw - pw - 1.5, ih);
                    cairo_fill(cr);
                }
            } else if (ws->preview_vis) {
                cairo_set_source_rgba(cr, 0.78, 0.82, 0.88, alpha);
                cairo_rectangle(cr, rx, iy, rw, ih);
                cairo_fill(cr);
            } else if (ws->term_vis) {
                cairo_set_source_rgba(cr, 0.14, 0.82, 0.55, alpha);
                cairo_rectangle(cr, rx, iy, rw, ih);
                cairo_fill(cr);
            }
        }
    }

    /* Draw the '+' Add Workspace button at the right end (Ubuntu style) */
    if (app->num_workspaces < MAX_VIRTUAL_LAYOUTS) {
        gboolean add_hover = (app->hover_ws == app->num_workspaces);
        gdouble ax = WS_PAD_X + app->num_workspaces * (WS_CELL_W + WS_CELL_GAP);
        gdouble ay = start_y;

        draw_rounded_rect(cr, ax, ay, WS_ADD_W, WS_CELL_H, 3.0);
        if (add_hover) {
            cairo_set_source_rgb(cr, 0.16, 0.20, 0.25);
        } else {
            cairo_set_source_rgb(cr, 0.10, 0.11, 0.12);
        }
        cairo_fill_preserve(cr);

        if (add_hover) {
            cairo_set_source_rgb(cr, 0.0, 0.48, 0.80);
            cairo_set_line_width(cr, 1.3);
        } else {
            cairo_set_source_rgb(cr, 0.25, 0.28, 0.31);
            cairo_set_line_width(cr, 1.0);
        }
        cairo_stroke(cr);

        /* Plus '+' icon */
        gdouble cx = ax + WS_ADD_W * 0.5;
        gdouble cy = ay + WS_CELL_H * 0.5;
        gdouble arm = 4.0;
        if (add_hover) {
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        } else {
            cairo_set_source_rgb(cr, 0.68, 0.72, 0.76);
        }
        cairo_set_line_width(cr, 1.6);
        cairo_move_to(cr, cx - arm, cy);
        cairo_line_to(cr, cx + arm, cy);
        cairo_move_to(cr, cx, cy - arm);
        cairo_line_to(cr, cx, cy + arm);
        cairo_stroke(cr);
    }
}

/* Returns 0..num_workspaces-1 for a workspace cell, num_workspaces for '+', or -1 */
static gint ws_index_at_x(AppData* app, gdouble x) {
    for (gint i = 0; i < app->num_workspaces; i++) {
        gdouble bx = WS_PAD_X + i * (WS_CELL_W + WS_CELL_GAP);
        if (x >= bx && x <= bx + WS_CELL_W) {
            return i;
        }
    }
    if (app->num_workspaces < MAX_VIRTUAL_LAYOUTS) {
        gdouble ax = WS_PAD_X + app->num_workspaces * (WS_CELL_W + WS_CELL_GAP);
        if (x >= ax && x <= ax + WS_ADD_W) {
            return app->num_workspaces;
        }
    }
    return -1;
}

static void on_ws_minimap_pressed(GtkGestureClick* gesture, gint n_press,
                                  gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)y;
    AppData* app = (AppData*)data;
    guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
    gint idx = ws_index_at_x(app, x);
    if (idx < 0) {
        return;
    }
    if (idx == app->num_workspaces) {
        if (button == GDK_BUTTON_PRIMARY) {
            workspace_add(app);
        }
        return;
    }
    if (button == GDK_BUTTON_MIDDLE || button == GDK_BUTTON_SECONDARY) {
        workspace_remove(app, idx);
    } else if (button == GDK_BUTTON_PRIMARY) {
        workspace_switch(app, idx);
    }
}

static void on_ws_minimap_motion(GtkEventControllerMotion* ctrl,
                                 gdouble x, gdouble y, gpointer data) {
    (void)ctrl;
    (void)y;
    AppData* app = (AppData*)data;
    gint idx = ws_index_at_x(app, x);
    if (idx != app->hover_ws) {
        app->hover_ws = idx;
        gtk_widget_queue_draw(app->ws_minimap);
    }
}

static void on_ws_minimap_leave(GtkEventControllerMotion* ctrl, gpointer data) {
    (void)ctrl;
    AppData* app = (AppData*)data;
    if (app->hover_ws != -1) {
        app->hover_ws = -1;
        gtk_widget_queue_draw(app->ws_minimap);
    }
}

static gboolean on_ws_minimap_scroll(GtkEventControllerScroll* ctrl,
                                     gdouble dx, gdouble dy, gpointer data) {
    (void)ctrl;
    AppData* app = (AppData*)data;
    if (app->num_workspaces <= 1) {
        return FALSE;
    }
    gint step = (dy > 0.0 || dx > 0.0) ? 1 : ((dy < 0.0 || dx < 0.0) ? -1 : 0);
    if (step != 0) {
        gint next = (app->current_ws + step + app->num_workspaces) % app->num_workspaces;
        workspace_switch(app, next);
        return TRUE;
    }
    return FALSE;
}

GtkWidget* create_workspace_minimap(AppData* app) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_valign(area, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(
        area,
        "Workspaces: left-click to switch, click '+' to add new workspace (Alt++), right/middle-click to close (Alt+-)");
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area),
                                   on_ws_minimap_draw, app, NULL);

    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
    g_signal_connect(click, "pressed", G_CALLBACK(on_ws_minimap_pressed), app);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));

    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_ws_minimap_motion), app);
    g_signal_connect(motion, "leave", G_CALLBACK(on_ws_minimap_leave), app);
    gtk_widget_add_controller(area, motion);

    GtkEventController* scroll =
        gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    g_signal_connect(scroll, "scroll", G_CALLBACK(on_ws_minimap_scroll), app);
    gtk_widget_add_controller(area, scroll);

    app->ws_minimap = area;
    ws_minimap_update_size(app);
    return area;
}

/* Panel boundaries and VS Code Dark+ theme styling */
void apply_panel_borders(void) {
    GtkCssProvider* css = gtk_css_provider_new();
    static const gchar* DATA =
        "window, .background {\n"
        "  background-color: #191a1b;\n"
        "  color: #cccccc;\n"
        "}\n"
        "#statusbar {\n"
        "  background-color: #18191a;\n"
        "  padding: 4px 10px;\n"
        "  border-bottom: 1px solid #26282a;\n"
        "  color: #cccccc;\n"
        "}\n"
        "#statusbar label {\n"
        "  color: #cccccc;\n"
        "}\n"
        "button.breadcrumb-item,\n"
        "button.breadcrumb-current {\n"
        "  background: transparent;\n"
        "  border: none;\n"
        "  border-radius: 4px;\n"
        "  padding: 2px 6px;\n"
        "  min-height: 22px;\n"
        "  min-width: 0px;\n"
        "  box-shadow: none;\n"
        "}\n"
        "button.breadcrumb-item label {\n"
        "  color: #9da5b4;\n"
        "  font-size: 0.94em;\n"
        "}\n"
        "button.breadcrumb-item:hover,\n"
        "button.breadcrumb-current:hover {\n"
        "  background-color: #26292c;\n"
        "}\n"
        "button.breadcrumb-item:hover label {\n"
        "  color: #e5e5e5;\n"
        "}\n"
        "button.breadcrumb-current label {\n"
        "  color: #ffffff;\n"
        "  font-weight: bold;\n"
        "  font-size: 0.95em;\n"
        "}\n"
        "label.breadcrumb-sep {\n"
        "  color: #5c6370;\n"
        "  padding: 0px 2px;\n"
        "  font-weight: bold;\n"
        "}\n"
        "button.status-pill {\n"
        "  background-color: #232528;\n"
        "  border: 1px solid #2f3236;\n"
        "  border-radius: 10px;\n"
        "  padding: 2px 10px;\n"
        "  min-height: 20px;\n"
        "  min-width: 0px;\n"
        "  box-shadow: none;\n"
        "}\n"
        "button.status-pill label {\n"
        "  color: #b8bfc6;\n"
        "  font-size: 0.88em;\n"
        "}\n"
        "button.status-pill:hover {\n"
        "  background-color: #2d3136;\n"
        "  border-color: #007acc;\n"
        "}\n"
        "button.status-pill:hover label {\n"
        "  color: #ffffff;\n"
        "}\n"
        "button.status-pill.pill-active {\n"
        "  background-color: #094771;\n"
        "  border-color: #0e639c;\n"
        "}\n"
        "button.status-pill.pill-active label {\n"
        "  color: #ffffff;\n"
        "  font-weight: bold;\n"
        "}\n"
        "paned > separator {\n"
        "  min-width: 2px;\n"
        "  min-height: 2px;\n"
        "  background-color: #232527;\n"
        "}\n"
        "paned > separator:hover {\n"
        "  background-color: #007acc;\n"
        "}\n"
        "#left-sidebar, #left-scroll, #left-scroll viewport, treeview, treeview.view {\n"
        "  background-color: #191a1b;\n"
        "  color: #cccccc;\n"
        "}\n"
        "treeview:hover, #file-tree:hover {\n"
        "  background-color: #252729;\n"
        "}\n"
        "treeview:selected,\n"
        "treeview:selected:focus,\n"
        "treeview.view:selected,\n"
        "treeview.view:selected:focus,\n"
        "#file-tree:selected,\n"
        "#file-tree:selected:focus,\n"
        "#file-tree.view:selected,\n"
        "#file-tree.view:selected:focus,\n"
        "#left-scroll treeview:selected,\n"
        "#left-scroll treeview:selected:focus,\n"
        "#left-sidebar treeview:selected,\n"
        "#left-sidebar treeview:selected:focus {\n"
        "  background-color: #0e639c;\n"
        "  color: #ffffff;\n"
        "  font-weight: bold;\n"
        "}\n"
        "treeview:selected:hover,\n"
        "treeview.view:selected:hover,\n"
        "#file-tree:selected:hover,\n"
        "#file-tree.view:selected:hover,\n"
        "#left-scroll treeview:selected:hover,\n"
        "#left-sidebar treeview:selected:hover {\n"
        "  background-color: #1177bb;\n"
        "  color: #ffffff;\n"
        "  font-weight: bold;\n"
        "}\n"
        "treeview:selected:backdrop,\n"
        "treeview:backdrop:selected,\n"
        "treeview.view:selected:backdrop,\n"
        "treeview.view:backdrop:selected,\n"
        "#file-tree:selected:backdrop,\n"
        "#file-tree:backdrop:selected,\n"
        "#file-tree.view:selected:backdrop,\n"
        "#file-tree.view:backdrop:selected,\n"
        "#left-scroll treeview:selected:backdrop,\n"
        "#left-sidebar treeview:selected:backdrop {\n"
        "  background-color: #04395e;\n"
        "  color: #ffffff;\n"
        "  font-weight: bold;\n"
        "}\n"
        "treeview:selected expander,\n"
        "treeview.view:selected expander,\n"
        "#file-tree:selected expander {\n"
        "  color: #ffffff;\n"
        "}\n"
        "#viewer text, textview text, textview, #right-scroll, #right-scroll viewport {\n"
        "  background-color: #121314;\n"
        "  color: #d4d4d4;\n"
        "}\n"
        "notebook, notebook stack {\n"
        "  background-color: #121314;\n"
        "}\n"
        "notebook header {\n"
        "  background-color: #191a1b;\n"
        "  border: none;\n"
        "  border-bottom: 1px solid #232527;\n"
        "}\n"
        "notebook tab {\n"
        "  background-color: #222426;\n"
        "  color: #969696;\n"
        "  border: none;\n"
        "  border-right: 1px solid #191a1b;\n"
        "  padding: 4px 10px;\n"
        "}\n"
        "notebook tab:checked {\n"
        "  background-color: #121314;\n"
        "  color: #ffffff;\n"
        "  border-top: 2px solid #007acc;\n"
        "}\n"
        "notebook tab:hover:not(:checked) {\n"
        "  background-color: #2b2d30;\n"
        "  color: #cccccc;\n"
        "}\n"
        "#term-scroll, #term-scroll viewport, #term-box, vte-terminal {\n"
        "  background-color: #1f2021;\n"
        "}\n"
        "entry, searchentry {\n"
        "  background-color: #232527;\n"
        "  color: #cccccc;\n"
        "  border: 1px solid #2e3134;\n"
        "  border-radius: 2px;\n"
        "}\n"
        "entry:focus-within, searchentry:focus-within {\n"
        "  border-color: #007acc;\n"
        "}\n"
        "scrollbar trough {\n"
        "  background-color: transparent;\n"
        "  border: none;\n"
        "}\n"
        "scrollbar slider {\n"
        "  background-color: rgba(121, 121, 121, 0.4);\n"
        "  border-radius: 4px;\n"
        "  min-width: 8px;\n"
        "  min-height: 8px;\n"
        "}\n"
        "scrollbar slider:hover {\n"
        "  background-color: rgba(100, 100, 100, 0.7);\n"
        "}\n"
        "scrollbar slider:active {\n"
        "  background-color: rgba(191, 191, 191, 0.4);\n"
        "}\n"
        "#dim-window {\n"
        "  background-color: #191a1b;\n"
        "  border: 1px solid #007acc;\n"
        "  color: #cccccc;\n"
        "  padding: 6px 10px;\n"
        "}\n"
        "#help-panel {\n"
        "  background-color: #191a1b;\n"
        "  border-left: 1px solid #26282a;\n"
        "  color: #cccccc;\n"
        "}\n"
        "#help-header {\n"
        "  background-color: #18191a;\n"
        "  padding: 6px 12px;\n"
        "  border-bottom: 1px solid #26282a;\n"
        "}\n"
        "button.help-close-btn {\n"
        "  background: transparent;\n"
        "  border: none;\n"
        "  border-radius: 4px;\n"
        "  padding: 2px 6px;\n"
        "  min-height: 20px;\n"
        "  min-width: 20px;\n"
        "  box-shadow: none;\n"
        "}\n"
        "button.help-close-btn label {\n"
        "  color: #9da5b4;\n"
        "}\n"
        "button.help-close-btn:hover {\n"
        "  background-color: #2d3136;\n"
        "}\n"
        "button.help-close-btn:hover label {\n"
        "  color: #ffffff;\n"
        "}\n"
        ".code-font text, textview.code-font text, .code-font {\n"
        "  font-family: 'Consolas', 'DejaVu Sans Mono', 'Liberation Mono', 'Menlo', 'Courier New', monospace;\n"
        "}\n";
    gtk_css_provider_load_from_string(css, DATA);
    GdkDisplay* display = gdk_display_get_default();
    if (display != NULL) {
        gtk_style_context_add_provider_for_display(
            display, GTK_STYLE_PROVIDER(css),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    g_object_unref(css);
}
