/* Top bar: clickable path breadcrumbs, the global mode pill and the layout/sort/hidden/help toggle pills (Help: user guide on click, cheat sheet on right click). */
#include "chrome/topbar.h"
#include "core/i18n.h"
#include "theme/icons.h"
#include "core/app_events.h"
#include "explorer/file_list.h"
#include "editor/editor.h"
#include "git/git_tab.h"
#include "chrome/help.h"
#include "chrome/help_guide.h"
#include "widgets/minimap.h"
#include "explorer/nav_history.h"
#include "explorer/nav_keys.h"
#include "explorer/remote.h"
#include "search/search.h"
#include "chrome/sidebar.h"
#include "terminal/terminal.h"
#include "chrome/status_bar.h"
#include "explorer/tree_nav.h"

/* Private to this file: the top bar's widgets (NULL until topbar_new()) */
struct TopBar {
    GtkWidget* path_label;      /* hidden text of the state (directory, sort, hidden, view) */
    GtkWidget* breadcrumb_box;  /* folders of base_dir */
    GtkWidget* history_back_btn; /* folder history arrows */
    GtkWidget* history_fwd_btn;
    GtkWidget* mode_btn;        /* global mode pill */
    GtkWidget* files_btn;       /* panel pills */
    GtkWidget* preview_btn;
    GtkWidget* term_btn;
    GtkWidget* help_btn;
    GtkWidget* split_btn;       /* action button of the preview notebook: dedicated editor panel */
};

static struct TopBar* bar_get(AppData* app) {
    if (app->topbar == NULL) {
        app->topbar = g_new0(struct TopBar, 1);
    }
    return app->topbar;
}

#if defined(G_OS_WIN32) || defined(_WIN32)
static void show_drive_switcher(GtkWidget* parent, AppData* app);
#endif

static void on_breadcrumb_clicked(GtkButton* btn, gpointer data) {
    AppData* app = (AppData*)data;
    const gchar* target = (const gchar*)g_object_get_data(G_OBJECT(btn), "target-path");
    if (target != NULL) {
#if defined(G_OS_WIN32) || defined(_WIN32)
        if (g_strcmp0(target, app->base_dir) == 0 &&
            g_ascii_isalpha(target[0]) && target[1] == ':' &&
            (target[2] == '\0' || ((target[2] == '\\' || target[2] == '/') && target[3] == '\0'))) {
            show_drive_switcher(GTK_WIDGET(btn), app);
            return;
        }
#endif
        if (app->sidebar_view == SIDEBAR_VIEW_BOOKMARKS || g_strcmp0(target, app->base_dir) != 0) {
            if (app->sidebar_view == SIDEBAR_VIEW_BOOKMARKS) {
                explorer_set_view(app, SIDEBAR_VIEW_FILES);
            }
            reload_directory(app, target);
        }
    }
}

void on_files_pill_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    AppData* app = (AppData*)data;
    if (app->left_vbox == NULL) {
        return;
    }
    gboolean cur = gtk_widget_get_visible(app->left_vbox);
    gboolean preview_vis = (app->viewer_box != NULL) && gtk_widget_get_visible(app->viewer_box);
    gboolean term_vis = (app->term_scroll != NULL) && gtk_widget_get_visible(app->term_scroll);
    gboolean editor_vis = app->editor_dedicated && (app->editor_box != NULL) && gtk_widget_get_visible(app->editor_box);
    if (cur && !preview_vis && !term_vis && !editor_vis) {
        return; /* keep at least one panel visible */
    }
    app_layout_begin(app);
    gtk_widget_set_visible(app->left_vbox, !cur);
    update_path_label(app);
    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    if (!cur) {
        gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
    } else if (preview_vis) {
        gtk_widget_grab_focus(app->text_view);
    } else if (editor_vis) {
        EditorTab* tab = editor_current_tab(app);
        if (tab != NULL && tab->view != NULL) {
            gtk_widget_grab_focus(tab->view);
        }
    }
}

