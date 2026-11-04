/* Explorer panel header: view switcher buttons (files, bookmarks, notes, remote) and per-view action toolbars. */
#include "sidebar.h"
#include "icons.h"
#include "app_events.h"
#include "bookmarks.h"
#include "file_list.h"
#include "notes.h"
#include "remote.h"

static void on_sidebar_files_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    AppData* app = (AppData*)data;
    if (app == NULL) {
        return;
    }
    if (app->sidebar_view != SIDEBAR_VIEW_FILES || app->show_bookmarks) {
        app->sidebar_view = SIDEBAR_VIEW_FILES;
        app->show_bookmarks = FALSE;
        app_emit(app, APP_EVENT_CHROME_CHANGED);
        app_emit(app, APP_EVENT_FILES_CHANGED);
    } else if (app->tree_view != NULL) {
        gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
    }
}

static void on_sidebar_bm_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    AppData* app = (AppData*)data;
    if (app == NULL) {
        return;
    }
    if (app->sidebar_view != SIDEBAR_VIEW_BOOKMARKS) {
        toggle_bookmarks_view(app);
    } else if (app->tree_view != NULL) {
        gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
    }
}

static void on_sidebar_notes_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    AppData* app = (AppData*)data;
    if (app == NULL) {
        return;
    }
    if (app->sidebar_view != SIDEBAR_VIEW_NOTES) {
        toggle_notes_view(app);
    } else if (app->tree_view != NULL) {
        gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
    }
}

static void on_sidebar_remote_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    AppData* app = (AppData*)data;
    if (app == NULL) {
        return;
    }
    if (app->sidebar_view != SIDEBAR_VIEW_REMOTE) {
        toggle_remote_view(app);
    } else if (app->tree_view != NULL) {
        gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
    }
}

static void on_sidebar_games_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    AppData* app = (AppData*)data;
    if (app == NULL) {
        return;
    }
    if (app->sidebar_view != SIDEBAR_VIEW_GAMES) {
        toggle_games_view(app);
    } else if (app->tree_view != NULL) {
        gtk_widget_grab_focus(GTK_WIDGET(app->tree_view));
    }
}

static void on_sidebar_games_play_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    AppData* app = (AppData*)data;
    if (app != NULL) {
        activate_selected_entry(app);
    }
}

static void on_sidebar_remote_add_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    remote_add_dialog((AppData*)data);
}

static void on_sidebar_remote_new_folder_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    remote_new_folder_dialog((AppData*)data);
}

static void on_sidebar_remote_connect_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    remote_toggle_connect_selected((AppData*)data);
}

static void on_sidebar_remote_term_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    remote_open_terminal_selected((AppData*)data);
}

static void on_sidebar_remote_edit_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    remote_edit_dialog((AppData*)data);
}

static void on_sidebar_remote_delete_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    remote_delete_selected((AppData*)data);
}

static void on_sidebar_remote_refresh_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    remote_rescan((AppData*)data);
}

static void on_sidebar_files_new_file_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    create_new_file_in_tree((AppData*)data);
}

static void on_sidebar_files_new_folder_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    create_new_folder_in_tree((AppData*)data);
}

static void on_sidebar_files_hidden_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    toggle_hidden_files((AppData*)data);
}

static void on_sidebar_files_collapse_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    toggle_collapse_expand_files((AppData*)data);
}

static void on_sidebar_bm_add_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    bookmarks_add((AppData*)data);
}

static void on_sidebar_bm_new_folder_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    bookmarks_new_folder((AppData*)data);
}

static void on_sidebar_bm_edit_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    bookmarks_edit_selected((AppData*)data);
}

static void on_sidebar_bm_delete_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    bookmarks_delete_selected((AppData*)data);
}

static void on_sidebar_bm_collapse_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    bookmarks_toggle_collapse_expand((AppData*)data);
}

static void on_sidebar_notes_new_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    notes_new_note((AppData*)data);
}

static void on_sidebar_notes_new_notebook_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    notes_new_notebook((AppData*)data);
}

