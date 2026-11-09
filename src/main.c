#include "core/app_paths.h"
#include "core/i18n.h"
#include "chrome/topbar.h"
#include "widgets/dialog.h"
#include "workspace/layout_switcher.h"
#include "theme/icons.h"
#include "explorer/fs_watch.h"
#include "preview/preview_state.h"
#include "widgets/panels.h"
#include "core/app_events.h"
#include "theme/text_style.h"
#include "explorer/tree_nav.h"
#include "theme/theme.h"
#include "chrome/themes.h"
#include "chrome/preferences.h"
#include "core/settings_store.h"
#include "chrome/status_bar.h"
#include "chrome/sidebar.h"
#include "workspace/workspace.h"
#include "chrome/help.h"
#include "explorer/file_list.h"
#include "git/git_state.h"
#include "explorer/explorer_keys.h"
#include "preview/viewer.h"
#include "media/image_preview.h"
#include "preview/text_chunks.h"
#include "media/video.h"
#include "media/audio.h"
#include "widgets/minimap.h"
#include "search/search.h"
#include "chrome/ui.h"
#include "editor/editor.h"
#include "git/git_tab.h"
#include "explorer/bookmarks.h"
#include "notes/notes.h"
#include "explorer/remote.h"
#include "games/games.h"
#include "explorer/files_menu.h"
#include "workspace/session.h"
#include "workspace/session_bookmark.h"
#include "org/orgmode.h"
#include "org/org_fold.h"
#include "terminal/terminal.h"
#include "widgets/diff_gutter.h"
#include "widgets/color_column.h"
#include "canvas/canvas_view.h"
#include "notes/note_types.h"
#include "code/code_intel.h"
#include "code/code_nav.h"
#include "editor/editor_modes.h"
#include <stdio.h>
#include <stdlib.h>
#include "widgets/focus_route.h"
#include "preview/preview_tagbar.h"
#include "preview/preview_full.h"
#include "core/ui_probe.h"
#include <string.h>

/* The window is closed: ask about unsaved changes like Ctrl+Q, then remember
 * the workspaces for the next launch. With a dialog open, close the dialog
 * only: its nested main loop must return before the window goes away. */
static gboolean on_window_close_request(GtkWindow* window, gpointer data) {
    if (app_dialog_is_open(window)) {
        app_dialog_dismiss_top(window);
        return TRUE;
    }
    return !ui_confirm_quit((AppData*)data);
}

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
        fs_watch_git_changed(app);
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

/* File given on the command line: edit it in the dedicated editor panel,
 * with the explorer and the preview hidden */
static void open_start_file(AppData* app, const gchar* path) {
    select_by_path(app, path);
    gchar* name = g_path_get_basename(path);
    editor_open_file(app, name, path);
    g_free(name);
    EditorTab* tab = editor_current_tab(app);
    if (tab == NULL || g_strcmp0(tab->path, path) != 0) {
        return; /* not editable (media, too large...): keep the preview */
    }
    if (!app->editor_dedicated) {
        editor_set_dedicated(app, TRUE);
    }
    if (app->left_vbox != NULL && gtk_widget_get_visible(app->left_vbox)) {
        on_files_pill_clicked(NULL, app);
    }
    if (app->viewer_box != NULL && gtk_widget_get_visible(app->viewer_box)) {
        on_preview_pill_clicked(NULL, app);
    }
    if (tab->view != NULL) {
        gtk_widget_grab_focus(tab->view);
    }
}

