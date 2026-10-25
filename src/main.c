#include "app_types.h"
#include "file_list.h"
#include "viewer.h"
#include "editor.h"
#include "minimap.h"
#include "terminal.h"
#include "search.h"
#include "ui.h"
#include <unistd.h>

int main(int argc, char** argv) {
    /* Strip custom options first: gtk_init aborts on unknown options.
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
    gtk_init(&argc, &argv);

    /* Adwaita dark theme */
    GtkSettings* settings = gtk_settings_get_default();
    g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_window_set_title(GTK_WINDOW(window), "File Explorer");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 600);

    /* Ctrl+Q accelerator to quit */
    GtkAccelGroup* accel = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(window), accel);
    gtk_accel_group_connect(accel, GDK_KEY_q, GDK_CONTROL_MASK, 0,
                            g_cclosure_new(G_CALLBACK(gtk_main_quit), NULL, NULL));

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

    /* Top bar path label */
    char path_text[512];
    snprintf(path_text, sizeof(path_text), "Directory: %s", start_dir);
    GtkWidget* path_label = gtk_label_new(path_text);
    gtk_widget_set_halign(path_label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(path_label, 5);

    /* Spacer label for top bar */
    GtkWidget* sep_label = gtk_label_new(" ");
    gtk_widget_set_halign(sep_label, GTK_ALIGN_START);

    /* Left pane: file list store
     * col0 = raw name, col1 = type, col2 = display label (emoji + name) */
    GtkListStore* store =
        gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

    populate_store(store, start_dir, FALSE, SORT_NAME);

    GtkWidget* tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree_view), FALSE);

    GtkTreeViewColumn* col_name = gtk_tree_view_column_new();
    gtk_tree_view_column_set_expand(col_name, TRUE);
    GtkCellRenderer* renderer_name = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col_name, renderer_name, TRUE);
    gtk_tree_view_column_add_attribute(col_name, renderer_name, "text", 2);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col_name);

    g_object_unref(store);

    GtkWidget* left_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(left_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(left_scroll), tree_view);

    /* Right pane: text viewer (read-only preview) */
#ifdef HAVE_GSV
    GtkWidget* text_view = gtk_source_view_new();
#else
    GtkWidget* text_view = gtk_text_view_new();
#endif
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);

#ifdef HAVE_GSV
    gtk_source_buffer_set_highlight_syntax(
        GTK_SOURCE_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view))), TRUE);
