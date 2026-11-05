/* Panel status bars: the explorer bar (Git branch, app_notice() messages,
 * shown in the top bar while the explorer is hidden) and the preview bar
 * (name, size, type and encoding of the previewed entry). Editor tabs have
 * their own bar (editor_modes). */
#include "status_bar.h"
#include "app_events.h"
#include "file_list.h"
#include "file_types.h"
#include "git_utils.h"

/* How long an app_notice() message stays */
#define NOTICE_SECONDS 8

static guint notice_timeout_id;
static guint notice_shown_serial;

static gboolean on_notice_timeout(gpointer data) {
    notice_timeout_id = 0;
    app_notice((AppData*)data, NULL, FALSE);
    return G_SOURCE_REMOVE;
}

static void on_undo_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    undo_last_file_move((AppData*)data);
}

static void set_notice(GtkWidget* label, GtkWidget* undo, const gchar* text, gboolean with_undo) {
    if (label == NULL) {
        return;
    }
    gtk_label_set_text(GTK_LABEL(label), text != NULL ? text : "");
    gtk_widget_set_visible(label, text != NULL);
    gtk_widget_set_visible(undo, text != NULL && with_undo);
}

/* The app_notice() message and its Undo button, in the explorer bar or, while
 * the explorer is hidden, in the top bar; a new message restarts the timeout */
static void update_notice(AppData* app) {
    gboolean shown = (app->status_notice != NULL);
    gboolean in_explorer = (app->left_vbox == NULL) || gtk_widget_get_visible(app->left_vbox);
    set_notice(app->status_notice_label, app->status_undo_btn,
               in_explorer ? app->status_notice : NULL, app->status_notice_undo);
    set_notice(app->topbar_notice_label, app->topbar_undo_btn,
               in_explorer ? NULL : app->status_notice, app->status_notice_undo);
    if (app->topbar_notice != NULL) {
        gtk_widget_set_visible(app->topbar_notice, shown && !in_explorer);
    }
    if (notice_shown_serial != app->status_notice_serial) {
        notice_shown_serial = app->status_notice_serial;
        if (notice_timeout_id != 0) {
            g_source_remove(notice_timeout_id);
            notice_timeout_id = 0;
        }
        if (shown) {
            notice_timeout_id = g_timeout_add_seconds(NOTICE_SECONDS, on_notice_timeout, app);
        }
    }
}

static GtkWidget* undo_button_new(AppData* app) {
    GtkWidget* undo = gtk_button_new_with_mnemonic("_Undo");
    gtk_button_set_has_frame(GTK_BUTTON(undo), FALSE);
    gtk_widget_set_focusable(undo, FALSE);
    gtk_widget_set_tooltip_text(undo, "Undo the Last Trash or Rename (u)");
    gtk_widget_set_visible(undo, FALSE);
    g_signal_connect(undo, "clicked", G_CALLBACK(on_undo_clicked), app);
    return undo;
}

static GtkWidget* git_branches_popover = NULL;

typedef struct {
    AppData* app;
    gchar* branch_name;
} BranchSwitchData;

static gboolean do_switch_branch_idle(gpointer user_data) {
    BranchSwitchData* bd = (BranchSwitchData*)user_data;
    AppData* app = bd->app;
    gchar* err_msg = NULL;

    gboolean ok = git_checkout_branch(app->base_dir, bd->branch_name, &err_msg);
    if (ok) {
        gchar* msg = g_strdup_printf("Switched to branch “%s”", bd->branch_name);
        app_notice(app, msg, FALSE);
        g_free(msg);
        reload_directory(app, app->base_dir);
        app_emit(app, APP_EVENT_GIT_STATUS_CHANGED);
        app_emit(app, APP_EVENT_STATUS_CHANGED);
    } else {
        gchar* msg = g_strdup_printf("Could not switch to branch “%s”: %s",
                                     bd->branch_name, err_msg != NULL ? err_msg : "unknown error");
        app_notice(app, msg, FALSE);
        g_free(msg);
        g_free(err_msg);
    }

    g_free(bd->branch_name);
    g_free(bd);
    return G_SOURCE_REMOVE;
}