void on_preview_pill_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    AppData* app = (AppData*)data;
    if (app->viewer_box == NULL) {
        return;
    }
    gboolean cur = gtk_widget_get_visible(app->viewer_box);
    gboolean files_vis = (app->left_vbox != NULL) && gtk_widget_get_visible(app->left_vbox);
    gboolean editor_vis = app->editor_dedicated && (app->editor_box != NULL) && gtk_widget_get_visible(app->editor_box);
    gboolean term_vis = (app->term_scroll != NULL) && gtk_widget_get_visible(app->term_scroll);
    if (cur && !files_vis && !term_vis && !editor_vis) {
        return; /* keep at least one panel visible */
    }
    app_layout_begin(app);
    gtk_widget_set_visible(app->viewer_box, !cur);
    update_path_label(app);
    app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    if (!cur) {
        minimap_update_visibility(app);
        gtk_widget_grab_focus(app->text_view);
    } else if (files_vis) {
        gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
    }
}

static void on_split_btn_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    editor_toggle_dedicated((AppData*)data);
}

static void on_term_pill_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    AppData* app = (AppData*)data;
    if (app->term_scroll == NULL) {
        return;
    }
    gboolean cur = gtk_widget_get_visible(app->term_scroll);
    gboolean files_vis = (app->left_vbox != NULL) && gtk_widget_get_visible(app->left_vbox);
    gboolean preview_vis = (app->viewer_box != NULL) && gtk_widget_get_visible(app->viewer_box);
    gboolean editor_vis = app->editor_dedicated && (app->editor_box != NULL) && gtk_widget_get_visible(app->editor_box);
    if (cur) {
        if (!files_vis && !preview_vis && !editor_vis) {
            return; /* keep at least one panel visible */
        }
        app_layout_begin(app);
        gtk_widget_set_visible(app->term_scroll, FALSE);
        update_path_label(app);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
        if (files_vis) {
            gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
        } else if (preview_vis) {
            gtk_widget_grab_focus(app->text_view);
        }
    } else {
        app_layout_begin(app);
        if (app->right_paned != NULL) {
            gtk_widget_set_visible(app->right_paned, TRUE);
        }
        gtk_widget_set_visible(app->term_scroll, TRUE);
#ifdef HAVE_VTE
        GPtrArray* panes = term_visible_panes(app);
        if (panes->len == 0) {
            term_spawn_pane(app, FALSE);
        } else {
            GtkWidget* target = (terminal_last_focused(app) != NULL)
                                    ? terminal_last_focused(app)
                                    : GTK_WIDGET(g_ptr_array_index(panes, 0));
            gtk_widget_grab_focus(target);
        }
        g_ptr_array_unref(panes);
#endif
        update_path_label(app);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    }
}

static void on_help_pill_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    /* Toggle: a click on the shown Help tab closes it */
    AppData* app = (AppData*)data;
    if (!help_guide_close_shown(app)) help_guide_show(app, NULL);
}

static void on_help_pill_right_clicked(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    toggle_help_panel((AppData*)data);
}

#define HISTORY_MENU_MAX 15

static void on_history_clicked(GtkButton* btn, gpointer data) {
    AppData* app = (AppData*)data;
    if (GTK_WIDGET(btn) == bar_get(app)->history_back_btn) {
        go_back_directory(app);
    } else {
        go_forward_directory(app);
    }
}

static gboolean unparent_idle(gpointer data) {
    gtk_widget_unparent(GTK_WIDGET(data));
    g_object_unref(data);
    return G_SOURCE_REMOVE;
}

static void on_history_menu_closed(GtkPopover* popover, gpointer data) {
    (void)data;
    g_idle_add(unparent_idle, g_object_ref(popover));
}

#if defined(G_OS_WIN32) || defined(_WIN32)
#include <windows.h>

static void on_drive_btn_clicked(GtkButton* b, gpointer data) {
    GtkWidget* popover = GTK_WIDGET(data);
    AppData* app = (AppData*)g_object_get_data(G_OBJECT(popover), "app");
    const gchar* drive_path = (const gchar*)g_object_get_data(G_OBJECT(b), "drive-path");
    gtk_popover_popdown(GTK_POPOVER(popover));
    if (app != NULL && drive_path != NULL) {
        reload_directory(app, drive_path);
    }
}

