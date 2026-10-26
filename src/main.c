#include "app_types.h"
#include "file_list.h"
#include "viewer.h"
#include "editor.h"
#include "minimap.h"
#include "terminal.h"
#include "search.h"
#include "ui.h"
#include <unistd.h>

static void on_window_destroy(GtkWidget* widget, gpointer data) {
    (void)widget;
    GMainLoop* loop = (GMainLoop*)data;
    if (loop != NULL && g_main_loop_is_running(loop)) {
        g_main_loop_quit(loop);
    }
}

int main(int argc, char** argv) {
    /* Strip custom options first:
     * -f / --fullscreen: start in fullscreen mode. */
    gboolean start_fullscreen = FALSE;
    int kept = 0;
    for (int i = 0; i < argc; i++) {
        if (i > 0 && (g_strcmp0(argv[i], "-f") == 0 ||
                      g_strcmp0(argv[i], "--fullscreen") == 0)) {
            start_fullscreen = TRUE;
            continue;
        }
        argv[kept++] = argv[i];
    }
    argv[kept] = NULL;
    argc = kept;

    /* Force GTK 4 built-in Default dark theme to avoid GTK 3 system theme CSS errors */
    g_setenv("GTK_THEME", "Default:dark", TRUE);

    gtk_init();
#ifdef HAVE_GSV
    gtk_source_init();
#endif

    /* Adwaita / Default dark theme */
    GtkSettings* settings = gtk_settings_get_default();
    if (settings != NULL) {
        g_object_set(settings,
                     "gtk-theme-name", "Default",
                     "gtk-application-prefer-dark-theme", TRUE,
                     NULL);
    }

    GMainLoop* loop = g_main_loop_new(NULL, FALSE);

    GtkWidget* window = gtk_window_new();
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), loop);
    gtk_window_set_title(GTK_WINDOW(window), "File Explorer");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 600);

    /* Get current directory */
    char cwd_buf[2048];
    const char* cwd = (getcwd(cwd_buf, sizeof(cwd_buf)) != NULL) ? cwd_buf : "/workspace";

    /* Optional CLI argument: directory to display (default: cwd) */
    const char* start_dir = cwd;
    if (argc > 1) {
        if (g_file_test(argv[1], G_FILE_TEST_IS_DIR)) {
            start_dir = argv[1];
        } else {
            g_printerr("[ '%s' is not a directory: showing %s instead ]\n",
                       argv[1], cwd);
        }
    }

    /* Top bar path label & interactive breadcrumbs / status pills */
    char path_text[512];
    snprintf(path_text, sizeof(path_text), "Directory: %s", start_dir);
    GtkWidget* path_label = gtk_label_new(path_text);
    gtk_widget_set_visible(path_label, FALSE);

    GtkWidget* breadcrumb_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_valign(breadcrumb_box, GTK_ALIGN_CENTER);

    GtkWidget* files_btn = gtk_button_new_with_label("📁 Files");
    gtk_button_set_has_frame(GTK_BUTTON(files_btn), FALSE);
    gtk_widget_set_focusable(files_btn, FALSE);
    gtk_widget_add_css_class(files_btn, "status-pill");
    gtk_widget_set_tooltip_text(files_btn, "Show/hide File panel");

    GtkWidget* preview_btn = gtk_button_new_with_label("📄 Preview");
    gtk_button_set_has_frame(GTK_BUTTON(preview_btn), FALSE);
    gtk_widget_set_focusable(preview_btn, FALSE);
    gtk_widget_add_css_class(preview_btn, "status-pill");
    gtk_widget_set_tooltip_text(preview_btn, "Show/hide Preview panel");

    GtkWidget* term_btn = gtk_button_new_with_label(">_ Terminal");
    gtk_button_set_has_frame(GTK_BUTTON(term_btn), FALSE);
    gtk_widget_set_focusable(term_btn, FALSE);
    gtk_widget_add_css_class(term_btn, "status-pill");
    gtk_widget_set_tooltip_text(term_btn, "Show/hide Terminal panel (Ctrl+T)");

    GtkWidget* pill_div_label = gtk_label_new("|");
    gtk_widget_add_css_class(pill_div_label, "breadcrumb-sep");

    GtkWidget* sort_btn = gtk_button_new_with_label("⇅ Sort: Name");
    gtk_button_set_has_frame(GTK_BUTTON(sort_btn), FALSE);
    gtk_widget_set_focusable(sort_btn, FALSE);
    gtk_widget_add_css_class(sort_btn, "status-pill");
    gtk_widget_set_tooltip_text(sort_btn, "Cycle sort order (o)");

    GtkWidget* hidden_btn = gtk_button_new_with_label("👁 Hidden: Off");
    gtk_button_set_has_frame(GTK_BUTTON(hidden_btn), FALSE);
    gtk_widget_set_focusable(hidden_btn, FALSE);
    gtk_widget_add_css_class(hidden_btn, "status-pill");
    gtk_widget_set_tooltip_text(hidden_btn, "Toggle hidden files (i)");

    GtkWidget* help_btn = gtk_button_new_with_label("? Help");
    gtk_button_set_has_frame(GTK_BUTTON(help_btn), FALSE);
    gtk_widget_set_focusable(help_btn, FALSE);
    gtk_widget_add_css_class(help_btn, "status-pill");
    gtk_widget_set_tooltip_text(help_btn, "Show keyboard shortcuts (h)");

    /* Spacer label for top bar */
    GtkWidget* sep_label = gtk_label_new(" ");
    gtk_widget_set_halign(sep_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(sep_label, TRUE);

    /* Left pane: file tree store */
    GtkTreeStore* store =
        gtk_tree_store_new(COL_COUNT,
                           G_TYPE_STRING,   /* COL_NAME */
                           G_TYPE_STRING,   /* COL_TYPE */
                           G_TYPE_STRING,   /* COL_LABEL */
                           G_TYPE_STRING,   /* COL_PATH */
                           G_TYPE_BOOLEAN); /* COL_LOADED */

    GtkWidget* tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_widget_set_name(tree_view, "file-tree");
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree_view), FALSE);
    gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(tree_view), TRUE);
    gtk_tree_view_set_show_expanders(GTK_TREE_VIEW(tree_view), TRUE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(tree_view), FALSE);

    GtkTreeViewColumn* col_name = gtk_tree_view_column_new();
    gtk_tree_view_column_set_expand(col_name, TRUE);
    GtkCellRenderer* renderer_name = gtk_cell_renderer_text_new();
    gtk_cell_renderer_set_padding(renderer_name, 6, 4);
    gtk_tree_view_column_pack_start(col_name, renderer_name, TRUE);
    gtk_tree_view_column_add_attribute(col_name, renderer_name, "text", COL_LABEL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col_name);

    GtkWidget* left_scroll = gtk_scrolled_window_new();
    gtk_widget_set_name(left_scroll, "left-scroll");
    gtk_widget_set_vexpand(left_scroll, TRUE);
    gtk_widget_set_hexpand(left_scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(left_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(left_scroll), tree_view);

    /* Right pane: text viewer (read-only preview) */
#ifdef HAVE_GSV
    GtkWidget* text_view = gtk_source_view_new();
#else
    GtkWidget* text_view = gtk_text_view_new();
#endif
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text_view), PREVIEW_TEXT_PADDING_X);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(text_view), PREVIEW_TEXT_PADDING_X);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(text_view), PREVIEW_TEXT_PADDING_Y);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(text_view), PREVIEW_TEXT_PADDING_Y);

