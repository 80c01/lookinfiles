#include "ui.h"
#include "file_list.h"
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
    {"Global", "Ctrl+T", "Open a terminal pane right of the running ones (exit the shell to close it)"},
    {"Global", "Ctrl+Shift+T", "Open a new terminal pane below the running ones"},
    {"Global", "Alt+Left / Alt+Right", "Cycle the keyboard focus between panels"},
    {"Global", "f", "Toggle fullscreen (Ctrl+F keeps its paging role)"},
    {"Global", "Ctrl+Shift+L", "Stack the terminal pane under the viewer, or put it back beside it (the file list stays fixed)"},
    {"Global", "Ctrl+Q", "Quit"},
};

/* In the help panel, any key closes it and gives keyboard focus back to the file list */
gboolean on_help_key_press(GtkEventControllerKey* controller, guint keyval,
                           guint keycode, GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keyval;
    (void)keycode;
    (void)state;
    AppData* app = (AppData*)data;
    gtk_widget_set_visible(app->help_window, FALSE);
    gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
    return TRUE;
}

/* Build floating cheat sheet toggled by 'h' */
void create_help_panel(GtkWindow* parent, AppData* app) {
    GtkWidget* win = gtk_window_new();
    gtk_widget_set_name(win, "help-window");
    gtk_window_set_title(GTK_WINDOW(win),
                         "Key bindings - press h or any key to close");
    gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    gtk_window_set_hide_on_close(GTK_WINDOW(win), TRUE);

    GtkGrid* grid = GTK_GRID(gtk_grid_new());
    gtk_grid_set_row_spacing(grid, 3);
    gtk_grid_set_column_spacing(grid, 16);
    gtk_widget_set_margin_top(GTK_WIDGET(grid), 14);
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
                g_strdup_printf("<span weight=\"bold\">%s</span>\n",
                                current_section);
            GtkWidget* section_label = gtk_label_new(NULL);
            gtk_label_set_markup(GTK_LABEL(section_label), header);
            gtk_widget_set_halign(section_label, GTK_ALIGN_START);
            g_free(header);
            gtk_grid_attach(grid, section_label, 0, row++, 2, 1);
        }
        GtkWidget* key_label = gtk_label_new(NULL);
        gchar* key_markup =
            g_strdup_printf("<tt><b>%s</b></tt>", KEY_HELP[i].key);
        gtk_label_set_markup(GTK_LABEL(key_label), key_markup);
        g_free(key_markup);
        gtk_widget_set_halign(key_label, GTK_ALIGN_START);
        GtkWidget* desc_label = gtk_label_new(KEY_HELP[i].description);
        gtk_widget_set_halign(desc_label, GTK_ALIGN_START);
        gtk_grid_attach(grid, key_label, 0, row, 1, 1);
        gtk_grid_attach(grid, desc_label, 1, row, 1, 1);
        row++;
    }

    gtk_window_set_child(GTK_WINDOW(win), GTK_WIDGET(grid));

    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed",
                     G_CALLBACK(on_help_key_press), app);
    gtk_widget_add_controller(win, key_ctrl);

    app->help_window = win;
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
    if (app->right_paned != NULL) {
        gtk_widget_set_visible(app->right_paned, TRUE);
    }
    if (!gtk_widget_get_visible(app->term_scroll)) {
        gtk_widget_set_visible(app->term_scroll, TRUE);
    }
    term_spawn_pane(app, keyval == GDK_KEY_T /* new row below? */);
    update_path_label(app);
#else
    /* No VTE installed: the panel is a notice instead of a terminal */
    gtk_widget_set_visible(app->term_scroll,
                           !gtk_widget_get_visible(app->term_scroll));
    update_path_label(app);
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
    GtkWidget* picked = gtk_widget_pick(widget, x, y, GTK_PICK_DEFAULT);
    if (picked == NULL || g_strcmp0(gtk_widget_get_css_name(picked), "separator") != 0) {
        return;
    }
    app->panning = TRUE;
    app->dims_paned = widget;
    update_pane_dims(widget, app);
}

