#include "topbar.h"
#include "dialog.h"
#include "layouts.h"
#include "icons.h"
#include "fs_watch.h"
#include "panels.h"
#include "app_events.h"
#include "text_style.h"
#include "tree_nav.h"
#include "theme.h"
#include "status_bar.h"
#include "sidebar.h"
#include "workspace.h"
#include "help.h"
#include "file_list.h"
#include "viewer.h"
#include "video.h"
#include "audio.h"
#include "minimap.h"
#include "search.h"
#include "ui.h"
#include "editor.h"
#include "git_tab.h"
#include "bookmarks.h"
#include "notes.h"
#include "remote.h"
#include "orgmode.h"
#include "diff_gutter.h"
#include <unistd.h>

static void on_window_destroy(GtkWidget* widget, gpointer data) {
    (void)widget;
    GMainLoop* loop = (GMainLoop*)data;
    if (loop != NULL && g_main_loop_is_running(loop)) {
        g_main_loop_quit(loop);
    }
}

static void on_window_active_changed(GObject* object, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    AppData* app = (AppData*)user_data;
    if (app != NULL && gtk_window_is_active(GTK_WINDOW(object))) {
        app->git_dirty = TRUE;
        schedule_refresh(app);
    }
}

static void on_tree_click_pressed(GtkGestureClick* gesture, gint n_press,
                                  gdouble x, gdouble y, gpointer user_data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    AppData* app = (AppData*)user_data;
    if (app != NULL && app->tree_view != NULL) {
        gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
    }
}

static void on_viewer_click_pressed(GtkGestureClick* gesture, gint n_press,
                                    gdouble x, gdouble y, gpointer user_data) {
    (void)gesture;
    (void)n_press;
    AppData* app = (AppData*)user_data;
    if (app != NULL) {
        orgmode_handle_click(app, x, y);
    }
}

/* HEAD or files changed: re-read the committed versions behind the change bars */
static void refresh_diff_gutters(AppData* app) {
    (void)app;
    diff_gutter_refresh_all();
}