#ifdef HAVE_GSV
    gtk_source_buffer_set_highlight_syntax(
        GTK_SOURCE_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view))), TRUE);
#endif
    GtkWidget* right_scroll = gtk_scrolled_window_new();
    gtk_widget_set_name(right_scroll, "right-scroll");
    gtk_widget_set_vexpand(right_scroll, TRUE);
    gtk_widget_set_hexpand(right_scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(right_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(right_scroll), text_view);

    /* Minimap column */
    GtkWidget* minimap = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(minimap), MINIMAP_WIDTH);
    gtk_widget_set_size_request(minimap, MINIMAP_WIDTH, -1);
    gtk_widget_set_vexpand(minimap, TRUE);
    gtk_widget_set_visible(minimap, FALSE);

    /* Viewer zone: notebook (page 0 is preview, additional pages are editor tabs) */
    GtkWidget* notebook = gtk_notebook_new();
    gtk_widget_set_hexpand(notebook, TRUE);
    gtk_widget_set_vexpand(notebook, TRUE);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), right_scroll,
                             gtk_label_new("Preview"));
    GtkWidget* viewer_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(viewer_box, TRUE);
    gtk_widget_set_vexpand(viewer_box, TRUE);
    gtk_box_append(GTK_BOX(viewer_box), notebook);
    gtk_box_append(GTK_BOX(viewer_box), minimap);

    /* Search bar */
    GtkWidget* search_entry = gtk_search_entry_new();
    gtk_widget_set_visible(search_entry, FALSE);

    /* Terminal panel container */
    GtkWidget* term_scroll = gtk_scrolled_window_new();
    gtk_widget_set_name(term_scroll, "term-scroll");
    gtk_widget_set_hexpand(term_scroll, TRUE);
    gtk_widget_set_vexpand(term_scroll, TRUE);