static void print_usage(FILE* out) {
    /* TRANSLATORS: keep the option names (-h, --help...) and the metavariables
     * <file>, <folder>, <note>, WIDTHxHEIGHT untranslated, or translate the
     * metavariables everywhere they appear */
    fprintf(out,
            _("Usage: %s [options] [<file>|<folder>]\n"
            "       %s [options] -n <note>\n"
            "\n"
            "Without argument, reopens the workspaces of the last session (else\n"
            "shows the current directory; \".\" always shows it).\n"
            "A folder is shown in the explorer; a file is opened in the dedicated\n"
            "editor panel, with the explorer and the preview hidden.\n"
            "With -n, the argument is a note: \"Title\" or \"Notebook/Title\"; a\n"
            ".org, .md or .txt extension sets the type. Missing notes are created.\n"
            "\n"
            "Options:\n"
            "  -h, --help                       show this help and exit\n"
            "  -V, --version                    show the version and exit\n"
            "  -e, --editor emacs|vim|notepad   default editor mode of new tabs\n"
            "                                   (Org files always open in Org-mode)\n"
            "  -n, --note                       the argument is a note, not a file\n"
            "  -f, --fullscreen                 start in fullscreen\n"
            "  -s, --size WIDTHxHEIGHT          window size (e.g. 1920x1080)\n"),
            APP_DIR_NAME, APP_DIR_NAME);
}

/* -e / --editor value: FALSE if it is not a known editor mode */
static gboolean parse_editor_mode(const char* val, EditorMode* mode) {
    if (g_ascii_strcasecmp(val, "notepad") == 0) {
        *mode = EDITOR_MODE_NOTEPAD;
    } else if (g_ascii_strcasecmp(val, "vim") == 0) {
        *mode = EDITOR_MODE_VIM;
    } else if (g_ascii_strcasecmp(val, "emacs") == 0) {
        *mode = EDITOR_MODE_EMACS;
    } else {
        return FALSE;
    }
    return TRUE;
}

/* -s / --size value: FALSE if it is not WIDTHxHEIGHT */
static gboolean parse_size(const char* val, int* width, int* height) {
    int w = 0, h = 0;
    char sep = 0, tail = 0;
    if (sscanf(val, "%d %c %d %c", &w, &sep, &h, &tail) == 3 &&
        (sep == 'x' || sep == 'X') && w > 0 && h > 0) {
        *width = w;
        *height = h;
        return TRUE;
    }
    return FALSE;
}

/* Value of an option written "-x VALUE", "--long VALUE" or "--long=VALUE";
 * NULL when `arg` is not this option. Exits when the value is missing. */
static const char* option_value(int argc, char** argv, int* i,
                                const char* short_opt, const char* long_opt) {
    const char* arg = argv[*i];
    if (g_strcmp0(arg, short_opt) == 0 || g_strcmp0(arg, long_opt) == 0) {
        if (*i + 1 >= argc) {
            g_printerr(_("%s: option '%s' requires an argument\n"), APP_DIR_NAME, arg);
            print_usage(stderr);
            exit(2);
        }
        (*i)++;
        return argv[*i];
    }
    gsize len = strlen(long_opt);
    if (strncmp(arg, long_opt, len) == 0 && arg[len] == '=') {
        return arg + len + 1;
    }
    return NULL;
}

/* Top-bar gear button */
static void on_preferences_clicked(GtkButton* button, gpointer data) {
    (void)button;
    preferences_show(data, NULL);
}