static void show_drive_switcher(GtkWidget* parent, AppData* app) {
    DWORD drives = GetLogicalDrives();
    if (drives == 0) return;

    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_parent(popover, parent);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    g_object_set_data(G_OBJECT(popover), "app", app);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);

    for (int i = 0; i < 26; i++) {
        if (drives & (1 << i)) {
            gchar letter = 'A' + i;
            gchar path[4] = { letter, ':', '\\', '\0' };
            gchar label[8];
            g_snprintf(label, sizeof(label), "%c:", letter);
            GtkWidget* b = gtk_button_new();
            gtk_button_set_child(GTK_BUTTON(b), glyph_label_new("root", label));
            gtk_button_set_has_frame(GTK_BUTTON(b), FALSE);
            g_object_set_data_full(G_OBJECT(b), "drive-path", g_strdup(path), g_free);
            g_signal_connect(b, "clicked", G_CALLBACK(on_drive_btn_clicked), popover);
            gtk_box_append(GTK_BOX(box), b);
        }
    }

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect(popover, "closed", G_CALLBACK(on_history_menu_closed), NULL);
    gtk_popover_popup(GTK_POPOVER(popover));
}
#endif

static void on_history_item_clicked(GtkButton* btn, gpointer data) {
    GtkWidget* popover = GTK_WIDGET(data);
    AppData* app = (AppData*)g_object_get_data(G_OBJECT(popover), "app");
    gboolean back = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(popover), "back"));
    guint steps = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(btn), "steps"));
    gtk_popover_popdown(GTK_POPOVER(popover));
    go_history_directory(app, back, steps);
}

/* The folder as shown in the menu: its name, then where it is (~ for home) */
static GtkWidget* history_item_new(const gchar* path) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gchar* name = g_path_get_basename(path);
    GtkWidget* label = gtk_label_new(name);
    g_free(name);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(row), label);

    gchar* parent = g_path_get_dirname(path);
    const gchar* home = g_get_home_dir();
    gsize home_len = home != NULL ? strlen(home) : 0;
    gchar* where = (home_len > 1 && g_str_has_prefix(parent, home) &&
                    (parent[home_len] == '\0' || parent[home_len] == '/' || parent[home_len] == '\\'))
                       ? g_strconcat("~", parent + home_len, NULL)
                       : g_strdup(parent);
    GtkWidget* dir = gtk_label_new(where);
    gtk_widget_add_css_class(dir, "dim-label");
    gtk_label_set_ellipsize(GTK_LABEL(dir), PANGO_ELLIPSIZE_START);
    gtk_label_set_max_width_chars(GTK_LABEL(dir), 48);
    gtk_box_append(GTK_BOX(row), dir);
    g_free(where);
    g_free(parent);

    GtkWidget* btn = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
    gtk_button_set_child(GTK_BUTTON(btn), row);
    gtk_widget_set_tooltip_text(btn, path);
    return btn;
}

static void on_history_right_clicked(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    AppData* app = (AppData*)data;
    struct TopBar* t = bar_get(app);
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    GtkWidget* button = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    gboolean back = button == t->history_back_btn;
    GPtrArray* folders = nav_history_folders(app, back);
    if (folders->len == 0) {
        g_ptr_array_unref(folders);
        return;
    }

    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_parent(popover, button);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    gtk_widget_set_halign(popover, GTK_ALIGN_START);
    g_object_set_data(G_OBJECT(popover), "app", app);
    g_object_set_data(G_OBJECT(popover), "back", GINT_TO_POINTER(back));

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    for (guint i = 0; i < folders->len && i < HISTORY_MENU_MAX; i++) {
        const gchar* path = g_ptr_array_index(folders, i);
        GtkWidget* item = history_item_new(path);
        /* A deleted folder is listed but cannot be picked */
        gtk_widget_set_sensitive(item, g_file_test(path, G_FILE_TEST_IS_DIR));
        g_object_set_data(G_OBJECT(item), "steps", GUINT_TO_POINTER(i + 1));
        g_signal_connect(item, "clicked", G_CALLBACK(on_history_item_clicked), popover);
        gtk_box_append(GTK_BOX(box), item);
    }
    g_ptr_array_unref(folders);

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect(popover, "closed", G_CALLBACK(on_history_menu_closed), NULL);
    gtk_popover_popup(GTK_POPOVER(popover));
}