static void on_branch_item_clicked(GtkButton* btn, gpointer data) {
    GtkWidget* popover = GTK_WIDGET(data);
    AppData* app = (AppData*)g_object_get_data(G_OBJECT(btn), "app");
    const gchar* branch_name = (const gchar*)g_object_get_data(G_OBJECT(btn), "branch-name");
    gboolean is_head = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "is-head"));

    gtk_popover_popdown(GTK_POPOVER(popover));

    if (is_head || app == NULL || branch_name == NULL) {
        return;
    }

    BranchSwitchData* bd = g_new0(BranchSwitchData, 1);
    bd->app = app;
    bd->branch_name = g_strdup(branch_name);
    g_idle_add(do_switch_branch_idle, bd);
}

static gboolean unparent_idle(gpointer data) {
    gtk_widget_unparent(GTK_WIDGET(data));
    g_object_unref(data);
    return G_SOURCE_REMOVE;
}

static void on_branches_popover_closed(GtkPopover* popover, gpointer data) {
    (void)data;
    git_branches_popover = NULL;
    g_idle_add(unparent_idle, g_object_ref(popover));
}

static gboolean on_branches_popover_key_pressed(GtkEventControllerKey* controller, guint keyval,
                                                guint keycode, GdkModifierType state, gpointer data) {
    (void)keycode;
    GtkWidget* popover = GTK_WIDGET(data);
    GdkModifierType mods = state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK);
    if ((mods & GDK_CONTROL_MASK) && (keyval == GDK_KEY_q || keyval == GDK_KEY_Q)) {
        AppData* app = (AppData*)g_object_get_data(G_OBJECT(popover), "app");
        gtk_popover_popdown(GTK_POPOVER(popover));
        if (app != NULL && app->window != NULL) {
            return gtk_event_controller_key_forward(controller, GTK_WIDGET(app->window));
        }
    }
    return FALSE;
}

static void show_git_branches_popover(AppData* app) {
    if (app == NULL || app->status_branch == NULL || app->base_dir == NULL) {
        return;
    }
    if (git_branches_popover != NULL) {
        gtk_popover_popdown(GTK_POPOVER(git_branches_popover));
        return;
    }

    GPtrArray* branches = git_list_branches(app->base_dir);
    if (branches == NULL) {
        return;
    }

    GtkWidget* popover = gtk_popover_new();
    git_branches_popover = popover;
    gtk_widget_set_parent(popover, app->status_branch);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_TOP);
    gtk_widget_set_halign(popover, GTK_ALIGN_START);
    gtk_widget_set_size_request(popover, 260, -1);
    g_object_set_data(G_OBJECT(popover), "app", app);

    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_branches_popover_key_pressed), popover);
    gtk_widget_add_controller(popover, key_ctrl);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(box, 4);
    gtk_widget_set_margin_end(box, 4);
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);

    GtkWidget* title = gtk_label_new("Git Branches");
    gtk_widget_add_css_class(title, "dim-label");
    gtk_widget_set_margin_start(title, 6);
    gtk_widget_set_margin_top(title, 2);
    gtk_widget_set_margin_bottom(title, 4);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_box_append(GTK_BOX(box), title);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 280);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), TRUE);

    GtkWidget* list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gboolean has_remote_sep = FALSE;

    for (guint i = 0; i < branches->len; i++) {
        const GitBranchInfo* b = g_ptr_array_index(branches, i);
        if (b->is_remote && !has_remote_sep) {
            has_remote_sep = TRUE;
            GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
            gtk_widget_set_margin_top(sep, 4);
            gtk_widget_set_margin_bottom(sep, 4);
            gtk_box_append(GTK_BOX(list_box), sep);

            GtkWidget* rem_title = gtk_label_new("Remote Branches");
            gtk_widget_add_css_class(rem_title, "dim-label");
            gtk_widget_set_margin_start(rem_title, 6);
            gtk_widget_set_margin_bottom(rem_title, 2);
            gtk_label_set_xalign(GTK_LABEL(rem_title), 0.0f);
            gtk_box_append(GTK_BOX(list_box), rem_title);
        }

        GtkWidget* btn = gtk_button_new();
        gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
        gtk_widget_add_css_class(btn, "flat");

        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

        GtkWidget* mark = gtk_label_new("●");
        gtk_widget_set_opacity(mark, b->is_head ? 1.0 : 0.0);
        gtk_box_append(GTK_BOX(row), mark);

        GtkWidget* icon_label = gtk_label_new("⎇");
        if (b->is_remote) {
            gtk_widget_add_css_class(icon_label, "dim-label");
        }
        gtk_box_append(GTK_BOX(row), icon_label);

        GtkWidget* name_label = gtk_label_new(b->name);
        gtk_label_set_xalign(GTK_LABEL(name_label), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(name_label), PANGO_ELLIPSIZE_MIDDLE);
        gtk_widget_set_hexpand(name_label, TRUE);
        if (b->is_remote) {
            gtk_widget_add_css_class(name_label, "dim-label");
        }
        gtk_box_append(GTK_BOX(row), name_label);

        if (b->is_head) {
            gtk_widget_add_css_class(btn, "menu-item-current");
            GtkWidget* cur = gtk_label_new("(current)");
            gtk_widget_add_css_class(cur, "dim-label");
            gtk_box_append(GTK_BOX(row), cur);
            gtk_widget_set_tooltip_text(btn, "Currently checked out branch");
        } else if (b->is_remote) {
            gchar* tip = g_strdup_printf("Switch to remote tracking branch %s", b->name);
            gtk_widget_set_tooltip_text(btn, tip);
            g_free(tip);
        } else {
            gchar* tip = g_strdup_printf("Switch to branch %s", b->name);
            gtk_widget_set_tooltip_text(btn, tip);
            g_free(tip);
        }

        gtk_button_set_child(GTK_BUTTON(btn), row);

        g_object_set_data(G_OBJECT(btn), "app", app);
        g_object_set_data_full(G_OBJECT(btn), "branch-name", g_strdup(b->name), g_free);
        g_object_set_data(G_OBJECT(btn), "is-head", GINT_TO_POINTER(b->is_head));
        g_signal_connect(btn, "clicked", G_CALLBACK(on_branch_item_clicked), popover);

        gtk_box_append(GTK_BOX(list_box), btn);
    }

    g_ptr_array_unref(branches);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list_box);
    gtk_box_append(GTK_BOX(box), scroll);

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect(popover, "closed", G_CALLBACK(on_branches_popover_closed), NULL);
    gtk_popover_popup(GTK_POPOVER(popover));
}

