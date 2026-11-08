/* Right-click menu of the files view (same labels and keys as the toolbar
 * tooltips and the README tables). */
#include "explorer/files_menu.h"
#include "preview/preview_state.h"
#include "core/i18n.h"
#include "core/app_events.h"
#include "explorer/bookmarks.h"
#include "preview/browser.h"
#include "editor.h"
#include "explorer/file_list.h"
#include "explorer/file_moves.h"
#include "core/file_types.h"
#include "explorer/nav_keys.h"
#include "widgets/panels.h"
#include "terminal.h"
#include "explorer/tree_nav.h"

typedef enum {
    FM_OPEN,
    FM_OPEN_SIDE,
    FM_OPEN_DEFAULT_APP,
    FM_OPEN_BROWSER,
    FM_RENAME,
    FM_TRASH,
    FM_COPY_PATH,
    FM_BOOKMARK,
    FM_TERMINAL,
    FM_NEW_FILE,
    FM_NEW_FOLDER,
    FM_UNDO
} FilesMenuAction;

typedef struct {
    AppData* app;
    FilesMenuAction action;
    gchar* path; /* the entry the menu was opened on (or the folder shown) */
} FilesMenuRun;

static void on_launch_done(GObject* source, GAsyncResult* res, gpointer data) {
    AppData* app = (AppData*)data;
    GError* err = NULL;
    if (!gtk_file_launcher_launch_finish(GTK_FILE_LAUNCHER(source), res, &err)) {
        if (!g_error_matches(err, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) {
            gchar* msg = g_strdup_printf(_("Cannot open with the default application: %s"), err->message);
            app_notice(app, msg, FALSE);
            g_free(msg);
        }
        g_clear_error(&err);
    }
    g_object_unref(source);
}

static void open_default_app(AppData* app, const gchar* path) {
    GFile* file = g_file_new_for_path(path);
    GtkFileLauncher* launcher = gtk_file_launcher_new(file);
    gtk_file_launcher_launch(launcher, app->window, NULL, on_launch_done, app);
    g_object_unref(file);
}

static void copy_path(AppData* app, const gchar* path) {
    GdkClipboard* clip = gtk_widget_get_clipboard(GTK_WIDGET(app->window));
    gdk_clipboard_set_text(clip, path);
    gchar* msg = g_strdup_printf(_("Copied “%s”"), path);
    app_notice(app, msg, FALSE);
    g_free(msg);
}

static void open_side(AppData* app, const gchar* path) {
    gchar* name = g_path_get_basename(path);
    editor_open_dedicated(app, name, path, NULL);
    g_free(name);
}

static void open_browser(AppData* app, const gchar* path) {
    gchar* uri = g_filename_to_uri(path, NULL, NULL);
    if (uri != NULL) browser_open(app, uri);
    g_free(uri);
}

static gboolean run_action(gpointer data) {
    FilesMenuRun* r = (FilesMenuRun*)data;
    AppData* app = r->app;
    switch (r->action) {
    case FM_OPEN:             activate_selected_entry(app); break;
    case FM_OPEN_SIDE:        open_side(app, r->path); break;
    case FM_OPEN_DEFAULT_APP: open_default_app(app, r->path); break;
    case FM_OPEN_BROWSER:     open_browser(app, r->path); break;
    case FM_RENAME:           rename_selected_in_tree(app); break;
    case FM_TRASH:            trash_selected_in_tree(app); break;
    case FM_COPY_PATH:        copy_path(app, r->path); break;
    case FM_BOOKMARK:         bookmarks_add_from_context(app); break;
    case FM_TERMINAL:         terminal_open(app, FALSE, r->path); break;
    case FM_NEW_FILE:         create_new_file_in_tree(app); break;
    case FM_NEW_FOLDER:       create_new_folder_in_tree(app); break;
    case FM_UNDO:             undo_last_file_move(app); break;
    }
    g_free(r->path);
    g_free(r);
    return G_SOURCE_REMOVE;
}

static void on_item_clicked(GtkButton* btn, gpointer data) {
    GtkWidget* popover = GTK_WIDGET(data);
    FilesMenuRun* r = g_new0(FilesMenuRun, 1);
    r->app = (AppData*)g_object_get_data(G_OBJECT(btn), "app");
    r->action = (FilesMenuAction)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "action"));
    r->path = g_strdup(g_object_get_data(G_OBJECT(popover), "path"));
    gtk_popover_popdown(GTK_POPOVER(popover));
    /* Run after the popover is gone: actions may open modal dialogs */
    g_idle_add(run_action, r);
}

static void menu_item(GtkWidget* box, GtkWidget* popover, AppData* app,
                      const gchar* text, const gchar* hint, FilesMenuAction action) {
    GtkWidget* btn = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(row), label);
    if (hint != NULL) {
        GtkWidget* key = gtk_label_new(hint);
        gtk_widget_add_css_class(key, "dim-label");
        gtk_box_append(GTK_BOX(row), key);
    }
    gtk_button_set_child(GTK_BUTTON(btn), row);
    g_object_set_data(G_OBJECT(btn), "app", app);
    g_object_set_data(G_OBJECT(btn), "action", GINT_TO_POINTER(action));
    g_signal_connect(btn, "clicked", G_CALLBACK(on_item_clicked), popover);
    gtk_box_append(GTK_BOX(box), btn);
}