static GtkWidget* history_button_new(AppData* app, const gchar* icon, const gchar* tooltip) {
    GtkWidget* btn = icon_button_new(icon, tooltip);
    gtk_widget_add_css_class(btn, "breadcrumb-item");
    g_signal_connect(btn, "clicked", G_CALLBACK(on_history_clicked), app);
    GtkGesture* right = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right), GDK_BUTTON_SECONDARY);
    g_signal_connect(right, "pressed", G_CALLBACK(on_history_right_clicked), app);
    gtk_widget_add_controller(btn, GTK_EVENT_CONTROLLER(right));
    return btn;
}

static GtkWidget* history_buttons_new(AppData* app) {
    struct TopBar* t = bar_get(app);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    t->history_back_btn = history_button_new(app, "go-previous",
                                               _("Back (Shift+H) — Right-Click: Folders Before"));
    t->history_fwd_btn = history_button_new(app, "go-next",
                                              _("Forward (Shift+L) — Right-Click: Folders After"));
    gtk_box_append(GTK_BOX(box), t->history_back_btn);
    gtk_box_append(GTK_BOX(box), t->history_fwd_btn);
    return box;
}

static const gchar* mode_name(EditorMode mode) {
    switch (mode) {
    case EDITOR_MODE_VIM:
        return "Vim";
    case EDITOR_MODE_EMACS:
        return "Emacs";
    default:
        return "Notepad";
    }
}

/* The global mode only: an editor tab's own mode is set from its tab menu */
static void on_mode_item_clicked(GtkButton* b, gpointer data) {
    GtkWidget* popover = GTK_WIDGET(data);
    AppData* app = g_object_get_data(G_OBJECT(b), "app");
    EditorMode mode = (EditorMode)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "mode"));
    gtk_popover_popdown(GTK_POPOVER(popover));
    if (app != NULL && mode != app->default_editor_mode) {
        app->default_editor_mode = mode;
        app->nav_prefix = NAV_PREFIX_NONE; /* a prefix of the old mode (g, C-x) */
        app->nav_recenter_step = 0;
        search_mode_changed(app);
        update_path_label(app);
    }
}

static void on_mode_pill_clicked(GtkButton* btn, gpointer data) {
    AppData* app = (AppData*)data;
    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_parent(popover, GTK_WIDGET(btn));
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);

    GtkWidget* title = gtk_label_new(_("Global Mode"));
    gtk_widget_add_css_class(title, "dim-label");
    gtk_widget_set_margin_bottom(title, 2);
    gtk_box_append(GTK_BOX(box), title);

    static const EditorMode modes[] = {EDITOR_MODE_NOTEPAD, EDITOR_MODE_EMACS, EDITOR_MODE_VIM};
    for (gsize i = 0; i < G_N_ELEMENTS(modes); i++) {
        GtkWidget* b = gtk_button_new_with_label(mode_name(modes[i]));
        gtk_button_set_has_frame(GTK_BUTTON(b), FALSE);
        if (modes[i] == app->default_editor_mode) {
            gtk_widget_add_css_class(b, "menu-item-current");
        }
        g_object_set_data(G_OBJECT(b), "app", app);
        g_object_set_data(G_OBJECT(b), "mode", GINT_TO_POINTER(modes[i]));
        g_signal_connect(b, "clicked", G_CALLBACK(on_mode_item_clicked), popover);
        gtk_box_append(GTK_BOX(box), b);
    }

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect(popover, "closed", G_CALLBACK(on_history_menu_closed), NULL);
    gtk_popover_popup(GTK_POPOVER(popover));
}