static void on_sidebar_notes_edit_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    notes_edit_selected((AppData*)data);
}

static void on_sidebar_notes_delete_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    notes_delete_selected((AppData*)data);
}

static void on_sidebar_remote_collapse_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    remote_toggle_collapse_expand((AppData*)data);
}

static void on_sidebar_notes_collapse_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    notes_toggle_collapse_expand((AppData*)data);
}

/* Collapse / expand toggles of the four toolbars. Each shows what a click
 * does: pan-up "Collapse All" while something is expanded, else pan-down
 * "Expand All". Refreshed (once per idle) whenever the explorer rows change. */
enum { CB_FILES, CB_BOOKMARKS, CB_NOTES, CB_REMOTE, CB_COUNT };

typedef struct {
    AppData* app;
    GtkWidget* btn[CB_COUNT];
    gboolean collapse[CB_COUNT]; /* state shown, valid once shown is TRUE */
    gboolean shown;
    GListModel* model;           /* watched list view model (ref) */
    gulong items_handler;
    guint idle_id;
} CollapseButtons;

static const gchar* const CB_NOUNS[CB_COUNT] = {
    "Folders", "Bookmark Folders", "Notebooks", "Folders"
};

static gboolean cb_any_expanded(AppData* app, gint which) {
    switch (which) {
    case CB_FILES:     return files_any_expanded(app);
    case CB_BOOKMARKS: return bookmarks_any_expanded(app);
    case CB_NOTES:     return notes_any_expanded(app);
    default:           return remote_any_expanded(app);
    }
}

static void cb_update(CollapseButtons* cb) {
    for (gint i = 0; i < CB_COUNT; i++) {
        gboolean collapse = cb_any_expanded(cb->app, i);
        if (cb->shown && cb->collapse[i] == collapse) {
            continue;
        }
        cb->collapse[i] = collapse;
        icon_button_set_content(cb->btn[i], collapse ? "pan-up" : "pan-down", NULL);
        gchar* tip = g_strdup_printf("%s All %s", collapse ? "Collapse" : "Expand", CB_NOUNS[i]);
        gtk_widget_set_tooltip_text(cb->btn[i], tip);
        g_free(tip);
    }
    cb->shown = TRUE;
}

static gboolean cb_update_idle(gpointer data) {
    CollapseButtons* cb = (CollapseButtons*)data;
    cb->idle_id = 0;
    cb_update(cb);
    return G_SOURCE_REMOVE;
}

static void cb_on_items_changed(GListModel* model, guint pos, guint removed, guint added, gpointer data) {
    (void)model; (void)pos; (void)removed; (void)added;
    CollapseButtons* cb = (CollapseButtons*)data;
    if (cb->idle_id == 0) {
        cb->idle_id = g_idle_add(cb_update_idle, cb);
    }
}

static void cb_unwatch_model(CollapseButtons* cb) {
    if (cb->model != NULL) {
        g_signal_handler_disconnect(cb->model, cb->items_handler);
        g_clear_object(&cb->model);
        cb->items_handler = 0;
    }
}

/* The list view gets a new model when the workspace changes (data: the header) */
static void cb_on_model_changed(GObject* view, GParamSpec* pspec, gpointer data) {
    (void)pspec;
    CollapseButtons* cb = g_object_get_data(G_OBJECT(data), "collapse-buttons");
    if (cb == NULL) {
        return;
    }
    cb_unwatch_model(cb);
    GtkSelectionModel* model = gtk_list_view_get_model(GTK_LIST_VIEW(view));
    if (model != NULL) { /* NULL while the view is destroyed */
        cb->model = G_LIST_MODEL(g_object_ref(model));
        cb->items_handler = g_signal_connect(cb->model, "items-changed",
                                             G_CALLBACK(cb_on_items_changed), cb);
        cb_on_items_changed(NULL, 0, 0, 0, cb);
    }
}

static void cb_free(gpointer data) {
    CollapseButtons* cb = (CollapseButtons*)data;
    cb_unwatch_model(cb);
    if (cb->idle_id != 0) {
        g_source_remove(cb->idle_id);
    }
    g_free(cb);
}