static void on_status_branch_clicked(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)gesture;
    (void)n_press;
    (void)x;
    (void)y;
    AppData* app = (AppData*)data;
    if (app != NULL) {
        show_git_branches_popover(app);
    }
}

static GtkWidget* status_item_new(void) {
    GtkWidget* label = gtk_label_new("");
    gtk_widget_add_css_class(label, "status-item");
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_single_line_mode(GTK_LABEL(label), TRUE);
    gtk_widget_set_visible(label, FALSE);
    return label;
}

GtkWidget* create_explorer_bar(AppData* app) {
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(bar, "panel-statusbar");

    GtkWidget* status_branch = status_item_new();
    gtk_widget_add_css_class(status_branch, "status-branch");
    gtk_widget_set_tooltip_text(status_branch, "Git Branch of the Current Directory — Click to View Branches");
    gtk_widget_set_cursor_from_name(status_branch, "pointer");
    GtkGesture* branch_click = gtk_gesture_click_new();
    g_signal_connect(branch_click, "pressed", G_CALLBACK(on_status_branch_clicked), app);
    gtk_widget_add_controller(status_branch, GTK_EVENT_CONTROLLER(branch_click));
    gtk_box_append(GTK_BOX(bar), status_branch);

    GtkWidget* status_notice = status_item_new();
    gtk_widget_set_hexpand(status_notice, TRUE);
    gtk_widget_set_halign(status_notice, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(bar), status_notice);

    GtkWidget* status_undo = undo_button_new(app);
    gtk_widget_add_css_class(status_undo, "status-item");
    gtk_box_append(GTK_BOX(bar), status_undo);

    app->status_branch = status_branch;
    app->status_notice_label = status_notice;
    app->status_undo_btn = status_undo;
    return bar;
}