static GtkWidget* mode_button_new(AppData* app) {
    struct TopBar* t = bar_get(app);
    GtkWidget* btn = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
    gtk_widget_set_focusable(btn, FALSE);
    gtk_widget_add_css_class(btn, "status-pill");
    gtk_widget_set_tooltip_text(btn, _("Global Mode: Keys of the Explorer and Preview, Mode of New Editor Tabs"));
    g_signal_connect(btn, "clicked", G_CALLBACK(on_mode_pill_clicked), app);
    icon_button_set_content(btn, "input-keyboard", mode_name(app->default_editor_mode));
    g_object_set_data(G_OBJECT(btn), "mode-state", GINT_TO_POINTER((gint)app->default_editor_mode + 1));
    t->mode_btn = btn;
    return btn;
}

typedef struct {
    gchar* label;
    gchar* full_path;
    const gchar* glyph; /* icon before the label (icons.h), NULL for none */
} BreadcrumbSeg;

/* Top bar: clickable path breadcrumbs + right-aligned sort/hidden/help pills */
/* A panel pill of the top bar: icon and label, no frame, never focused */
static GtkWidget* pill_new(const gchar* icon, const gchar* label, const gchar* tooltip) {
    GtkWidget* btn = gtk_button_new();
    icon_button_set_content(btn, icon, label);
    gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
    gtk_widget_set_focusable(btn, FALSE);
    gtk_widget_add_css_class(btn, "status-pill");
    gtk_widget_set_tooltip_text(btn, tooltip);
    return btn;
}

/* A short vertical rule between groups of pills */
static GtkWidget* separator_new(void) {
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_add_css_class(sep, "topbar-sep");
    gtk_widget_set_valign(sep, GTK_ALIGN_CENTER);
    return sep;
}