static GtkWidget* collapse_button_new(CollapseButtons* cb, gint which, GCallback on_clicked) {
    GtkWidget* btn = icon_button_new("pan-up", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(btn), FALSE);
    gtk_widget_set_focusable(btn, FALSE);
    gtk_widget_add_css_class(btn, "sidebar-icon-btn");
    g_signal_connect(btn, "clicked", on_clicked, cb->app);
    cb->btn[which] = btn;
    return btn;
}

/* VS Code-style explorer panel header: "EXPLORER" title + mini view icons + action bars */
GtkWidget* create_sidebar_header(AppData* app) {
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(header, "sidebar-header");
    CollapseButtons* cb = g_new0(CollapseButtons, 1);
    cb->app = app;

    GtkWidget* title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_name(title_row, "sidebar-title-row");

    GtkWidget* title = gtk_label_new("");
    gtk_widget_set_name(title, "sidebar-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(title, TRUE);
    gtk_box_append(GTK_BOX(title_row), title);

    GtkWidget* files_btn = icon_button_new("folder", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(files_btn), FALSE);
    gtk_widget_set_focusable(files_btn, FALSE);
    gtk_widget_add_css_class(files_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(files_btn, "Explorer (Ctrl+A)");
    g_signal_connect(files_btn, "clicked", G_CALLBACK(on_sidebar_files_clicked), app);
    gtk_box_append(GTK_BOX(title_row), files_btn);

    GtkWidget* bm_btn = icon_button_new("user-bookmarks", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(bm_btn), FALSE);
    gtk_widget_set_focusable(bm_btn, FALSE);
    gtk_widget_add_css_class(bm_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(bm_btn, "Bookmarks (Ctrl+A)");
    g_signal_connect(bm_btn, "clicked", G_CALLBACK(on_sidebar_bm_clicked), app);
    gtk_box_append(GTK_BOX(title_row), bm_btn);

    GtkWidget* buf_btn = icon_button_new("text-x-generic", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(buf_btn), FALSE);
    gtk_widget_set_focusable(buf_btn, FALSE);
    gtk_widget_add_css_class(buf_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(buf_btn, "Notes (Ctrl+U)");
    g_signal_connect(buf_btn, "clicked", G_CALLBACK(on_sidebar_notes_clicked), app);
    gtk_box_append(GTK_BOX(title_row), buf_btn);

    GtkWidget* remote_btn = icon_button_new("network-server", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(remote_btn), FALSE);
    gtk_widget_set_focusable(remote_btn, FALSE);
    gtk_widget_add_css_class(remote_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(remote_btn, "Remote (Ctrl+Shift+R)");
    g_signal_connect(remote_btn, "clicked", G_CALLBACK(on_sidebar_remote_clicked), app);
    gtk_box_append(GTK_BOX(title_row), remote_btn);

    GtkWidget* games_btn = icon_button_new("applications-games", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(games_btn), FALSE);
    gtk_widget_set_focusable(games_btn, FALSE);
    gtk_widget_add_css_class(games_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(games_btn, "Games (Game Mode)");
    g_signal_connect(games_btn, "clicked", G_CALLBACK(on_sidebar_games_clicked), app);
    gtk_box_append(GTK_BOX(title_row), games_btn);
    gtk_widget_set_visible(games_btn, app != NULL && app->game_mode_unlocked);

    if (app != NULL && app->sidebar_view == SIDEBAR_VIEW_GAMES) {
        gtk_widget_add_css_class(games_btn, "sidebar-icon-active");
    } else if (app != NULL && app->sidebar_view == SIDEBAR_VIEW_REMOTE) {
        gtk_widget_add_css_class(remote_btn, "sidebar-icon-active");
    } else if (app != NULL && app->sidebar_view == SIDEBAR_VIEW_NOTES) {
        gtk_widget_add_css_class(buf_btn, "sidebar-icon-active");
    } else if (app != NULL && (app->sidebar_view == SIDEBAR_VIEW_BOOKMARKS || app->show_bookmarks)) {
        gtk_widget_add_css_class(bm_btn, "sidebar-icon-active");
    } else {
        gtk_widget_add_css_class(files_btn, "sidebar-icon-active");
    }

    gtk_box_append(GTK_BOX(header), title_row);

    /* 1. Files Explorer Toolbar: mini action icons */
    GtkWidget* files_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_name(files_toolbar, "sidebar-files-toolbar");

    GtkWidget* section = gtk_label_new("");
    gtk_widget_set_name(section, "sidebar-section");
    gtk_label_set_xalign(GTK_LABEL(section), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(section), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(section, TRUE);
    gtk_widget_set_visible(section, FALSE);
    gtk_box_append(GTK_BOX(files_toolbar), section);

    GtkWidget* files_new_file_btn = icon_button_new("document-new", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(files_new_file_btn), FALSE);
    gtk_widget_set_focusable(files_new_file_btn, FALSE);
    gtk_widget_add_css_class(files_new_file_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(files_new_file_btn, "New File (a)");
    g_signal_connect(files_new_file_btn, "clicked", G_CALLBACK(on_sidebar_files_new_file_clicked), app);
    gtk_box_append(GTK_BOX(files_toolbar), files_new_file_btn);

    GtkWidget* files_new_folder_btn = icon_button_new("folder-new", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(files_new_folder_btn), FALSE);
    gtk_widget_set_focusable(files_new_folder_btn, FALSE);
    gtk_widget_add_css_class(files_new_folder_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(files_new_folder_btn, "New Folder (Shift+A)");
    g_signal_connect(files_new_folder_btn, "clicked", G_CALLBACK(on_sidebar_files_new_folder_clicked), app);
    gtk_box_append(GTK_BOX(files_toolbar), files_new_folder_btn);

    GtkWidget* files_hidden_btn = icon_button_new("view-reveal", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(files_hidden_btn), FALSE);
    gtk_widget_set_focusable(files_hidden_btn, FALSE);
    gtk_widget_add_css_class(files_hidden_btn, "sidebar-icon-btn");
    if (app != NULL && app->show_hidden) {
        gtk_widget_add_css_class(files_hidden_btn, "sidebar-icon-active");
    }
    gtk_widget_set_tooltip_text(files_hidden_btn, "Toggle Hidden Files (i / I)");
    g_signal_connect(files_hidden_btn, "clicked", G_CALLBACK(on_sidebar_files_hidden_clicked), app);
    gtk_box_append(GTK_BOX(files_toolbar), files_hidden_btn);

    GtkWidget* files_collapse_btn = collapse_button_new(cb, CB_FILES, G_CALLBACK(on_sidebar_files_collapse_clicked));
    gtk_box_append(GTK_BOX(files_toolbar), files_collapse_btn);

    gtk_widget_set_visible(files_toolbar, (app == NULL || (app->sidebar_view == SIDEBAR_VIEW_FILES && !app->show_bookmarks)));
    gtk_box_append(GTK_BOX(header), files_toolbar);

    /* 2. Bookmarks Toolbar: mini action icons */
    GtkWidget* bm_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_name(bm_toolbar, "sidebar-bm-toolbar");

    GtkWidget* bm_section = gtk_label_new("");
    gtk_widget_add_css_class(bm_section, "sidebar-section-title");
    gtk_label_set_xalign(GTK_LABEL(bm_section), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(bm_section), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(bm_section, TRUE);
    gtk_widget_set_visible(bm_section, FALSE);
    gtk_box_append(GTK_BOX(bm_toolbar), bm_section);

    GtkWidget* bm_add_btn = icon_button_new("bookmark-new", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(bm_add_btn), FALSE);
    gtk_widget_set_focusable(bm_add_btn, FALSE);
    gtk_widget_add_css_class(bm_add_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(bm_add_btn, "Add Bookmark (a)");
    g_signal_connect(bm_add_btn, "clicked", G_CALLBACK(on_sidebar_bm_add_clicked), app);
    gtk_box_append(GTK_BOX(bm_toolbar), bm_add_btn);

    GtkWidget* bm_new_folder_btn = icon_button_new("folder-new", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(bm_new_folder_btn), FALSE);
    gtk_widget_set_focusable(bm_new_folder_btn, FALSE);
    gtk_widget_add_css_class(bm_new_folder_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(bm_new_folder_btn, "New Bookmark Folder (Shift+A)");
    g_signal_connect(bm_new_folder_btn, "clicked", G_CALLBACK(on_sidebar_bm_new_folder_clicked), app);
    gtk_box_append(GTK_BOX(bm_toolbar), bm_new_folder_btn);

    GtkWidget* bm_edit_btn = icon_button_new("document-edit", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(bm_edit_btn), FALSE);
    gtk_widget_set_focusable(bm_edit_btn, FALSE);
    gtk_widget_add_css_class(bm_edit_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(bm_edit_btn, "Edit Selected (F2 / r / e)");
    g_signal_connect(bm_edit_btn, "clicked", G_CALLBACK(on_sidebar_bm_edit_clicked), app);
    gtk_box_append(GTK_BOX(bm_toolbar), bm_edit_btn);

    GtkWidget* bm_delete_btn = icon_button_new("edit-delete", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(bm_delete_btn), FALSE);
    gtk_widget_set_focusable(bm_delete_btn, FALSE);
    gtk_widget_add_css_class(bm_delete_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(bm_delete_btn, "Delete Selected (d / Delete)");
    g_signal_connect(bm_delete_btn, "clicked", G_CALLBACK(on_sidebar_bm_delete_clicked), app);
    gtk_box_append(GTK_BOX(bm_toolbar), bm_delete_btn);

    GtkWidget* bm_collapse_btn = collapse_button_new(cb, CB_BOOKMARKS, G_CALLBACK(on_sidebar_bm_collapse_clicked));
    gtk_box_append(GTK_BOX(bm_toolbar), bm_collapse_btn);

    gtk_widget_set_visible(bm_toolbar, (app != NULL && (app->sidebar_view == SIDEBAR_VIEW_BOOKMARKS || app->show_bookmarks)));
    gtk_box_append(GTK_BOX(header), bm_toolbar);

    /* 3. Buffers Toolbar: mini action icons */
    GtkWidget* buf_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_name(buf_toolbar, "sidebar-buf-toolbar");

    GtkWidget* buf_section = gtk_label_new("");
    gtk_widget_add_css_class(buf_section, "sidebar-section-title");
    gtk_label_set_xalign(GTK_LABEL(buf_section), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(buf_section), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(buf_section, TRUE);
    gtk_widget_set_visible(buf_section, FALSE);
    gtk_box_append(GTK_BOX(buf_toolbar), buf_section);

    GtkWidget* new_note_btn = icon_button_new("document-new", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(new_note_btn), FALSE);
    gtk_widget_set_focusable(new_note_btn, FALSE);
    gtk_widget_add_css_class(new_note_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(new_note_btn, "New Note (a)");
    g_signal_connect(new_note_btn, "clicked", G_CALLBACK(on_sidebar_notes_new_clicked), app);
    gtk_box_append(GTK_BOX(buf_toolbar), new_note_btn);

    GtkWidget* new_nb_btn = icon_button_new("folder-new", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(new_nb_btn), FALSE);
    gtk_widget_set_focusable(new_nb_btn, FALSE);
    gtk_widget_add_css_class(new_nb_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(new_nb_btn, "New Notebook (Shift+A)");
    g_signal_connect(new_nb_btn, "clicked", G_CALLBACK(on_sidebar_notes_new_notebook_clicked), app);
    gtk_box_append(GTK_BOX(buf_toolbar), new_nb_btn);

    GtkWidget* edit_note_btn = icon_button_new("document-edit", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(edit_note_btn), FALSE);
    gtk_widget_set_focusable(edit_note_btn, FALSE);
    gtk_widget_add_css_class(edit_note_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(edit_note_btn, "Edit Selected (F2 / r / e)");
    g_signal_connect(edit_note_btn, "clicked", G_CALLBACK(on_sidebar_notes_edit_clicked), app);
    gtk_box_append(GTK_BOX(buf_toolbar), edit_note_btn);

    GtkWidget* del_note_btn = icon_button_new("user-trash", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(del_note_btn), FALSE);
    gtk_widget_set_focusable(del_note_btn, FALSE);
    gtk_widget_add_css_class(del_note_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(del_note_btn, "Move to Trash / Delete (d / Delete)");
    g_signal_connect(del_note_btn, "clicked", G_CALLBACK(on_sidebar_notes_delete_clicked), app);
    gtk_box_append(GTK_BOX(buf_toolbar), del_note_btn);

    GtkWidget* collapse_note_btn = collapse_button_new(cb, CB_NOTES, G_CALLBACK(on_sidebar_notes_collapse_clicked));
    gtk_box_append(GTK_BOX(buf_toolbar), collapse_note_btn);

    gtk_widget_set_visible(buf_toolbar, (app != NULL && app->sidebar_view == SIDEBAR_VIEW_NOTES));
    gtk_box_append(GTK_BOX(header), buf_toolbar);

    /* 4. Remote SSH Toolbar: mini action icons */
    GtkWidget* remote_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_name(remote_toolbar, "sidebar-remote-toolbar");

    GtkWidget* remote_section = gtk_label_new("");
    gtk_widget_add_css_class(remote_section, "sidebar-section-title");
    gtk_label_set_xalign(GTK_LABEL(remote_section), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(remote_section), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(remote_section, TRUE);
    gtk_widget_set_visible(remote_section, FALSE);
    gtk_box_append(GTK_BOX(remote_toolbar), remote_section);

    GtkWidget* remote_add_btn = icon_button_new("list-add", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(remote_add_btn), FALSE);
    gtk_widget_set_focusable(remote_add_btn, FALSE);
    gtk_widget_add_css_class(remote_add_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(remote_add_btn, "Add Remote Machine (a)");
    g_signal_connect(remote_add_btn, "clicked", G_CALLBACK(on_sidebar_remote_add_clicked), app);
    gtk_box_append(GTK_BOX(remote_toolbar), remote_add_btn);

    GtkWidget* remote_folder_btn = icon_button_new("folder-new", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(remote_folder_btn), FALSE);
    gtk_widget_set_focusable(remote_folder_btn, FALSE);
    gtk_widget_add_css_class(remote_folder_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(remote_folder_btn, "New Remote Folder (Shift+A)");
    g_signal_connect(remote_folder_btn, "clicked", G_CALLBACK(on_sidebar_remote_new_folder_clicked), app);
    gtk_box_append(GTK_BOX(remote_toolbar), remote_folder_btn);

    GtkWidget* remote_edit_btn = icon_button_new("document-edit", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(remote_edit_btn), FALSE);
    gtk_widget_set_focusable(remote_edit_btn, FALSE);
    gtk_widget_add_css_class(remote_edit_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(remote_edit_btn, "Edit Remote Machine (F2 / r / e)");
    g_signal_connect(remote_edit_btn, "clicked", G_CALLBACK(on_sidebar_remote_edit_clicked), app);
    gtk_box_append(GTK_BOX(remote_toolbar), remote_edit_btn);

    GtkWidget* remote_del_btn = icon_button_new("edit-delete", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(remote_del_btn), FALSE);
    gtk_widget_set_focusable(remote_del_btn, FALSE);
    gtk_widget_add_css_class(remote_del_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(remote_del_btn, "Delete Remote Machine (d / Delete)");
    g_signal_connect(remote_del_btn, "clicked", G_CALLBACK(on_sidebar_remote_delete_clicked), app);
    gtk_box_append(GTK_BOX(remote_toolbar), remote_del_btn);

    GtkWidget* remote_conn_btn = icon_button_new("network-transmit-receive", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(remote_conn_btn), FALSE);
    gtk_widget_set_focusable(remote_conn_btn, FALSE);
    gtk_widget_add_css_class(remote_conn_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(remote_conn_btn, "Connect / Disconnect Selected (Enter / c)");
    g_signal_connect(remote_conn_btn, "clicked", G_CALLBACK(on_sidebar_remote_connect_clicked), app);
    gtk_box_append(GTK_BOX(remote_toolbar), remote_conn_btn);

    GtkWidget* remote_term_btn = icon_button_new("utilities-terminal", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(remote_term_btn), FALSE);
    gtk_widget_set_focusable(remote_term_btn, FALSE);
    gtk_widget_add_css_class(remote_term_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(remote_term_btn, "Open Remote SSH Terminal (t)");
    g_signal_connect(remote_term_btn, "clicked", G_CALLBACK(on_sidebar_remote_term_clicked), app);
    gtk_box_append(GTK_BOX(remote_toolbar), remote_term_btn);

    GtkWidget* remote_ref_btn = icon_button_new("view-refresh", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(remote_ref_btn), FALSE);
    gtk_widget_set_focusable(remote_ref_btn, FALSE);
    gtk_widget_add_css_class(remote_ref_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(remote_ref_btn, "Re-scan ~/.ssh/config (F5)");
    g_signal_connect(remote_ref_btn, "clicked", G_CALLBACK(on_sidebar_remote_refresh_clicked), app);
    gtk_box_append(GTK_BOX(remote_toolbar), remote_ref_btn);

    GtkWidget* remote_collapse_btn = collapse_button_new(cb, CB_REMOTE, G_CALLBACK(on_sidebar_remote_collapse_clicked));
    gtk_box_append(GTK_BOX(remote_toolbar), remote_collapse_btn);

    gtk_widget_set_visible(remote_toolbar, (app != NULL && app->sidebar_view == SIDEBAR_VIEW_REMOTE));
    gtk_box_append(GTK_BOX(header), remote_toolbar);

    /* 5. Games Toolbar */
    GtkWidget* games_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_name(games_toolbar, "sidebar-games-toolbar");
    GtkWidget* games_play_btn = icon_button_new("media-playback-start", NULL);
    gtk_button_set_has_frame(GTK_BUTTON(games_play_btn), FALSE);
    gtk_widget_set_focusable(games_play_btn, FALSE);
    gtk_widget_add_css_class(games_play_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(games_play_btn, "Play Selected Game (Enter)");
    g_signal_connect(games_play_btn, "clicked", G_CALLBACK(on_sidebar_games_play_clicked), app);
    gtk_box_append(GTK_BOX(games_toolbar), games_play_btn);

    gtk_widget_set_visible(games_toolbar, (app != NULL && app->sidebar_view == SIDEBAR_VIEW_GAMES));
    gtk_box_append(GTK_BOX(header), games_toolbar);

    if (app != NULL) {
        app->sidebar_title = title;
        app->sidebar_section = section;
        app->sidebar_files_btn = files_btn;
        app->sidebar_bm_btn = bm_btn;
        app->sidebar_notes_btn = buf_btn;
        app->sidebar_remote_btn = remote_btn;
        app->sidebar_games_btn = games_btn;
        app->sidebar_files_toolbar = files_toolbar;
        app->sidebar_bm_toolbar = bm_toolbar;
        app->sidebar_notes_actions = buf_toolbar;
        app->sidebar_remote_toolbar = remote_toolbar;
        app->sidebar_games_toolbar = games_toolbar;
        app->sidebar_files_hidden_btn = files_hidden_btn;
    }
    g_object_set_data_full(G_OBJECT(header), "collapse-buttons", cb, cb_free);
    if (app != NULL && app->tree_view != NULL) {
        /* Disconnected when the header goes (it owns cb) */
        g_signal_connect_object(app->tree_view, "notify::model", G_CALLBACK(cb_on_model_changed),
                                header, 0);
        cb_on_model_changed(G_OBJECT(app->tree_view), NULL, header);
    }
    cb_update(cb);
    return header;
}