int main(int argc, char** argv) {
    i18n_init(argc > 0 ? argv[0] : NULL); /* before any message is printed */
    gboolean start_fullscreen = FALSE;
    /* Preferences (settings.ini), then the command line over them */
    Settings prefs;
    gchar* settings_path = settings_store_path();
    settings_store_load(settings_path, &prefs);
    g_free(settings_path);
    int win_width = prefs.width;
    int win_height = prefs.height;
    EditorMode default_editor_mode = preferences_editor_mode(prefs.editor);
    const char* target = NULL; /* <file> or <folder> argument */
    gboolean options_done = FALSE;
    gboolean target_is_note = FALSE;
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        const char* val = NULL;
        if (options_done || arg[0] != '-' || arg[1] == '\0') {
            if (target != NULL) {
                g_printerr(_("%s: only one file or folder can be given\n"), APP_DIR_NAME);
                print_usage(stderr);
                return 2;
            }
            target = arg;
        } else if (g_strcmp0(arg, "--") == 0) {
            options_done = TRUE;
        } else if (g_strcmp0(arg, "-h") == 0 || g_strcmp0(arg, "--help") == 0) {
            print_usage(stdout);
            return 0;
        } else if (g_strcmp0(arg, "-V") == 0 || g_strcmp0(arg, "--version") == 0) {
            printf("%s %s\n", APP_NAME, APP_VERSION);
            return 0;
        } else if (g_strcmp0(arg, "-n") == 0 || g_strcmp0(arg, "--note") == 0) {
            target_is_note = TRUE;
        } else if (g_strcmp0(arg, "-f") == 0 || g_strcmp0(arg, "--fullscreen") == 0) {
            start_fullscreen = TRUE;
        } else if ((val = option_value(argc, argv, &i, "-e", "--editor")) != NULL) {
            if (!parse_editor_mode(val, &default_editor_mode)) {
                g_printerr(_("%s: unknown editor '%s' (expected emacs, vim or notepad)\n"),
                           APP_DIR_NAME, val);
                return 2;
            }
        } else if ((val = option_value(argc, argv, &i, "-s", "--size")) != NULL) {
            if (!parse_size(val, &win_width, &win_height)) {
                g_printerr(_("[ Invalid size '%s': expected format WIDTHxHEIGHT (e.g. --size 1920x1080) ]\n"),
                           val);
            }
        } else {
            g_printerr(_("%s: unknown option '%s'\n"), APP_DIR_NAME, arg);
            print_usage(stderr);
            return 2;
        }
    }

    if (target_is_note && target == NULL) {
        g_printerr(_("%s: option '-n' needs a note name\n"), APP_DIR_NAME);
        print_usage(stderr);
        return 2;
    }

    g_set_prgname(APP_DIR_NAME);
    g_set_application_name(APP_NAME);

    /* Force GTK 4 built-in Default dark theme to avoid GTK 3 system theme CSS errors */
    g_setenv("GTK_THEME", "Default:dark", TRUE);

    /* Prefer classic playbin in GstPlay to avoid decodebin3 collection assertions */
    g_setenv("GST_PLAY_USE_PLAYBIN3", "0", FALSE);

    gtk_init();
    ui_probe_install(); /* LOOKIN_UI_PROBE: UI tests only */
#ifdef HAVE_GSV
    gtk_source_init();
#endif

    /* The chosen theme sets the palette and fonts before any widget is
     * made; GTK's own widgets follow its dark or light kind */
    gboolean dark_theme = theme_apply_saved();
    GtkSettings* settings = gtk_settings_get_default();
    if (settings != NULL) {
        g_object_set(settings,
                     "gtk-theme-name", "Default",
                     "gtk-application-prefer-dark-theme", dark_theme,
                     NULL);
    }

    GMainLoop* loop = g_main_loop_new(NULL, FALSE);

    GtkWidget* window = gtk_window_new();
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), loop);
    gtk_window_set_title(GTK_WINDOW(window), APP_NAME " " APP_VERSION);
    gtk_window_set_default_icon_name(APP_ID);
    gtk_window_set_default_size(GTK_WINDOW(window), win_width, win_height);

    /* Get current directory */
    gchar* cwd_alloc = g_get_current_dir();
    const gchar* cwd = (cwd_alloc != NULL) ? cwd_alloc : g_get_home_dir();
    if (cwd == NULL) {
        cwd = "/";
    }

    /* Optional CLI argument: folder to display, or file to edit (default: cwd) */
    const char* start_dir = cwd;
    gchar* target_dir = NULL;  /* parent directory of the file to edit */
    gchar* start_file = NULL;  /* absolute path of the file to edit */
    if (target != NULL && !target_is_note) {
        if (g_file_test(target, G_FILE_TEST_IS_DIR)) {
            start_dir = target;
        } else if (g_file_test(target, G_FILE_TEST_IS_REGULAR)) {
            start_file = g_canonicalize_filename(target, cwd);
            target_dir = g_path_get_dirname(start_file);
            start_dir = target_dir;
        } else {
            g_printerr(_("[ '%s' is not a file or a directory: showing %s instead ]\n"),
                       target, cwd);
        }
    }


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

    /* Viewer zone: notebook (page 0 is preview stack, additional pages are editor tabs) */
    GtkWidget* preview_stack = gtk_stack_new();
    gtk_widget_set_name(preview_stack, "preview-stack");
    gtk_widget_set_hexpand(preview_stack, TRUE);
    gtk_widget_set_vexpand(preview_stack, TRUE);
    /* Filled once AppData exists: column line, minimap and tagbar around the text */
    GtkWidget* text_page = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_stack_add_named(GTK_STACK(preview_stack), text_page, "text");

    GtkWidget* notebook = gtk_notebook_new();
    gtk_widget_set_hexpand(notebook, TRUE);
    gtk_widget_set_vexpand(notebook, TRUE);
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
    gtk_notebook_set_group_name(GTK_NOTEBOOK(notebook), "editor-tabs");
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), preview_stack,
                             icon_label_new("document-print-preview", _("Preview")));

    /* Preview panel: notebook, its search bar below it */
    GtkWidget* viewer_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(viewer_row, TRUE);
    gtk_widget_set_vexpand(viewer_row, TRUE);
    gtk_box_append(GTK_BOX(viewer_row), notebook);
    GtkWidget* viewer_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(viewer_box, "viewer-box");
    gtk_widget_set_hexpand(viewer_box, TRUE);
    gtk_widget_set_vexpand(viewer_box, TRUE);
    gtk_box_append(GTK_BOX(viewer_box), viewer_row);

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
#ifndef HAVE_VTE
    GtkWidget* term_stub = gtk_label_new(
        _("Terminal panel disabled:\ninstall libvte-2.91-gtk4-dev then rebuild (make)."));
    gtk_widget_set_halign(term_stub, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(term_stub, GTK_ALIGN_CENTER);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(term_scroll), term_stub);