#endif
    GtkWidget* right_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(right_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(right_scroll), text_view);

    /* Minimap column */
    GtkWidget* minimap = gtk_drawing_area_new();
    gtk_widget_set_size_request(minimap, MINIMAP_WIDTH, -1);
    gtk_widget_add_events(minimap, GDK_BUTTON_PRESS_MASK |
                                   GDK_BUTTON_RELEASE_MASK |
                                   GDK_BUTTON1_MOTION_MASK);

    /* Viewer zone: notebook (page 0 is preview, additional pages are editor tabs) */
    GtkWidget* notebook = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), right_scroll,
                             gtk_label_new("Preview"));
    GtkWidget* viewer_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(viewer_box), notebook, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(viewer_box), minimap, FALSE, FALSE, 0);

    /* Search bar */
    GtkWidget* search_entry = gtk_search_entry_new();
    gtk_widget_set_no_show_all(search_entry, TRUE);

    /* Terminal panel container */
    GtkWidget* term_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(term_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_no_show_all(term_scroll, TRUE);
#ifdef HAVE_VTE
    GtkWidget* term_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_add(GTK_CONTAINER(term_scroll), term_box);
    gtk_widget_show(term_box);
#else
    GtkWidget* term_stub = gtk_label_new(
        "Terminal panel disabled:\ninstall libvte-2.91-dev then rebuild (make).");
    gtk_widget_set_halign(term_stub, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(term_stub, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(term_scroll), term_stub);
#endif

    /* Allocate and wire AppData */
    AppData* app = g_new0(AppData, 1);
    app->window = GTK_WINDOW(window);
    app->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
#ifdef HAVE_GSV
    apply_vscode_scheme(app->buffer, text_view);
#endif
    app->base_dir = g_strdup(start_dir);
    app->model = GTK_TREE_MODEL(store);
    app->tree_view = GTK_TREE_VIEW(tree_view);
    app->text_view = text_view;
    app->search_entry = search_entry;
    app->path_label = path_label;
    update_path_label(app);

    app->term_scroll = term_scroll;
#ifdef HAVE_VTE
    app->term_box = term_box;
#endif

    app->notebook = notebook;
    g_signal_connect(notebook, "switch-page",
                     G_CALLBACK(on_editor_switch_page_changed), app);

    app->minimap = minimap;
    app->viewer_vadj =
        gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(right_scroll));
    g_signal_connect(minimap, "draw", G_CALLBACK(on_minimap_draw), app);
    g_signal_connect(minimap, "button-press-event",
                     G_CALLBACK(on_minimap_button_press), app);
    g_signal_connect(minimap, "motion-notify-event",
                     G_CALLBACK(on_minimap_motion), app);
    g_signal_connect(minimap, "button-release-event",
                     G_CALLBACK(on_minimap_button_release), app);
    g_signal_connect(minimap, "size-allocate",
                     G_CALLBACK(on_minimap_size_allocate), app);
    g_signal_connect(minimap, "realize", G_CALLBACK(on_minimap_realize), app);
    g_signal_connect(app->buffer, "changed",
                     G_CALLBACK(on_minimap_buffer_changed), app);
    g_signal_connect(app->viewer_vadj, "value-changed",
                     G_CALLBACK(on_minimap_scroll), app);
    g_signal_connect(app->viewer_vadj, "changed",
                     G_CALLBACK(on_minimap_adj_changed), app);

    create_help_panel(GTK_WINDOW(window), app);

    g_signal_connect(window, "key-press-event",
                     G_CALLBACK(on_window_key_press), app);

    monitor_directory(app);

    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    app->selection = selection;
    g_signal_connect(selection, "changed", G_CALLBACK(on_selection_changed), app);

    g_signal_connect(tree_view, "key-press-event", G_CALLBACK(on_tree_key_press), app);
    g_signal_connect(tree_view, "row-activated", G_CALLBACK(on_row_activated), app);
    g_signal_connect(text_view, "key-press-event", G_CALLBACK(on_viewer_key_press), app);

    g_signal_connect(search_entry, "search-changed", G_CALLBACK(on_search_changed), app);
    g_signal_connect(search_entry, "activate", G_CALLBACK(on_search_activate), app);
    g_signal_connect(search_entry, "key-press-event", G_CALLBACK(on_search_key_press), app);

    GtkWidget* left_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(left_vbox), search_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left_vbox), left_scroll, TRUE, TRUE, 0);

    GtkWidget* main_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(main_paned), left_vbox, FALSE, FALSE);
    gtk_widget_set_size_request(left_vbox, 150, -1);
    g_signal_connect(main_paned, "button-press-event",
                     G_CALLBACK(on_paned_button_press), app);
    g_signal_connect(main_paned, "button-release-event",
                     G_CALLBACK(on_paned_button_release), app);
    g_signal_connect(main_paned, "notify::position",
                     G_CALLBACK(on_paned_notify_position), app);

    app->main_paned = main_paned;
    app->viewer_box = viewer_box;
    app->right_frac = 0.72;
    layout_build(app, GTK_ORIENTATION_HORIZONTAL);

    GtkWidget* dim_window = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_type_hint(GTK_WINDOW(dim_window), GDK_WINDOW_TYPE_HINT_TOOLTIP);
    gtk_window_set_decorated(GTK_WINDOW(dim_window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(dim_window), 1, 1);
    gtk_container_set_border_width(GTK_CONTAINER(dim_window), 8);
    GtkWidget* dim_label = gtk_label_new("");
    gtk_container_add(GTK_CONTAINER(dim_window), dim_label);
    app->dim_window = dim_window;
    app->dim_label = dim_label;

    GtkWidget* outer_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget* label_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(label_row, "statusbar");
    gtk_box_pack_start(GTK_BOX(label_row), path_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(label_row), sep_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(outer_vbox), label_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer_vbox), main_paned, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(window), outer_vbox);
    apply_panel_borders();
    gtk_widget_show_all(window);

    if (start_fullscreen) {
        gtk_window_fullscreen(GTK_WINDOW(window));
    }

    gtk_main();

    return 0;
}