int main(int argc, char** argv) {
    /* Strip custom options first:
     * -f / --fullscreen: start in fullscreen mode.
     * -s / --size WIDTHxHEIGHT: start with specific window size (e.g. 1920x1080). */
    gboolean start_fullscreen = FALSE;
    int win_width = 1000;
    int win_height = 600;
    int kept = 0;
    for (int i = 0; i < argc; i++) {
        if (i > 0) {
            if (g_strcmp0(argv[i], "-f") == 0 ||
                g_strcmp0(argv[i], "--fullscreen") == 0) {
                start_fullscreen = TRUE;
                continue;
            }
            if (g_strcmp0(argv[i], "-s") == 0 ||
                g_strcmp0(argv[i], "--size") == 0) {
                if (i + 1 < argc) {
                    i++;
                    int w = 0, h = 0;
                    char sep = 0, tail = 0;
                    if (sscanf(argv[i], "%d %c %d %c", &w, &sep, &h, &tail) == 3 &&
                        (sep == 'x' || sep == 'X') && w > 0 && h > 0) {
                        win_width = w;
                        win_height = h;
                    } else {
                        g_printerr("[ Invalid size '%s': expected format WIDTHxHEIGHT (e.g. --size 1920x1080) ]\n",
                                   argv[i]);
                    }
                } else {
                    g_printerr("[ Option '%s' requires an argument (e.g. --size 1920x1080) ]\n",
                               argv[i]);
                }
                continue;
            }
            if (g_str_has_prefix(argv[i], "--size=") ||
                g_str_has_prefix(argv[i], "-s=")) {
                const char* val = strchr(argv[i], '=') + 1;
                int w = 0, h = 0;
                char sep = 0, tail = 0;
                if (sscanf(val, "%d %c %d %c", &w, &sep, &h, &tail) == 3 &&
                    (sep == 'x' || sep == 'X') && w > 0 && h > 0) {
                    win_width = w;
                    win_height = h;
                } else {
                    g_printerr("[ Invalid size '%s': expected format WIDTHxHEIGHT (e.g. --size 1920x1080) ]\n",
                               val);
                }
                continue;
            }
        }
        argv[kept++] = argv[i];
    }
    argv[kept] = NULL;
    argc = kept;

    /* Force GTK 4 built-in Default dark theme to avoid GTK 3 system theme CSS errors */
    g_setenv("GTK_THEME", "Default:dark", TRUE);

    /* Prefer classic playbin in GstPlay to avoid decodebin3 collection assertions */
    g_setenv("GST_PLAY_USE_PLAYBIN3", "0", FALSE);

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
    gtk_window_set_default_size(GTK_WINDOW(window), win_width, win_height);

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
    gchar* path_text = g_strdup_printf("Directory: %s", start_dir);
    GtkWidget* path_label = gtk_label_new(path_text);
    g_free(path_text);
    gtk_widget_set_visible(path_label, FALSE);

    GtkWidget* breadcrumb_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_valign(breadcrumb_box, GTK_ALIGN_CENTER);

    GtkWidget* files_btn = gtk_button_new();
    icon_button_set_content(files_btn, "folder", "Explorer");
    gtk_button_set_has_frame(GTK_BUTTON(files_btn), FALSE);
    gtk_widget_set_focusable(files_btn, FALSE);
    gtk_widget_add_css_class(files_btn, "status-pill");
    gtk_widget_set_tooltip_text(files_btn, "Show / Hide Explorer Panel");

    GtkWidget* preview_btn = gtk_button_new();
    icon_button_set_content(preview_btn, "document-print-preview", "Preview");
    gtk_button_set_has_frame(GTK_BUTTON(preview_btn), FALSE);
    gtk_widget_set_focusable(preview_btn, FALSE);
    gtk_widget_add_css_class(preview_btn, "status-pill");
    gtk_widget_set_tooltip_text(preview_btn, "Show / Hide Preview Panel");

    GtkWidget* term_btn = gtk_button_new();
    icon_button_set_content(term_btn, "utilities-terminal", "Terminal");
    gtk_button_set_has_frame(GTK_BUTTON(term_btn), FALSE);
    gtk_widget_set_focusable(term_btn, FALSE);
    gtk_widget_add_css_class(term_btn, "status-pill");
    gtk_widget_set_tooltip_text(term_btn, "Show / Hide Terminal Panel (Ctrl+T)");

    GtkWidget* pill_div_label = gtk_label_new("|");
    gtk_widget_add_css_class(pill_div_label, "breadcrumb-sep");

    GtkWidget* help_btn = gtk_button_new();
    icon_button_set_content(help_btn, "dialog-question", "Help");
    gtk_button_set_has_frame(GTK_BUTTON(help_btn), FALSE);
    gtk_widget_set_focusable(help_btn, FALSE);
    gtk_widget_add_css_class(help_btn, "status-pill");
    gtk_widget_set_tooltip_text(help_btn, "Show Keyboard Shortcuts (?)");

    /* Spacer label for top bar */
    GtkWidget* sep_label = gtk_label_new(" ");
    gtk_widget_set_halign(sep_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(sep_label, TRUE);

    /* Left pane: explorer tree (files, bookmarks, notes, remote views) */
    ExplorerTree* explorer = explorer_tree_new();
    GtkWidget* tree_view = explorer_tree_get_widget(explorer);
    gtk_widget_set_name(tree_view, "file-tree");

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
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(text_view), PREVIEW_TEXT_PADDING_X + TEXT_SCROLLBAR_CLEARANCE);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(text_view), PREVIEW_TEXT_PADDING_Y);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(text_view), PREVIEW_TEXT_PADDING_Y + TEXT_SCROLLBAR_CLEARANCE);

    diff_gutter_attach(text_view, FALSE);

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

    /* Viewer zone: notebook (page 0 is preview stack, additional pages are editor tabs) */
    GtkWidget* preview_stack = gtk_stack_new();
    gtk_widget_set_name(preview_stack, "preview-stack");
    gtk_widget_set_hexpand(preview_stack, TRUE);
    gtk_widget_set_vexpand(preview_stack, TRUE);
    gtk_stack_add_named(GTK_STACK(preview_stack), right_scroll, "text");

    GtkWidget* notebook = gtk_notebook_new();
    gtk_widget_set_hexpand(notebook, TRUE);
    gtk_widget_set_vexpand(notebook, TRUE);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
    gtk_notebook_set_group_name(GTK_NOTEBOOK(notebook), "editor-tabs");
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), preview_stack,
                             icon_label_new("document-print-preview", "Preview"));
    /* Left-aligned action widget inside preview notebook tab header: Split/Dock button */
    GtkWidget* split_btn = icon_button_new("view-dual", NULL);
    gtk_widget_set_name(split_btn, "tab-split-btn");
    gtk_button_set_has_frame(GTK_BUTTON(split_btn), FALSE);
    gtk_widget_set_focusable(split_btn, FALSE);
    gtk_widget_add_css_class(split_btn, "tab-action-btn");
    gtk_widget_set_tooltip_text(split_btn, "Move Editor Tabs to a Dedicated Panel (Ctrl+E)");
    gtk_notebook_set_action_widget(GTK_NOTEBOOK(notebook), split_btn, GTK_PACK_START);

    GtkWidget* viewer_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(viewer_box, "viewer-box");
    gtk_widget_set_hexpand(viewer_box, TRUE);
    gtk_widget_set_vexpand(viewer_box, TRUE);
    gtk_box_append(GTK_BOX(viewer_box), notebook);
    gtk_box_append(GTK_BOX(viewer_box), minimap);

    /* Dedicated editor panel (when tabs moved right of preview) */
    GtkWidget* editor_notebook = gtk_notebook_new();
    gtk_widget_set_name(editor_notebook, "editor-notebook");
    gtk_widget_set_hexpand(editor_notebook, TRUE);
    gtk_widget_set_vexpand(editor_notebook, TRUE);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(editor_notebook), TRUE);
    gtk_notebook_set_group_name(GTK_NOTEBOOK(editor_notebook), "editor-tabs");

    GtkWidget* editor_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(editor_box, "editor-panel");
    gtk_widget_set_hexpand(editor_box, TRUE);
    gtk_widget_set_vexpand(editor_box, TRUE);
    gtk_widget_set_size_request(editor_box, 150, -1);
    gtk_box_append(GTK_BOX(editor_box), editor_notebook);
    gtk_widget_set_visible(editor_box, FALSE);

    GtkWidget* editor_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_name(editor_paned, "editor-paned");
    gtk_widget_set_hexpand(editor_paned, TRUE);
    gtk_widget_set_vexpand(editor_paned, TRUE);
    gtk_paned_set_start_child(GTK_PANED(editor_paned), viewer_box);
    gtk_paned_set_resize_start_child(GTK_PANED(editor_paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(editor_paned), TRUE);
    gtk_paned_set_end_child(GTK_PANED(editor_paned), editor_box);
    gtk_paned_set_resize_end_child(GTK_PANED(editor_paned), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(editor_paned), TRUE);
    gtk_widget_set_size_request(viewer_box, 150, -1);

    /* Search bar */
    GtkWidget* search_entry = gtk_search_entry_new();
    gtk_widget_set_visible(search_entry, FALSE);
    gtk_editable_set_width_chars(GTK_EDITABLE(search_entry), 5);

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

    /* Event subscriptions: before any code can emit (see app_events.h) */
    app_events_subscribe(app, APP_EVENT_CHROME_CHANGED, update_path_label);
    app_events_subscribe(app, APP_EVENT_STATUS_CHANGED, update_status_bar);
    app_events_subscribe(app, APP_EVENT_LAYOUT_CHANGED, workspace_save_current);
    app_events_subscribe(app, APP_EVENT_LAYOUT_REBUILT, workspace_apply_positions);
    app_events_subscribe(app, APP_EVENT_FILES_CHANGED, refresh_listing);
    app_events_subscribe(app, APP_EVENT_PREVIEW_STALE, viewer_reload_preview);
    app_events_subscribe(app, APP_EVENT_GIT_STATUS_CHANGED, refresh_git_status_in_place);
    app_events_subscribe(app, APP_EVENT_GIT_STATUS_CHANGED, refresh_diff_gutters);
    app_events_subscribe(app, APP_EVENT_FILES_CHANGED, refresh_diff_gutters);
    app->window = GTK_WINDOW(window);
    app->main_loop = loop;
    app->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
#ifdef HAVE_GSV
    apply_vscode_scheme(app->buffer, text_view);
#endif
    app->base_dir = g_strdup(start_dir);
    app->start_dir = g_canonicalize_filename(start_dir, NULL);
    app->term_scroll = term_scroll;
#ifdef HAVE_VTE
    app->term_stack = term_stack;
#endif
    workspaces_init(app);
    GtkWidget* ws_minimap = create_workspace_minimap(app);
    GtkWidget* layouts_btn = icon_button_new("view-grid", "Workspace Layouts (Alt+L)");
    gtk_widget_add_css_class(layouts_btn, "status-pill");
    g_signal_connect_swapped(layouts_btn, "clicked", G_CALLBACK(layouts_show_dialog), app);
    GtkWidget* ws_div_label = gtk_label_new("|");
    gtk_widget_add_css_class(ws_div_label, "breadcrumb-sep");
    app->tree = explorer;
    app->tree_view = tree_view;
    app->expanded_paths = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    app->dir_monitors = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
    g_signal_connect(window, "notify::is-active", G_CALLBACK(on_window_active_changed), app);
    bookmarks_init(app);
    bookmarks_attach_context_menu(app);
    notes_init(app);
    notes_attach_context_menu(app);
    remote_init(app);
    remote_attach_context_menu(app);
    orgmode_install_gtksourceview_spec();
    populate_tree(app, start_dir, FALSE, SORT_NAME);
    app->text_view = text_view;
    app->search_entry = search_entry;
    app->path_label = path_label;
    app->breadcrumb_box = breadcrumb_box;
    app->files_btn = files_btn;
    app->preview_btn = preview_btn;
    app->split_btn = split_btn;
    app->term_btn = term_btn;
    app->sort_btn = NULL;
    app->hidden_btn = NULL;
    app->help_btn = help_btn;
    g_signal_connect(files_btn, "clicked", G_CALLBACK(on_files_pill_clicked), app);
    g_signal_connect(preview_btn, "clicked", G_CALLBACK(on_preview_pill_clicked), app);
    g_signal_connect(split_btn, "clicked", G_CALLBACK(on_split_btn_clicked), app);
    g_signal_connect(term_btn, "clicked", G_CALLBACK(on_term_pill_clicked), app);
    g_signal_connect(help_btn, "clicked", G_CALLBACK(on_help_pill_clicked), app);
    app_emit(app, APP_EVENT_CHROME_CHANGED);

    app->notebook = notebook;
    app->preview_stack = preview_stack;
    video_preview_init(app);
    audio_preview_init(app);
    git_tab_init(app);
    g_signal_connect(notebook, "switch-page",
                     G_CALLBACK(on_editor_switch_page_changed), app);
    g_signal_connect(editor_notebook, "switch-page",
                     G_CALLBACK(on_editor_switch_page_changed), app);
    g_signal_connect(notebook, "page-reordered",
                     G_CALLBACK(on_notebook_page_reordered), app);
    g_signal_connect(editor_notebook, "page-reordered",
                     G_CALLBACK(on_notebook_page_reordered), app);
    editor_notebook_enable_tab_drop(app, notebook);
    editor_notebook_enable_tab_drop(app, editor_notebook);

    app->minimap = minimap;
    app->viewer_vadj =
        gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(right_scroll));
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(minimap), on_minimap_draw, app, NULL);
    diff_gutter_add_companion(text_view, minimap);

    GtkGesture* mm_click = gtk_gesture_click_new();
    g_signal_connect(mm_click, "pressed", G_CALLBACK(on_minimap_pressed), app);
    g_signal_connect(mm_click, "released", G_CALLBACK(on_minimap_released), app);
    gtk_widget_add_controller(minimap, GTK_EVENT_CONTROLLER(mm_click));

    GtkEventController* mm_motion = gtk_event_controller_motion_new();
    g_signal_connect(mm_motion, "motion", G_CALLBACK(on_minimap_motion), app);
    gtk_widget_add_controller(minimap, mm_motion);
    minimap_attach_wheel(minimap, text_view);

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

    app->selection_changed_id =
        g_signal_connect(app->tree, "selection-changed", G_CALLBACK(on_selection_changed), app);
    g_signal_connect(app->tree, "row-toggled", G_CALLBACK(on_tree_row_toggled), app);
    g_signal_connect(app->tree, "row-activated", G_CALLBACK(on_row_activated), app);

    GtkEventController* tree_key_ctrl = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(tree_key_ctrl, GTK_PHASE_CAPTURE);
    g_signal_connect(tree_key_ctrl, "key-pressed", G_CALLBACK(on_tree_key_press), app);
    gtk_widget_add_controller(tree_view, tree_key_ctrl);

    GtkGesture* tree_click_ctrl = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(tree_click_ctrl), GDK_BUTTON_PRIMARY);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(tree_click_ctrl), GTK_PHASE_CAPTURE);
    g_signal_connect(tree_click_ctrl, "pressed", G_CALLBACK(on_tree_click_pressed), app);
    gtk_widget_add_controller(tree_view, GTK_EVENT_CONTROLLER(tree_click_ctrl));

    GtkEventController* viewer_key_ctrl = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(viewer_key_ctrl, GTK_PHASE_CAPTURE);
    g_signal_connect(viewer_key_ctrl, "key-pressed", G_CALLBACK(on_viewer_key_press), app);
    gtk_widget_add_controller(text_view, viewer_key_ctrl);

    GtkGesture* viewer_click_ctrl = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(viewer_click_ctrl), GDK_BUTTON_PRIMARY);
    g_signal_connect(viewer_click_ctrl, "pressed", G_CALLBACK(on_viewer_click_pressed), app);
    gtk_widget_add_controller(text_view, GTK_EVENT_CONTROLLER(viewer_click_ctrl));

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
    gtk_box_append(GTK_BOX(left_vbox), create_sidebar_header(app));
    gtk_box_append(GTK_BOX(left_vbox), search_entry);
    gtk_box_append(GTK_BOX(left_vbox), left_scroll);

    GtkWidget* main_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_hexpand(main_paned, TRUE);
    gtk_widget_set_vexpand(main_paned, TRUE);
    gtk_paned_set_start_child(GTK_PANED(main_paned), left_vbox);
    gtk_paned_set_resize_start_child(GTK_PANED(main_paned), FALSE);
    gtk_paned_set_shrink_start_child(GTK_PANED(main_paned), TRUE);
    gtk_widget_set_size_request(left_vbox, 80, -1);

    wire_paned_controllers(main_paned, app);
    wire_paned_controllers(editor_paned, app);
    wire_focus_tracking(app);

    app->left_vbox = left_vbox;
    app->main_paned = main_paned;
    app->viewer_box = viewer_box;
    app->editor_paned = editor_paned;
    app->editor_box = editor_box;
    app->editor_notebook = editor_notebook;
    app->editor_dedicated = FALSE;
    app->editor_frac = 0.50;
    app->right_frac = 0.70;
    layout_build(app, GTK_ORIENTATION_HORIZONTAL);
    app_emit(app, APP_EVENT_CHROME_CHANGED);

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
    app->workspace_hbox = workspace_hbox;

    GtkWidget* outer_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* label_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_name(label_row, "statusbar");
    gtk_box_append(GTK_BOX(label_row), path_label);
    gtk_box_append(GTK_BOX(label_row), breadcrumb_box);
    gtk_box_append(GTK_BOX(label_row), sep_label);
    gtk_box_append(GTK_BOX(label_row), ws_minimap);
    gtk_box_append(GTK_BOX(label_row), layouts_btn);
    gtk_box_append(GTK_BOX(label_row), ws_div_label);
    gtk_box_append(GTK_BOX(label_row), files_btn);
    gtk_box_append(GTK_BOX(label_row), preview_btn);
    gtk_box_append(GTK_BOX(label_row), term_btn);
    gtk_box_append(GTK_BOX(label_row), pill_div_label);
    gtk_box_append(GTK_BOX(label_row), help_btn);
    gtk_box_append(GTK_BOX(outer_vbox), label_row);
    gtk_box_append(GTK_BOX(outer_vbox), workspace_hbox);
    gtk_box_append(GTK_BOX(outer_vbox), create_status_bar(app));
    app_emit(app, APP_EVENT_STATUS_CHANGED);

    /* Dialogs are drawn over the window content, never in their own window */
    gtk_window_set_child(GTK_WINDOW(window), app_dialog_window_overlay_new(outer_vbox));
    apply_panel_borders();
    gtk_window_present(GTK_WINDOW(window));

    if (start_fullscreen) {
        gtk_window_fullscreen(GTK_WINDOW(window));
    }

    select_first_row(app);
    gtk_widget_grab_focus(tree_view);
    workspace_apply_default_layout(app); /* saved default layout (Alt+L), if any */

    g_main_loop_run(loop);

    audio_preview_stop(app);
    video_preview_stop(app);
    editor_save_all_buffers(app);
    stop_directory_monitors(app);
    git_tab_cleanup(app);
    if (app->git_statuses != NULL) {
        g_hash_table_destroy(app->git_statuses);
        app->git_statuses = NULL;
    }
    if (app->expanded_paths != NULL) {
        g_hash_table_destroy(app->expanded_paths);
        app->expanded_paths = NULL;
    }
    bookmarks_free(app);
    notes_free(app);
    remote_free(app);
    if (app->org_folded_lines != NULL) {
        g_hash_table_destroy(app->org_folded_lines);
        app->org_folded_lines = NULL;
    }
    if (app->buffers != NULL) {
        g_ptr_array_unref(app->buffers);
    }
    if (app->dir_cancellable != NULL) {
        g_cancellable_cancel(app->dir_cancellable);
        g_clear_object(&app->dir_cancellable);
    }
    if (app->preview_cancellable != NULL) {
        g_cancellable_cancel(app->preview_cancellable);
        g_clear_object(&app->preview_cancellable);
    }
    for (gint i = 0; i < app->num_workspaces; i++) {
        editor_free_tabs(app, app->workspaces[i].editors, (i == app->current_ws));
        app->workspaces[i].editors = NULL;
        g_clear_object(&app->workspaces[i].editor_panel);
        g_free(app->workspaces[i].remote_id);
        app->workspaces[i].remote_id = NULL;
    }

    g_main_loop_unref(loop);

    return 0;
}
