#include "ui.h"
#include "terminal.h"
#include "editor.h"

/* Fullscreen state read from GdkWindow (gtk_window_is_fullscreen requires GTK 3.18+) */
gboolean window_is_fullscreen(GtkWindow* win) {
    GdkWindow* gdk_window = gtk_widget_get_window(GTK_WIDGET(win));
    return gdk_window != NULL &&
           (gdk_window_get_state(gdk_window) & GDK_WINDOW_STATE_FULLSCREEN);
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
    {"Right panel (viewer)", "m", "Toggle rendered preview / raw text view (Markdown and HTML)"},
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
gboolean on_help_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data) {
    (void)widget;
    (void)event;
    AppData* app = (AppData*)data;
    gtk_widget_hide(app->help_window);
    gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
    return TRUE;
}

/* Build floating cheat sheet toggled by 'h' */
void create_help_panel(GtkWindow* parent, AppData* app) {
    GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win),
                         "Key bindings - press h or any key to close");
    gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_container_set_border_width(GTK_CONTAINER(win), 14);

    GtkGrid* grid = GTK_GRID(gtk_grid_new());
    gtk_grid_set_row_spacing(grid, 3);
    gtk_grid_set_column_spacing(grid, 16);

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

    gtk_container_add(GTK_CONTAINER(win), GTK_WIDGET(grid));

    g_signal_connect(win, "key-press-event",
                     G_CALLBACK(on_help_key_press), app);
    app->help_window = win;
}

void focus_adjacent_panel(gint step, AppData* app) {
    GPtrArray* panels = g_ptr_array_new();
    gint count;
    gint current = -1;

    g_ptr_array_add(panels, GTK_WIDGET(app->tree_view));
    g_ptr_array_add(panels, app->text_view);
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

    /* Which panel owns the focus? Walk the focused widget's ancestors */
    GtkWidget* w = gtk_window_get_focus(GTK_WINDOW(
        gtk_widget_get_toplevel(GTK_WIDGET(app->tree_view))));
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

gboolean on_window_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data) {
    (void)widget;
    AppData* app = (AppData*)data;

    /* Alt+Left / Alt+Right: panel focus cycling */
    if ((event->state & GDK_MOD1_MASK) != 0 &&
        (event->keyval == GDK_KEY_Left || event->keyval == GDK_KEY_Right)) {
        focus_adjacent_panel(event->keyval == GDK_KEY_Right ? 1 : -1, app);
        return TRUE;
    }

    /* Ctrl+Shift+L: stack the panels or restore side-by-side layout.
     * Shift makes the keyval uppercase, so accept both l and L. */
    if ((event->state & GDK_CONTROL_MASK) &&
        (event->state & GDK_SHIFT_MASK) &&
        (event->keyval == GDK_KEY_l || event->keyval == GDK_KEY_L)) {
        layout_toggle(app);
        return TRUE;
    }

    /* Ctrl+S: save current editor tab; Ctrl+W: close current editor tab */
    if ((event->state & GDK_CONTROL_MASK) &&
        !(event->state & GDK_SHIFT_MASK) &&
        (event->keyval == GDK_KEY_s || event->keyval == GDK_KEY_S)) {
        return editor_save_current(app);
    }
    if ((event->state & GDK_CONTROL_MASK) &&
        (event->keyval == GDK_KEY_w || event->keyval == GDK_KEY_W)) {
        editor_close_current(app);
        return TRUE;
    }

    /* Ctrl+T: terminal pane beside last running one;
     * Ctrl+Shift+T: new terminal row below running ones */
    if ((event->state & GDK_CONTROL_MASK) == 0 ||
        (event->keyval != GDK_KEY_t && event->keyval != GDK_KEY_T)) {
        return FALSE;
    }

#ifdef HAVE_VTE
    /* Each pane gets its own shell in the current directory */
    if (!gtk_widget_get_visible(app->term_scroll)) {
        gtk_widget_show(app->term_scroll);
    }
    term_spawn_pane(app, event->keyval == GDK_KEY_T /* new row below? */);
#else
    /* No VTE installed: the panel is a notice instead of a terminal */
    gtk_widget_set_visible(app->term_scroll,
                           !gtk_widget_get_visible(app->term_scroll));
    gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
#endif
    return TRUE;
}

/* Show or refresh small popup centered on split handle, listing w x h of panes */
void update_pane_dims(GtkWidget* paned_widget, AppData* app) {
    GtkPaned* paned = GTK_PANED(paned_widget);
    GtkWidget* c1 = gtk_paned_get_child1(paned);
    GtkWidget* c2 = gtk_paned_get_child2(paned);
    if (!gtk_widget_get_visible(c2)) {
        return; /* nothing to drag on that side (terminal hidden) */
    }
    GtkAllocation a1, a2;
    gtk_widget_get_allocation(c1, &a1);
    gtk_widget_get_allocation(c2, &a2);
    if (a1.width <= 0 || a2.width <= 0) {
        return; /* not laid out yet */
    }

    gchar* text = g_strdup_printf("%d \xc3\x97 %d  |  %d \xc3\x97 %d",
                                  a1.width, a1.height, a2.width, a2.height);
    gtk_label_set_text(GTK_LABEL(app->dim_label), text);
    g_free(text);

    gtk_widget_show_all(app->dim_window);
    /* Size popup to content, center on handle (screen coordinates) */
    GtkRequisition nat;
    gtk_widget_get_preferred_size(app->dim_window, NULL, &nat);
    gtk_window_resize(GTK_WINDOW(app->dim_window), nat.width, nat.height);
    gint ox = 0;
    gint oy = 0;
    GdkWindow* gdk_window = gtk_widget_get_window(GTK_WIDGET(paned));
    if (gdk_window != NULL) {
        gdk_window_get_origin(gdk_window, &ox, &oy);
    }
    gtk_window_move(GTK_WINDOW(app->dim_window),
                    ox + a1.width - nat.width / 2,
                    oy + a1.height / 2 - nat.height / 2);
}