#endif

    /* Allocate and wire AppData */
    AppData* app = g_new0(AppData, 1);

    /* Event subscriptions: before any code can emit (see app_events.h) */
    app_events_subscribe(app, APP_EVENT_CHROME_CHANGED, update_path_label);
    app_events_subscribe(app, APP_EVENT_STATUS_CHANGED, update_status_bar);
    app_events_subscribe(app, APP_EVENT_STATUS_CHANGED, editor_modes_refresh_bars);
    app_events_subscribe(app, APP_EVENT_GIT_STATUS_CHANGED, editor_modes_refresh_git);
    app_events_subscribe(app, APP_EVENT_LAYOUT_CHANGED, workspace_save_current);
    app_events_subscribe(app, APP_EVENT_LAYOUT_REBUILT, workspace_apply_positions);
    app_events_subscribe(app, APP_EVENT_FILES_CHANGED, refresh_listing);
    app_events_subscribe(app, APP_EVENT_PREVIEW_STALE, viewer_reload_preview);
    app_events_subscribe(app, APP_EVENT_GIT_STATUS_CHANGED, refresh_git_status_in_place);
    app_events_subscribe(app, APP_EVENT_GIT_STATUS_CHANGED, refresh_diff_gutters);
    app_events_subscribe(app, APP_EVENT_FILES_CHANGED, refresh_diff_gutters);
    app_events_subscribe(app, APP_EVENT_FILES_CHANGED, themes_on_files_changed);
    app_events_subscribe(app, APP_EVENT_FOCUS_NEXT_PANEL, focus_next_panel);
    app_events_subscribe(app, APP_EVENT_FOCUS_PREV_PANEL, focus_prev_panel);
    app_events_subscribe(app, APP_EVENT_CLOSE_TAB, close_shown_tab);
    app_events_subscribe(app, APP_EVENT_SAVE_WORKSPACE_BOOKMARK, session_bookmark_save);
    app_events_subscribe(app, APP_EVENT_OPEN_WORKSPACE_BOOKMARK, session_bookmark_open);
    app->window = GTK_WINDOW(window);
    app->main_loop = loop;
    app->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
#ifdef HAVE_GSV
    apply_vscode_scheme(app->buffer, text_view);