GtkWidget* create_topbar_notice(AppData* app) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(box, FALSE);

    GtkWidget* label = gtk_label_new("");
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_single_line_mode(GTK_LABEL(label), TRUE);
    gtk_widget_add_css_class(label, "topbar-notice");
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget* undo = undo_button_new(app);
    gtk_widget_add_css_class(undo, "status-pill");
    gtk_box_append(GTK_BOX(box), undo);

    app->topbar_notice = box;
    app->topbar_notice_label = label;
    app->topbar_undo_btn = undo;
    return box;
}

/* Preview bar: name of the previewed entry on the left; size, type and encoding on the right */
GtkWidget* create_preview_bar(AppData* app) {
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(bar, "panel-statusbar");

    GtkWidget* status_selection = status_item_new();
    gtk_widget_add_css_class(status_selection, "status-dim");
    gtk_box_append(GTK_BOX(bar), status_selection);

    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(bar), spacer);

    GtkWidget* status_size = status_item_new();
    gtk_box_append(GTK_BOX(bar), status_size);

    GtkWidget* status_type = status_item_new();
    gtk_box_append(GTK_BOX(bar), status_type);

    GtkWidget* status_encoding = status_item_new();
    gtk_box_append(GTK_BOX(bar), status_encoding);

    app->preview_bar = bar;
    app->status_selection = status_selection;
    app->status_size = status_size;
    app->status_type = status_type;
    app->status_encoding = status_encoding;
    return bar;
}

/* Human-readable type / language of an explorer entry, like VS Code's language mode */
static gchar* status_type_label(const gchar* name, const gchar* type, FileKind kind,
                                const gchar* content_type) {
    if (g_strcmp0(type, "dir") == 0) {
        return g_strdup("Folder");
    }
    if (g_strcmp0(type, "bookmark") == 0) {
        return g_strdup("Bookmark");
    }
    if (g_strcmp0(type, "bm_folder") == 0) {
        return g_strdup("Bookmark Folder");
    }
    if (g_strcmp0(type, "notebook") == 0) {
        return g_strdup("Notebook");
    }
    if (g_strcmp0(type, "remote") == 0) {
        return g_strdup("Remote Machine");
    }
    if (g_strcmp0(type, "remote_folder") == 0) {
        return g_strdup("Remote Folder");
    }
    if (g_strcmp0(type, "trash") == 0) {
        return g_strdup("Trash");
    }
    if (g_strcmp0(type, "trash_note") == 0) {
        return g_strdup("Trashed Note");
    }
    if (g_strcmp0(type, "note") == 0 || g_strcmp0(type, "buffer") == 0) {
        return g_strdup("Note");
    }
    const gchar* label = file_kind_label(kind);
    if (label != NULL) {
        return g_strdup(label);
    }
#ifdef HAVE_GSV
    /* The content type of the previewed text also finds scripts without extension (#!) */
    GtkSourceLanguageManager* lm = gtk_source_language_manager_get_default();
    GtkSourceLanguage* lang = gtk_source_language_manager_guess_language(lm, name, content_type);
    if (lang != NULL) {
        return g_strdup(gtk_source_language_get_name(lang));
    }
#else
    (void)content_type;
#endif
    const gchar* ext = file_extension(name);
    if (ext[0] != '\0') {
        return g_ascii_strup(ext, -1);
    }
    return g_strdup("Plain Text");
}