gboolean pane_dims_idle(gpointer data) {
    AppData* app = (AppData*)data;
    if (app->panning && app->dims_paned != NULL) {
        update_pane_dims(app->dims_paned, app);
    }
    return FALSE; /* one-shot */
}

gboolean on_paned_button_press(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    AppData* app = (AppData*)data;
    if (event->button == 1) { /* handle grabbed: show sizes */
        app->panning = TRUE;
        app->dims_paned = widget;
        update_pane_dims(widget, app);
    }
    return FALSE; /* let GtkPaned start drag */
}

void on_paned_notify_position(GObject* object, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    AppData* app = (AppData*)data;
    if (app->panning) { /* ignore programmatic set_position calls */
        app->dims_paned = GTK_WIDGET(object);
        g_idle_add(pane_dims_idle, app);
    }
}

gboolean on_paned_button_release(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    (void)widget;
    (void)event;
    AppData* app = (AppData*)data;
    app->panning = FALSE;
    gtk_widget_hide(app->dim_window);
    return FALSE;
}

/* Remember the viewer/terminal split as a fraction of pane size */
void layout_save_frac(AppData* app) {
    GtkAllocation a;
    if (app->right_paned == NULL) {
        return;
    }
    GtkOrientation o =
        gtk_orientable_get_orientation(GTK_ORIENTABLE(app->right_paned));
    gtk_widget_get_allocation(app->right_paned, &a);
    gint len = (o == GTK_ORIENTATION_VERTICAL ? a.height : a.width);
    if (len > 0) {
        app->right_frac = (gdouble)gtk_paned_get_position(
            GTK_PANED(app->right_paned)) / len;
    }
}

/* Restore saved split proportion once new paned is laid out */
gboolean layout_position_idle(gpointer data) {
    AppData* app = (AppData*)data;
    GtkAllocation a;
    GtkOrientation o =
        gtk_orientable_get_orientation(GTK_ORIENTABLE(app->right_paned));
    gtk_widget_get_allocation(app->right_paned, &a);
    gint len = (o == GTK_ORIENTATION_VERTICAL ? a.height : a.width);
    if (len > 0) {
        gtk_paned_set_position(GTK_PANED(app->right_paned),
                               (gint)(app->right_frac * len));
        return FALSE; /* done */
    }
    return TRUE; /* try again next idle */
}

/* Assemble viewer and terminal into a pane pair with given orientation */
void layout_build(AppData* app, GtkOrientation orientation) {
    gboolean snatched = (app->right_paned != NULL);
    if (snatched) {
        /* Hold panels so removing paned does not destroy them */
        g_object_ref(app->viewer_box);
        g_object_ref(app->term_scroll);
        gtk_container_remove(GTK_CONTAINER(app->right_paned),
                             app->viewer_box);
        gtk_container_remove(GTK_CONTAINER(app->right_paned),
                             app->term_scroll);
        gtk_container_remove(GTK_CONTAINER(app->main_paned),
                             app->right_paned);
    }

    GtkWidget* right_paned = gtk_paned_new(orientation);
    gtk_paned_pack1(GTK_PANED(right_paned), app->viewer_box, TRUE, TRUE);
    gtk_paned_pack2(GTK_PANED(right_paned), app->term_scroll, FALSE, TRUE);
    if (snatched) {
        g_object_unref(app->viewer_box);
        g_object_unref(app->term_scroll);
    }

    /* Min sizes valid for both orientations */
    gtk_widget_set_size_request(app->term_scroll, 120, 80);

    app->right_paned = right_paned;

    /* Divider feedback handlers */
    g_signal_connect(right_paned, "button-press-event",
                     G_CALLBACK(on_paned_button_press), app);
    g_signal_connect(right_paned, "button-release-event",
                     G_CALLBACK(on_paned_button_release), app);
    g_signal_connect(right_paned, "notify::position",
                     G_CALLBACK(on_paned_notify_position), app);

    gtk_paned_pack2(GTK_PANED(app->main_paned), right_paned, TRUE, TRUE);
    gtk_widget_show(right_paned);

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

/* Panel boundaries: clear rule under status bar, contrasted paned handles */
void apply_panel_borders(void) {
    GtkCssProvider* css = gtk_css_provider_new();
    static const gchar* DATA =
        "#statusbar { padding-bottom: 4px; border-bottom: 1px solid #6e6e6e; }\n"
        "paned > separator {\n"
        "  -gtk-icon-source: none;\n"
        "  min-width: 2px;\n"
        "  min-height: 2px;\n"
        "  background-color: #6e6e6e;\n"
        "}\n";
    gtk_css_provider_load_from_data(css, DATA, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
}