#endif
    app->default_editor_mode = default_editor_mode;
    preferences_apply_startup(app, &prefs);
    app->base_dir = g_strdup(start_dir);
    app->start_dir = g_canonicalize_filename(start_dir, NULL);
    app->term_scroll = term_scroll;
#ifdef HAVE_VTE
    terminal_init(app, term_scroll);
#endif
    workspaces_init(app);
    GtkWidget* ws_minimap = create_workspace_minimap(app);
    GtkWidget* layouts_btn = icon_button_new("view-grid", _("Workspace Layouts (Alt+L)"));
    gtk_widget_add_css_class(layouts_btn, "status-pill");
    g_signal_connect_swapped(layouts_btn, "clicked", G_CALLBACK(layout_switcher_show), app);
    app->layouts_btn = layouts_btn;
    GtkWidget* prefs_btn = icon_button_new("applications-system", _("Preferences (Ctrl+,)"));
    gtk_widget_add_css_class(prefs_btn, "status-pill");
    g_signal_connect(prefs_btn, "clicked", G_CALLBACK(on_preferences_clicked), app);
    app->tree = explorer;
    app->tree_view = tree_view;
    app->expanded_paths = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    g_signal_connect(window, "notify::is-active", G_CALLBACK(on_window_active_changed), app);
    search_init(app);
    bookmarks_init(app);
    bookmarks_attach_context_menu(app);
    notes_init(app);
    notes_attach_context_menu(app);
    remote_init(app);
    remote_attach_context_menu(app);
    games_init(app);
    files_menu_attach(app);
    orgmode_install_gtksourceview_spec();
    populate_tree(app, start_dir, FALSE, SORT_NAME);
    app->text_view = text_view;
    g_object_set_data(G_OBJECT(text_view), "app", app);
    code_nav_attach_preview(app, editor_mode_message);   /* Ctrl+Click in source previews */
    GtkWidget* top_row = topbar_new(app, ws_minimap, layouts_btn, prefs_btn);
    /* Left-aligned action widget inside preview notebook tab header: Split/Dock button */
    gtk_notebook_set_action_widget(GTK_NOTEBOOK(notebook), topbar_split_button_new(app), GTK_PACK_START);
    app_emit(app, APP_EVENT_CHROME_CHANGED);

    app->notebook = notebook;
    app->preview_stack = preview_stack;
    /* Column line over code previews; minimap then headings beside the
     * text, as in editors */
    GtkWidget* text_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(text_row, TRUE);
    gtk_widget_set_vexpand(text_row, TRUE);
    gtk_box_append(GTK_BOX(text_row), color_column_wrap(right_scroll, text_view));
    gtk_box_append(GTK_BOX(text_row), minimap_viewer_new(app));
    gtk_box_append(GTK_BOX(text_page), preview_tagbar_wrap(app, preview_full_wrap(app, text_row), text_view));
    color_column_hide_on_change(text_view);
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
    focus_route_mark_nav(tree_view);

    GtkGesture* tree_click_ctrl = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(tree_click_ctrl), GDK_BUTTON_PRIMARY);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(tree_click_ctrl), GTK_PHASE_CAPTURE);
    g_signal_connect(tree_click_ctrl, "pressed", G_CALLBACK(on_tree_click_pressed), app);
    gtk_widget_add_controller(tree_view, GTK_EVENT_CONTROLLER(tree_click_ctrl));

    GtkEventController* viewer_key_ctrl = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(viewer_key_ctrl, GTK_PHASE_CAPTURE);
    g_signal_connect(viewer_key_ctrl, "key-pressed", G_CALLBACK(on_viewer_key_press), app);
    gtk_widget_add_controller(text_view, viewer_key_ctrl);
    focus_route_mark_nav(text_view);
    image_preview_attach_zoom(app, text_view);
    text_chunks_attach(app);

    GtkGesture* viewer_click_ctrl = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(viewer_click_ctrl), GDK_BUTTON_PRIMARY);
    g_signal_connect(viewer_click_ctrl, "pressed", G_CALLBACK(on_viewer_click_pressed), app);
    gtk_widget_add_controller(text_view, GTK_EVENT_CONTROLLER(viewer_click_ctrl));

    /* Search bars at the bottom of the explorer and of the preview */
    gtk_box_append(GTK_BOX(viewer_box), search_bar_new(app, TRUE));
    gtk_box_append(GTK_BOX(viewer_box), create_preview_bar(app));

    GtkWidget* left_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(left_vbox, "left-sidebar");
    gtk_widget_set_hexpand(left_vbox, TRUE);
    gtk_widget_set_vexpand(left_vbox, TRUE);
    gtk_box_append(GTK_BOX(left_vbox), create_sidebar_header(app));
    gtk_box_append(GTK_BOX(left_vbox), left_scroll);
    gtk_box_append(GTK_BOX(left_vbox), search_bar_new(app, FALSE));
    gtk_box_append(GTK_BOX(left_vbox), create_explorer_bar(app));

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
    layout_build(app, GTK_ORIENTATION_HORIZONTAL);
    app_emit(app, APP_EVENT_CHROME_CHANGED);

    /* Overlay badge for pane resize dimensions feedback */
    GtkWidget* overlay = gtk_overlay_new();
    gtk_widget_set_hexpand(overlay, TRUE);
    gtk_widget_set_vexpand(overlay, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), main_paned);

    panels_size_popup_attach(app, GTK_OVERLAY(overlay));

    GtkWidget* workspace_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(workspace_hbox, TRUE);
    gtk_widget_set_vexpand(workspace_hbox, TRUE);
    gtk_box_append(GTK_BOX(workspace_hbox), overlay);
    gtk_box_append(GTK_BOX(workspace_hbox), app->help_window);
    app->workspace_hbox = workspace_hbox;

    GtkWidget* outer_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(outer_vbox), top_row);
    gtk_box_append(GTK_BOX(outer_vbox), workspace_hbox);
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
    g_signal_connect(window, "close-request", G_CALLBACK(on_window_close_request), app);
    if (target == NULL) {
        session_restore(app); /* the workspaces of the last session, if any */
    }
    if (target_is_note) {
        gchar* note_path = notes_find_or_create(app, target);
        if (note_path != NULL) {
            if (app->sidebar_view != SIDEBAR_VIEW_NOTES) {
                toggle_notes_view(app);
            }
            if (note_type_from_filename(note_path) == NOTE_TYPE_CANVAS) {
                select_by_path(app, note_path);
                gchar* title = g_path_get_basename(note_path);
                *strrchr(title, '.') = '\0'; /* ".canvas" */
                canvas_view_open(app, note_path, title);
                g_free(title);
            } else {
                open_start_file(app, note_path);
            }
            g_free(note_path);
        } else {
            g_printerr(_("[ Invalid note name '%s': expected Title or Notebook/Title ]\n"), target);
        }
    } else if (start_file != NULL) {
        open_start_file(app, start_file);
    }
    g_free(start_file);
    g_free(target_dir);
    g_free(cwd_alloc);

    g_main_loop_run(loop);

    audio_preview_free(app);
    video_preview_free(app);
    editor_save_all_buffers(app);
    code_intel_shutdown(app);   /* language servers: shutdown, exit */
    fs_watch_free(app);
    git_tab_cleanup(app);
    panels_free(app);
    g_clear_pointer(&app->git_state, git_state_free);
    if (app->expanded_paths != NULL) {
        g_hash_table_destroy(app->expanded_paths);
        app->expanded_paths = NULL;
    }
    search_free(app);
    bookmarks_free(app);
    notes_free(app);
    remote_free(app);
    org_fold_free(app);
    preview_state_free(app);
    sidebar_free(app);
    topbar_free(app);
    workspace_view_free(app);
    status_bar_free(app);
    if (app->dir_cancellable != NULL) {
        g_cancellable_cancel(app->dir_cancellable);
        g_clear_object(&app->dir_cancellable);
    }
    cancel_git_status_scan(app);
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
#ifdef HAVE_VTE
    terminal_free(app);
#endif
    app_events_free(app);

    g_main_loop_unref(loop);

    return 0;
}