void on_paned_notify_position(GObject* object, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    AppData* app = (AppData*)data;
    if (app->panning) { /* ignore programmatic set_position calls */
        app->dims_paned = GTK_WIDGET(object);
        g_idle_add(pane_dims_idle, app);
    }
}

void on_paned_released(GtkGestureClick* gesture, gint n_press,
                       gdouble x, gdouble y, gpointer data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    AppData* app = (AppData*)data;
    app->panning = FALSE;
    gtk_widget_set_visible(app->dim_window, FALSE);
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
    if (len > 0) {
        app->right_frac = (gdouble)gtk_paned_get_position(
            GTK_PANED(app->right_paned)) / len;
    }
}

/* Restore saved split proportion once new paned is laid out */
gboolean layout_position_idle(gpointer data) {
    AppData* app = (AppData*)data;
    GtkOrientation o =
        gtk_orientable_get_orientation(GTK_ORIENTABLE(app->right_paned));
    gint len = (o == GTK_ORIENTATION_VERTICAL
                    ? gtk_widget_get_height(app->right_paned)
                    : gtk_widget_get_width(app->right_paned));
    if (len > 0) {
        gtk_paned_set_position(GTK_PANED(app->right_paned),
                               (gint)(app->right_frac * len));
        return FALSE; /* done */
    }
    return TRUE; /* try again next idle */
}

/* Assemble viewer and terminal into a pane pair with given orientation */
void layout_build(AppData* app, GtkOrientation orientation) {
    if (app->right_paned != NULL) {
        gtk_orientable_set_orientation(GTK_ORIENTABLE(app->right_paned), orientation);
        g_idle_add(layout_position_idle, app);
        return;
    }

    GtkWidget* right_paned = gtk_paned_new(orientation);
    gtk_widget_set_hexpand(right_paned, TRUE);
    gtk_widget_set_vexpand(right_paned, TRUE);
    gtk_paned_set_start_child(GTK_PANED(right_paned), app->viewer_box);
    gtk_paned_set_resize_start_child(GTK_PANED(right_paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(right_paned), TRUE);
    gtk_paned_set_end_child(GTK_PANED(right_paned), app->term_scroll);
    gtk_paned_set_resize_end_child(GTK_PANED(right_paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(right_paned), TRUE);

    /* Min sizes valid for both orientations */
    gtk_widget_set_size_request(app->term_scroll, 120, 80);

    app->right_paned = right_paned;

    /* Divider feedback handlers */
    GtkGesture* click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_paned_pressed), app);
    g_signal_connect(click, "released", G_CALLBACK(on_paned_released), app);
    gtk_widget_add_controller(right_paned, GTK_EVENT_CONTROLLER(click));

    g_signal_connect(right_paned, "notify::position",
                     G_CALLBACK(on_paned_notify_position), app);

    gtk_paned_set_end_child(GTK_PANED(app->main_paned), right_paned);
    gtk_paned_set_resize_end_child(GTK_PANED(app->main_paned), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(app->main_paned), TRUE);

    /* Restore last split proportion */
    g_idle_add(layout_position_idle, app);
}

/* Ctrl+Shift+L: stack terminal pane under viewer or place it beside it */
void layout_toggle(AppData* app) {
    layout_save_frac(app);
    layout_build(app,
                 gtk_orientable_get_orientation(
                     GTK_ORIENTABLE(app->right_paned)) ==
                         GTK_ORIENTATION_HORIZONTAL
                     ? GTK_ORIENTATION_VERTICAL
                     : GTK_ORIENTATION_HORIZONTAL);
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
        "#dim-window, #help-window {\n"
        "  background-color: #191a1b;\n"
        "  border: 1px solid #007acc;\n"
        "  color: #cccccc;\n"
        "  padding: 6px 10px;\n"
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