void update_path_label(AppData* app) {
    static const gchar* sort_names[SORT_COUNT] = {"name", "date", "size", "ext"};
    struct TopBar* t = bar_get(app);

    if (t->path_label != NULL) {
        gchar* text = g_strdup_printf("Directory: %s   [sort: %s] [hidden: %s] [view: %s]",
                                       app->base_dir, sort_names[app->sort_mode],
                                       app->show_hidden ? "on" : "off",
                                       app->sidebar_view == SIDEBAR_VIEW_BOOKMARKS ? "bookmarks" : "files");
        gtk_label_set_text(GTK_LABEL(t->path_label), text);
        g_free(text);
    }

    if (t->breadcrumb_box != NULL && app->base_dir != NULL) {
        const gchar* built_dir = (const gchar*)g_object_get_data(G_OBJECT(t->breadcrumb_box), "built-dir");
        if (g_strcmp0(built_dir, app->base_dir) != 0) {
            GtkWidget* child;
            while ((child = gtk_widget_get_first_child(t->breadcrumb_box)) != NULL) {
                gtk_box_remove(GTK_BOX(t->breadcrumb_box), child);
            }

            GArray* segs = g_array_new(FALSE, FALSE, sizeof(BreadcrumbSeg));
            const gchar* home = g_get_home_dir();
            gsize home_len = (home != NULL) ? strlen(home) : 0;
            gboolean under_home = (home_len > 1 &&
                                   g_str_has_prefix(app->base_dir, home) &&
                                   (app->base_dir[home_len] == '\0' ||
                                    app->base_dir[home_len] == '/' ||
                                    app->base_dir[home_len] == '\\'));

            RemoteMachine* act_rm = remote_get_active_machine(app);
            gboolean is_remote_mount = (act_rm != NULL && act_rm->mount_point != NULL &&
                                        g_str_has_prefix(app->base_dir, act_rm->mount_point));

            gchar* accum = NULL;
            const gchar* rest = NULL;
            if (is_remote_mount) {
                gsize mount_len = strlen(act_rm->mount_point);
                gchar* root_title = g_strdup(act_rm->name ? act_rm->name : act_rm->host);
                BreadcrumbSeg root_seg = { root_title, g_strdup(act_rm->mount_point), "remote-connected" };
                g_array_append_val(segs, root_seg);
                accum = g_strdup(act_rm->mount_point);
                rest = app->base_dir + mount_len;
            } else if (under_home) {
                BreadcrumbSeg root_seg = { g_strdup("~"), g_strdup(home), "home" };
                g_array_append_val(segs, root_seg);
                accum = g_strdup(home);
                rest = app->base_dir + home_len;
            } else if (g_ascii_isalpha(app->base_dir[0]) && app->base_dir[1] == ':') {
                gchar drive_letter = g_ascii_toupper(app->base_dir[0]);
                gchar* root_title = g_strdup_printf("%c:", drive_letter);
                gchar* drive_root = g_strdup_printf("%c:\\", drive_letter);
                BreadcrumbSeg root_seg = { root_title, drive_root, "root" };
                g_array_append_val(segs, root_seg);
                accum = g_strdup(drive_root);
                gint offset = (app->base_dir[2] == '\\' || app->base_dir[2] == '/') ? 3 : 2;
                rest = app->base_dir + offset;
            } else {
                BreadcrumbSeg root_seg = { g_strdup("/"), g_strdup("/"), "root" };
                g_array_append_val(segs, root_seg);
                accum = g_strdup("/");
                rest = app->base_dir;
            }

            gchar* rest_norm = g_strdup(rest);
            for (gchar* p = rest_norm; *p != '\0'; p++) {
                if (*p == '\\') {
                    *p = '/';
                }
            }
            gchar** parts = g_strsplit(rest_norm, "/", -1);
            g_free(rest_norm);
            for (gint i = 0; parts != NULL && parts[i] != NULL; i++) {
                if (parts[i][0] == '\0') {
                    continue;
                }
                gchar* next_accum = g_build_filename(accum, parts[i], NULL);
                g_free(accum);
                accum = next_accum;

                BreadcrumbSeg seg = { g_strdup(parts[i]), g_strdup(accum), NULL };
                g_array_append_val(segs, seg);
            }
            g_strfreev(parts);
            g_free(accum);

            guint total = segs->len;
            for (guint i = 0; i < total; i++) {
                /* Collapse middle segments if path is very deep (> 6 segments) */
                if (total > 6 && i == 1) {
                    GtkWidget* sep = gtk_label_new("›");
                    gtk_widget_add_css_class(sep, "breadcrumb-sep");
                    gtk_box_append(GTK_BOX(t->breadcrumb_box), sep);

                    GtkWidget* dots = gtk_label_new("…");
                    gtk_widget_add_css_class(dots, "breadcrumb-sep");
                    gtk_box_append(GTK_BOX(t->breadcrumb_box), dots);

                    i = total - 4;
                }

                if (i > 0) {
                    GtkWidget* sep = gtk_label_new("›");
                    gtk_widget_add_css_class(sep, "breadcrumb-sep");
                    gtk_box_append(GTK_BOX(t->breadcrumb_box), sep);
                }

                BreadcrumbSeg* s = &g_array_index(segs, BreadcrumbSeg, i);
                gboolean is_last = (i == total - 1);
                const gchar* glyph = (is_last && i > 0) ? "folder" : s->glyph;
                GtkWidget* btn = gtk_button_new();
                if (glyph != NULL) {
                    gtk_button_set_child(GTK_BUTTON(btn), glyph_label_new(glyph, s->label));
                } else {
                    gtk_button_set_label(GTK_BUTTON(btn), s->label);
                }
                gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
                gtk_widget_set_focusable(btn, FALSE);
                gtk_widget_add_css_class(btn, is_last ? "breadcrumb-current"
                                                      : "breadcrumb-item");
                gtk_widget_set_tooltip_text(btn, s->full_path);
                g_object_set_data_full(G_OBJECT(btn), "target-path",
                                       g_strdup(s->full_path), g_free);
                g_signal_connect(btn, "clicked",
                                 G_CALLBACK(on_breadcrumb_clicked), app);
                gtk_box_append(GTK_BOX(t->breadcrumb_box), btn);
            }

            for (guint i = 0; i < segs->len; i++) {
                BreadcrumbSeg* s = &g_array_index(segs, BreadcrumbSeg, i);
                g_free(s->label);
                g_free(s->full_path);
            }
            g_array_unref(segs);

            g_object_set_data_full(G_OBJECT(t->breadcrumb_box), "built-dir",
                                   g_strdup(app->base_dir), g_free);
        }
    }

    if (t->history_back_btn != NULL) {
        gtk_widget_set_sensitive(t->history_back_btn, nav_history_can_go(app, TRUE));
        gtk_widget_set_sensitive(t->history_fwd_btn, nav_history_can_go(app, FALSE));
    }

    if (t->mode_btn != NULL) {
        /* Rebuilt only when the mode changes (stored as mode + 1) */
        gint state = (gint)app->default_editor_mode + 1;
        if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(t->mode_btn), "mode-state")) != state) {
            icon_button_set_content(t->mode_btn, "input-keyboard", mode_name(app->default_editor_mode));
            g_object_set_data(G_OBJECT(t->mode_btn), "mode-state", GINT_TO_POINTER(state));
        }
    }

    gboolean files_vis = (app->left_vbox == NULL) || gtk_widget_get_visible(app->left_vbox);
    gboolean preview_vis = (app->viewer_box == NULL) || gtk_widget_get_visible(app->viewer_box);
    gboolean editor_vis = app->editor_dedicated && (app->editor_box != NULL) && gtk_widget_get_visible(app->editor_box);
    gboolean term_vis = (app->term_scroll != NULL) && gtk_widget_get_visible(app->term_scroll);

    if (app->editor_paned != NULL) {
        gtk_widget_set_visible(app->editor_paned, preview_vis || editor_vis);
    }
    if (app->right_paned != NULL) {
        gtk_widget_set_visible(app->right_paned, preview_vis || editor_vis || term_vis);
    }

    if (t->files_btn != NULL) {
        if (files_vis) {
            gtk_widget_add_css_class(t->files_btn, "pill-active");
        } else {
            gtk_widget_remove_css_class(t->files_btn, "pill-active");
        }
    }
    if (t->preview_btn != NULL) {
        if (preview_vis) {
            gtk_widget_add_css_class(t->preview_btn, "pill-active");
        } else {
            gtk_widget_remove_css_class(t->preview_btn, "pill-active");
        }
    }
    if (t->split_btn != NULL) {
        /* Nothing to move or dock without an editor tab */
        gtk_widget_set_visible(t->split_btn, app->editors != NULL);
        if (app->editor_dedicated) {
            gtk_widget_add_css_class(t->split_btn, "tab-action-active");
            icon_button_set_content(t->split_btn, "view-paged", NULL);
            gtk_widget_set_tooltip_text(t->split_btn, _("Dock Editor Tabs into Preview Panel"));
        } else {
            gtk_widget_remove_css_class(t->split_btn, "tab-action-active");
            icon_button_set_content(t->split_btn, "view-dual", NULL);
            gtk_widget_set_tooltip_text(t->split_btn, _("Move Editor Tabs to a Dedicated Panel"));
        }
    }
    if (t->term_btn != NULL) {
        RemoteMachine* act_rm = remote_get_active_machine(app);
        if (act_rm != NULL) {
            gchar* term_label = g_strdup_printf(_("SSH: %s"), act_rm->name ? act_rm->name : act_rm->host);
            icon_button_set_content(t->term_btn, "network-server", term_label);
            g_free(term_label);
            gchar* tip = g_strdup_printf(_("Connected to remote machine: %s (%s). Click to toggle SSH terminal (Ctrl+T)"),
                                         act_rm->name ? act_rm->name : act_rm->host, act_rm->host);
            gtk_widget_set_tooltip_text(t->term_btn, tip);
            g_free(tip);
        } else {
            icon_button_set_content(t->term_btn, "utilities-terminal", _("Terminal"));
            gtk_widget_set_tooltip_text(t->term_btn, _("Show / Hide Terminal Panel (Ctrl+T)"));
        }
        if (term_vis) {
            gtk_widget_add_css_class(t->term_btn, "pill-active");
        } else {
            gtk_widget_remove_css_class(t->term_btn, "pill-active");
        }
    }
    if (t->help_btn != NULL) {
        /* Lit while the cheat sheet or the Help tab is open */
        gboolean help_vis = ((app->help_window != NULL) && gtk_widget_get_visible(app->help_window)) ||
                            help_guide_is_open(app);
        if (help_vis) {
            gtk_widget_add_css_class(t->help_btn, "pill-active");
        } else {
            gtk_widget_remove_css_class(t->help_btn, "pill-active");
        }
    }

    /* Explorer panel header (VS Code style): view title + current folder section */
    if (sidebar_header_update(app)) {
        git_tab_update(app, app->base_dir);
    }

    app_emit(app, APP_EVENT_STATUS_CHANGED);
    app_emit(app, APP_EVENT_LAYOUT_CHANGED);
}