#ifdef HAVE_VTE
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(term_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_NEVER);
#else
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(term_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
#endif
    gtk_widget_set_visible(term_scroll, FALSE);
#ifdef HAVE_VTE
    GtkWidget* term_stack = gtk_stack_new();
    gtk_widget_set_name(term_stack, "term-box");
    gtk_widget_set_hexpand(term_stack, TRUE);
    gtk_widget_set_vexpand(term_stack, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(term_scroll), term_stack);
#else
    GtkWidget* term_stub = gtk_label_new(
        "Terminal panel disabled:\ninstall libvte-2.91-gtk4-dev then rebuild (make).");
    gtk_widget_set_halign(term_stub, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(term_stub, GTK_ALIGN_CENTER);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(term_scroll), term_stub);
#endif

    /* Allocate and wire AppData */
    AppData* app = g_new0(AppData, 1);
    app->window = GTK_WINDOW(window);
    app->main_loop = loop;
    app->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
#ifdef HAVE_GSV
    apply_vscode_scheme(app->buffer, text_view);
#endif
    app->base_dir = g_strdup(start_dir);
    app->term_scroll = term_scroll;
#ifdef HAVE_VTE
    app->term_stack = term_stack;
#endif
    workspaces_init(app);
    GtkWidget* ws_minimap = create_workspace_minimap(app);
    GtkWidget* ws_div_label = gtk_label_new("|");
    gtk_widget_add_css_class(ws_div_label, "breadcrumb-sep");
    app->model = GTK_TREE_MODEL(store);
    app->tree_view = GTK_TREE_VIEW(tree_view);
    app->expanded_paths = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    populate_store(store, app, start_dir, FALSE, SORT_NAME);
    g_object_unref(store);
    app->text_view = text_view;
    app->search_entry = search_entry;
    app->path_label = path_label;
    app->breadcrumb_box = breadcrumb_box;
    app->files_btn = files_btn;
    app->preview_btn = preview_btn;
    app->term_btn = term_btn;
    app->sort_btn = sort_btn;
    app->hidden_btn = hidden_btn;
    app->help_btn = help_btn;
    g_signal_connect(files_btn, "clicked", G_CALLBACK(on_files_pill_clicked), app);
    g_signal_connect(preview_btn, "clicked", G_CALLBACK(on_preview_pill_clicked), app);
    g_signal_connect(term_btn, "clicked", G_CALLBACK(on_term_pill_clicked), app);
    g_signal_connect(sort_btn, "clicked", G_CALLBACK(on_sort_pill_clicked), app);
    g_signal_connect(hidden_btn, "clicked", G_CALLBACK(on_hidden_pill_clicked), app);
    g_signal_connect(help_btn, "clicked", G_CALLBACK(on_help_pill_clicked), app);
    update_path_label(app);

    app->notebook = notebook;
    g_signal_connect(notebook, "switch-page",
                     G_CALLBACK(on_editor_switch_page_changed), app);

    app->minimap = minimap;
    app->viewer_vadj =
        gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(right_scroll));
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(minimap), on_minimap_draw, app, NULL);

    GtkGesture* mm_click = gtk_gesture_click_new();
    g_signal_connect(mm_click, "pressed", G_CALLBACK(on_minimap_pressed), app);
    g_signal_connect(mm_click, "released", G_CALLBACK(on_minimap_released), app);
    gtk_widget_add_controller(minimap, GTK_EVENT_CONTROLLER(mm_click));

    GtkEventController* mm_motion = gtk_event_controller_motion_new();
    g_signal_connect(mm_motion, "motion", G_CALLBACK(on_minimap_motion), app);
    gtk_widget_add_controller(minimap, mm_motion);

    g_signal_connect(minimap, "resize",
                     G_CALLBACK(on_minimap_resize), app);
    g_signal_connect(minimap, "realize",
                     G_CALLBACK(on_minimap_realize), app);
    g_signal_connect(app->buffer, "changed",
                     G_CALLBACK(on_minimap_buffer_changed), app);
    g_signal_connect(app->viewer_vadj, "value-changed",
                     G_CALLBACK(on_minimap_scroll), app);
    g_signal_connect(app->viewer_vadj, "changed",
                     G_CALLBACK(on_minimap_adj_changed), app);

    create_help_panel(GTK_WINDOW(window), app);

    GtkEventController* win_key_ctrl = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(win_key_ctrl, GTK_PHASE_CAPTURE);
    g_signal_connect(win_key_ctrl, "key-pressed",
                     G_CALLBACK(on_window_key_press), app);
    gtk_widget_add_controller(window, win_key_ctrl);

    monitor_directory(app);

    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    app->selection = selection;
    g_signal_connect(selection, "changed", G_CALLBACK(on_selection_changed), app);

    g_signal_connect(tree_view, "test-expand-row", G_CALLBACK(on_tree_test_expand_row), app);
    g_signal_connect(tree_view, "row-expanded", G_CALLBACK(on_tree_row_expanded), app);
    g_signal_connect(tree_view, "row-collapsed", G_CALLBACK(on_tree_row_collapsed), app);
    g_signal_connect(tree_view, "row-activated", G_CALLBACK(on_row_activated), app);

    GtkEventController* tree_key_ctrl = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(tree_key_ctrl, GTK_PHASE_CAPTURE);
    g_signal_connect(tree_key_ctrl, "key-pressed", G_CALLBACK(on_tree_key_press), app);
    gtk_widget_add_controller(tree_view, tree_key_ctrl);

    GtkEventController* viewer_key_ctrl = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(viewer_key_ctrl, GTK_PHASE_CAPTURE);
    g_signal_connect(viewer_key_ctrl, "key-pressed", G_CALLBACK(on_viewer_key_press), app);
    gtk_widget_add_controller(text_view, viewer_key_ctrl);

    g_signal_connect(search_entry, "search-changed", G_CALLBACK(on_search_changed), app);
    g_signal_connect(search_entry, "activate", G_CALLBACK(on_search_activate), app);

    GtkEventController* search_key_ctrl = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(search_key_ctrl, GTK_PHASE_CAPTURE);
    g_signal_connect(search_key_ctrl, "key-pressed", G_CALLBACK(on_search_key_press), app);
    gtk_widget_add_controller(search_entry, search_key_ctrl);

    GtkWidget* left_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(left_vbox, "left-sidebar");
    gtk_widget_set_hexpand(left_vbox, TRUE);
    gtk_widget_set_vexpand(left_vbox, TRUE);
    gtk_box_append(GTK_BOX(left_vbox), search_entry);
    gtk_box_append(GTK_BOX(left_vbox), left_scroll);

    GtkWidget* main_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_hexpand(main_paned, TRUE);
    gtk_widget_set_vexpand(main_paned, TRUE);
    gtk_paned_set_start_child(GTK_PANED(main_paned), left_vbox);
    gtk_paned_set_resize_start_child(GTK_PANED(main_paned), FALSE);
    gtk_paned_set_shrink_start_child(GTK_PANED(main_paned), TRUE);
    gtk_widget_set_size_request(left_vbox, 150, -1);

    GtkGesture* paned_click = gtk_gesture_click_new();
    g_signal_connect(paned_click, "pressed", G_CALLBACK(on_paned_pressed), app);
    g_signal_connect(paned_click, "released", G_CALLBACK(on_paned_released), app);
    gtk_widget_add_controller(main_paned, GTK_EVENT_CONTROLLER(paned_click));

    g_signal_connect(main_paned, "notify::position",
                     G_CALLBACK(on_paned_notify_position), app);

    app->left_vbox = left_vbox;
    app->main_paned = main_paned;
    app->viewer_box = viewer_box;
    app->right_frac = 0.72;
    layout_build(app, GTK_ORIENTATION_HORIZONTAL);
    update_path_label(app);

    /* Overlay badge for pane resize dimensions feedback */
    GtkWidget* overlay = gtk_overlay_new();
    gtk_widget_set_hexpand(overlay, TRUE);
    gtk_widget_set_vexpand(overlay, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), main_paned);

    GtkWidget* dim_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(dim_box, "dim-window");
    gtk_widget_set_halign(dim_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(dim_box, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(dim_box, FALSE);
    GtkWidget* dim_label = gtk_label_new("");
    gtk_box_append(GTK_BOX(dim_box), dim_label);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), dim_box);
    app->dim_window = dim_box;
    app->dim_label = dim_label;

    GtkWidget* workspace_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(workspace_hbox, TRUE);
    gtk_widget_set_vexpand(workspace_hbox, TRUE);
    gtk_box_append(GTK_BOX(workspace_hbox), overlay);
    gtk_box_append(GTK_BOX(workspace_hbox), app->help_window);

    GtkWidget* outer_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* label_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_name(label_row, "statusbar");
    gtk_box_append(GTK_BOX(label_row), path_label);
    gtk_box_append(GTK_BOX(label_row), breadcrumb_box);
    gtk_box_append(GTK_BOX(label_row), sep_label);
    gtk_box_append(GTK_BOX(label_row), ws_minimap);
    gtk_box_append(GTK_BOX(label_row), ws_div_label);
    gtk_box_append(GTK_BOX(label_row), files_btn);
    gtk_box_append(GTK_BOX(label_row), preview_btn);
    gtk_box_append(GTK_BOX(label_row), term_btn);
    gtk_box_append(GTK_BOX(label_row), pill_div_label);
    gtk_box_append(GTK_BOX(label_row), sort_btn);
    gtk_box_append(GTK_BOX(label_row), hidden_btn);
    gtk_box_append(GTK_BOX(label_row), help_btn);
    gtk_box_append(GTK_BOX(outer_vbox), label_row);
    gtk_box_append(GTK_BOX(outer_vbox), workspace_hbox);

    gtk_window_set_child(GTK_WINDOW(window), outer_vbox);
    apply_panel_borders();
    gtk_window_present(GTK_WINDOW(window));

    if (start_fullscreen) {
        gtk_window_fullscreen(GTK_WINDOW(window));
    }

    select_first_row(app);
    gtk_widget_grab_focus(tree_view);

    g_main_loop_run(loop);

    if (app->dir_cancellable != NULL) {
        g_cancellable_cancel(app->dir_cancellable);
        g_clear_object(&app->dir_cancellable);
    }
    if (app->preview_cancellable != NULL) {
        g_cancellable_cancel(app->preview_cancellable);
        g_clear_object(&app->preview_cancellable);
    }

    g_main_loop_unref(loop);

    return 0;
}