static void menu_separator(GtkWidget* box) {
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep, 3);
    gtk_widget_set_margin_bottom(sep, 3);
    gtk_box_append(GTK_BOX(box), sep);
}

static gboolean unparent_idle(gpointer data) {
    gtk_widget_unparent(GTK_WIDGET(data));
    g_object_unref(data);
    return G_SOURCE_REMOVE;
}

static void on_popover_closed(GtkPopover* popover, gpointer data) {
    (void)data;
    g_idle_add(unparent_idle, g_object_ref(popover));
}

/* Key of "open in the side panel" in the global mode (Vim t, Emacs C-o) */
static const gchar* side_panel_key(AppData* app) {
    switch (app_nav_style(app)) {
    case NAV_STYLE_VIM: return "t";
    case NAV_STYLE_EMACS: return "C-o";
    default: return NULL;
    }
}

static void fill_entry_menu(GtkWidget* box, GtkWidget* popover, AppData* app, ExplorerRow* row) {
    gboolean is_dir = explorer_row_is(row, "dir");
    menu_item(box, popover, app, _("Open"), "Enter", FM_OPEN);
    if (!is_dir) {
        FileKind kind = (g_strcmp0(row->path, preview_state_path(app)) == 0)
                            ? preview_state_kind(app)
                            : file_kind_of_path(row->path, row->name);
        if (file_kind_is_text(kind)) {
            menu_item(box, popover, app, _("Open in Side Panel"), side_panel_key(app), FM_OPEN_SIDE);
        } else {
            menu_item(box, popover, app, _("Open With Default App"), NULL, FM_OPEN_DEFAULT_APP);
        }
        if (browser_available() && is_html_file(row->name)) {
            menu_item(box, popover, app, _("Open in Browser Tab"), NULL, FM_OPEN_BROWSER);
        }
    } else {
        menu_item(box, popover, app, _("Open Terminal Here"), NULL, FM_TERMINAL);
    }
    menu_separator(box);
    menu_item(box, popover, app, _("Rename…"), "F2", FM_RENAME);
    menu_item(box, popover, app, _("Move to Trash…"), "Delete", FM_TRASH);
    menu_separator(box);
    menu_item(box, popover, app, _("Copy Path"), NULL, FM_COPY_PATH);
    menu_item(box, popover, app, _("Bookmark…"), "Ctrl+D", FM_BOOKMARK);
}

static void fill_folder_menu(GtkWidget* box, GtkWidget* popover, AppData* app) {
    menu_item(box, popover, app, _("New File…"), "a", FM_NEW_FILE);
    menu_item(box, popover, app, _("New Folder…"), "Shift+A", FM_NEW_FOLDER);
    menu_separator(box);
    menu_item(box, popover, app, _("Open Terminal Here"), NULL, FM_TERMINAL);
    menu_item(box, popover, app, _("Copy Path"), NULL, FM_COPY_PATH);
    menu_item(box, popover, app, _("Bookmark…"), "Ctrl+D", FM_BOOKMARK);
}

static void on_right_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    AppData* app = (AppData*)data;
    if (app == NULL || app->sidebar_view != SIDEBAR_VIEW_FILES) {
        return;
    }
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);

    ExplorerRow* row = explorer_tree_row_at(app->tree, x, y);
    gboolean entry = (explorer_row_is(row, "dir") || explorer_row_is(row, "file")) &&
                     row->path[0] != '\0';
    if (entry) {
        select_row(app, row);
    } else {
        explorer_tree_unselect_all(app->tree);
    }

    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_parent(popover, GTK_WIDGET(app->tree_view));
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    GdkRectangle rect = { (gint)x, (gint)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_widget_set_halign(popover, GTK_ALIGN_START);
    g_object_set_data_full(G_OBJECT(popover), "path",
                           g_strdup(entry ? row->path : app->base_dir), g_free);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    if (entry) {
        fill_entry_menu(box, popover, app, row);
    } else {
        fill_folder_menu(box, popover, app);
    }
    if (file_moves_can_undo(app)) {
        menu_separator(box);
        menu_item(box, popover, app, _("Undo"), "u", FM_UNDO);
    }

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), NULL);
    gtk_popover_popup(GTK_POPOVER(popover));
}

/* The mouse's back / forward buttons: the folder shown before / after */
static void on_side_button(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    AppData* app = (AppData*)data;
    guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
    if (button != 8 && button != 9) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED); /* left to the others */
        return;
    }
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    if (button == 8) {
        go_back_directory(app);
    } else {
        go_forward_directory(app);
    }
}

void files_menu_attach(AppData* app) {
    if (app == NULL || app->tree_view == NULL) {
        return;
    }
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
    g_signal_connect(click, "pressed", G_CALLBACK(on_right_click), app);
    gtk_widget_add_controller(GTK_WIDGET(app->tree_view), GTK_EVENT_CONTROLLER(click));

    GtkGesture* side = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(side), 0); /* any button: 8 and 9 kept */
    g_signal_connect(side, "pressed", G_CALLBACK(on_side_button), app);
    gtk_widget_add_controller(GTK_WIDGET(app->tree_view), GTK_EVENT_CONTROLLER(side));
}