GtkWidget* topbar_new(AppData* app, GtkWidget* ws_minimap, GtkWidget* layouts_btn, GtkWidget* prefs_btn) {
    struct TopBar* t = bar_get(app);

    t->path_label = gtk_label_new(NULL);
    gtk_widget_set_visible(t->path_label, FALSE);

    t->breadcrumb_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_valign(t->breadcrumb_box, GTK_ALIGN_CENTER);

    t->files_btn = pill_new("folder", _("Explorer"), _("Show / Hide Explorer Panel"));
    g_signal_connect(t->files_btn, "clicked", G_CALLBACK(on_files_pill_clicked), app);
    t->preview_btn = pill_new("document-print-preview", _("Preview"), _("Show / Hide Preview Panel"));
    g_signal_connect(t->preview_btn, "clicked", G_CALLBACK(on_preview_pill_clicked), app);
    t->term_btn = pill_new("utilities-terminal", _("Terminal"), _("Show / Hide Terminal Panel"));
    g_signal_connect(t->term_btn, "clicked", G_CALLBACK(on_term_pill_clicked), app);
    t->help_btn = pill_new("dialog-question", _("Help"),
                           _("User Guide (F1) — Right-Click: Keyboard Shortcuts (?)"));
    g_signal_connect(t->help_btn, "clicked", G_CALLBACK(on_help_pill_clicked), app);
    GtkGesture* help_right = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(help_right), GDK_BUTTON_SECONDARY);
    g_signal_connect(help_right, "pressed", G_CALLBACK(on_help_pill_right_clicked), app);
    gtk_widget_add_controller(t->help_btn, GTK_EVENT_CONTROLLER(help_right));

    /* Pushes the rest of the row to the right */
    GtkWidget* spacer = gtk_label_new(" ");
    gtk_widget_set_halign(spacer, GTK_ALIGN_START);
    gtk_widget_set_hexpand(spacer, TRUE);

    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_name(row, "statusbar");
    gtk_box_append(GTK_BOX(row), t->path_label);
    gtk_box_append(GTK_BOX(row), history_buttons_new(app));
    gtk_box_append(GTK_BOX(row), t->breadcrumb_box);
    gtk_box_append(GTK_BOX(row), spacer);
    gtk_box_append(GTK_BOX(row), create_topbar_notice(app));
    gtk_box_append(GTK_BOX(row), mode_button_new(app));
    gtk_box_append(GTK_BOX(row), ws_minimap);
    gtk_box_append(GTK_BOX(row), layouts_btn);
    gtk_box_append(GTK_BOX(row), prefs_btn);
    gtk_box_append(GTK_BOX(row), separator_new());
    gtk_box_append(GTK_BOX(row), t->files_btn);
    gtk_box_append(GTK_BOX(row), t->preview_btn);
    gtk_box_append(GTK_BOX(row), t->term_btn);
    gtk_box_append(GTK_BOX(row), separator_new());
    gtk_box_append(GTK_BOX(row), t->help_btn);
    return row;
}

GtkWidget* topbar_split_button_new(AppData* app) {
    struct TopBar* t = bar_get(app);
    t->split_btn = icon_button_new("view-dual", NULL);
    gtk_widget_set_name(t->split_btn, "tab-split-btn");
    gtk_button_set_has_frame(GTK_BUTTON(t->split_btn), FALSE);
    gtk_widget_set_focusable(t->split_btn, FALSE);
    gtk_widget_add_css_class(t->split_btn, "tab-action-btn");
    gtk_widget_set_tooltip_text(t->split_btn, _("Move Editor Tabs to a Dedicated Panel"));
    g_signal_connect(t->split_btn, "clicked", G_CALLBACK(on_split_btn_clicked), app);
    return t->split_btn;
}

void topbar_free(AppData* app) {
    g_clear_pointer(&app->topbar, g_free);
}