/* Refresh the explorer bar (branch, message) and the preview bar (selected entry) */
void update_status_bar(AppData* app) {
    if (app == NULL || app->status_selection == NULL) {
        return;
    }

    /* The branch only makes sense for the files view (like the Git Commits tab) */
    gboolean files_view = (app->sidebar_view == SIDEBAR_VIEW_FILES && !app->show_bookmarks);
    gchar* branch = files_view ? git_read_branch(app->base_dir) : NULL;
    if (branch != NULL) {
        gchar* root = find_git_root(app->base_dir);
        gboolean in_scanned_repo = (root != NULL && g_strcmp0(root, app->git_status_root) == 0);
        g_free(root);

        GString* text = g_string_new(NULL);
        g_string_append_printf(text, "⎇ %s", branch);
        if (in_scanned_repo && app->git_has_changes) {
            g_string_append(text, " *");
        }
        if (in_scanned_repo && app->git_has_upstream) {
            if (app->git_ahead > 0) {
                g_string_append_printf(text, " ↑%u", app->git_ahead);
            }
            if (app->git_behind > 0) {
                g_string_append_printf(text, " ↓%u", app->git_behind);
            }
        }
        gtk_label_set_text(GTK_LABEL(app->status_branch), text->str);
        g_string_free(text, TRUE);

        GString* tip = g_string_new(NULL);
        g_string_append_printf(tip, "Git Branch: %s", branch);
        if (in_scanned_repo && app->git_has_changes) {
            g_string_append(tip, " (uncommitted modifications)");
        }
        if (in_scanned_repo && app->git_has_upstream) {
            if (app->git_ahead > 0 && app->git_behind > 0) {
                g_string_append_printf(tip, "\n%u commit%s ahead, %u commit%s behind upstream",
                                       app->git_ahead, app->git_ahead == 1 ? "" : "s",
                                       app->git_behind, app->git_behind == 1 ? "" : "s");
            } else if (app->git_ahead > 0) {
                g_string_append_printf(tip, "\n%u commit%s ahead of upstream",
                                       app->git_ahead, app->git_ahead == 1 ? "" : "s");
            } else if (app->git_behind > 0) {
                g_string_append_printf(tip, "\n%u commit%s behind upstream",
                                       app->git_behind, app->git_behind == 1 ? "" : "s");
            } else {
                g_string_append(tip, "\nUp to date with upstream");
            }
        }
        g_string_append(tip, "\nClick to view branches");
        gtk_widget_set_tooltip_text(app->status_branch, tip->str);
        g_string_free(tip, TRUE);
    }
    gtk_widget_set_visible(app->status_branch, branch != NULL);
    g_free(branch);

    gchar* name = NULL;
    gchar* type = NULL;
    gchar* full = NULL;
    ExplorerRow* row = (app->tree != NULL) ? explorer_tree_get_selected(app->tree) : NULL;
    if (row != NULL) {
        name = g_strdup(row->name);
        type = g_strdup(row->type);
        full = g_strdup(row->path);
    }

    gboolean has_sel = (name != NULL && type != NULL && g_strcmp0(type, "-") != 0);
    gboolean has_size = FALSE;
    gboolean has_encoding = FALSE;
    if (has_sel) {
        gtk_label_set_text(GTK_LABEL(app->status_selection), name);
        gtk_widget_set_tooltip_text(app->status_selection, full);

        /* The previewed file's kind comes from its content; others from their name */
        gboolean previewed = (full != NULL && g_strcmp0(full, app->viewed_path) == 0);
        FileKind kind = previewed ? app->viewed_kind : file_kind_from_name(name);
        gchar* type_label = status_type_label(name, type, kind,
                                              previewed ? app->viewed_content_type : NULL);
        gtk_label_set_text(GTK_LABEL(app->status_type), type_label);
        g_free(type_label);
        /* Encoding of a previewed text file, as detected */
        has_encoding = (previewed && app->viewed_encoding != NULL);
        if (has_encoding) {
            gtk_label_set_text(GTK_LABEL(app->status_encoding), app->viewed_encoding);
        }

        gboolean is_file = (g_strcmp0(type, "dir") != 0 && g_strcmp0(type, "bookmark") != 0 &&
                            g_strcmp0(type, "bm_folder") != 0 && g_strcmp0(type, "notebook") != 0 &&
                            g_strcmp0(type, "trash") != 0 && g_strcmp0(type, "remote_folder") != 0);
        GStatBuf st;
        if (is_file && full != NULL && g_stat(full, &st) == 0) {
            gchar* size_str = g_format_size((guint64)st.st_size);
            gtk_label_set_text(GTK_LABEL(app->status_size), size_str);
            g_free(size_str);
            has_size = TRUE;
        }
    }
    gtk_widget_set_visible(app->status_selection, has_sel);
    gtk_widget_set_visible(app->status_type, has_sel);
    gtk_widget_set_visible(app->status_size, has_size);
    gtk_widget_set_visible(app->status_encoding, has_encoding);
    /* Only under the preview page: editor, Git and Help tabs have their own bars */
    if (app->preview_bar != NULL && app->notebook != NULL) {
        gtk_widget_set_visible(app->preview_bar,
                               gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook)) == 0);
    }

    update_notice(app);

    g_free(name);
    g_free(type);
    g_free(full);
}
