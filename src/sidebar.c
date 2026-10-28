/* Explorer panel header: view switcher buttons (files, bookmarks, notes, remote) and per-view action toolbars. */
#include "sidebar.h"
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
        update_path_label(app);
        refresh_listing(app);
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

static void on_sidebar_remote_add_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    remote_add_dialog((AppData*)data);
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
    remote_refresh((AppData*)data);
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

static void on_sidebar_files_sort_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    cycle_sort_mode((AppData*)data);
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

static void on_sidebar_bm_sort_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    bookmarks_toggle_sort((AppData*)data);
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

static void on_sidebar_notes_move_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    notes_move_selected((AppData*)data);
}

static void on_sidebar_notes_delete_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    notes_delete_selected((AppData*)data);
}

static void on_sidebar_notes_sort_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    notes_toggle_sort((AppData*)data);
}

static void on_sidebar_notes_collapse_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    notes_toggle_collapse_expand((AppData*)data);
}

static void on_sidebar_notes_empty_trash_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    notes_empty_trash((AppData*)data);
}

/* VS Code-style explorer panel header: "EXPLORER" title + mini view icons + action bars */
GtkWidget* create_sidebar_header(AppData* app) {
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(header, "sidebar-header");

    GtkWidget* title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_name(title_row, "sidebar-title-row");

    GtkWidget* title = gtk_label_new("");
    gtk_widget_set_name(title, "sidebar-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(title, TRUE);
    gtk_box_append(GTK_BOX(title_row), title);

    GtkWidget* files_btn = gtk_button_new_with_label("📁");
    gtk_button_set_has_frame(GTK_BUTTON(files_btn), FALSE);
    gtk_widget_set_focusable(files_btn, FALSE);
    gtk_widget_add_css_class(files_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(files_btn, "Files Explorer (Ctrl+A)");
    g_signal_connect(files_btn, "clicked", G_CALLBACK(on_sidebar_files_clicked), app);
    gtk_box_append(GTK_BOX(title_row), files_btn);

    GtkWidget* bm_btn = gtk_button_new_with_label("🔖");
    gtk_button_set_has_frame(GTK_BUTTON(bm_btn), FALSE);
    gtk_widget_set_focusable(bm_btn, FALSE);
    gtk_widget_add_css_class(bm_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(bm_btn, "Bookmarks (Ctrl+A)");
    g_signal_connect(bm_btn, "clicked", G_CALLBACK(on_sidebar_bm_clicked), app);
    gtk_box_append(GTK_BOX(title_row), bm_btn);

    GtkWidget* buf_btn = gtk_button_new_with_label("📝");
    gtk_button_set_has_frame(GTK_BUTTON(buf_btn), FALSE);
    gtk_widget_set_focusable(buf_btn, FALSE);
    gtk_widget_add_css_class(buf_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(buf_btn, "Notes (Ctrl+U)");
    g_signal_connect(buf_btn, "clicked", G_CALLBACK(on_sidebar_notes_clicked), app);
    gtk_box_append(GTK_BOX(title_row), buf_btn);

    GtkWidget* remote_btn = gtk_button_new_with_label("🌐");
    gtk_button_set_has_frame(GTK_BUTTON(remote_btn), FALSE);
    gtk_widget_set_focusable(remote_btn, FALSE);
    gtk_widget_add_css_class(remote_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(remote_btn, "Remote SSH (Ctrl+Shift+R)");
    g_signal_connect(remote_btn, "clicked", G_CALLBACK(on_sidebar_remote_clicked), app);
    gtk_box_append(GTK_BOX(title_row), remote_btn);

    if (app != NULL && app->sidebar_view == SIDEBAR_VIEW_REMOTE) {
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

    GtkWidget* files_new_file_btn = gtk_button_new_with_label("📄+");
    gtk_button_set_has_frame(GTK_BUTTON(files_new_file_btn), FALSE);
    gtk_widget_set_focusable(files_new_file_btn, FALSE);
    gtk_widget_add_css_class(files_new_file_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(files_new_file_btn, "New File");
    g_signal_connect(files_new_file_btn, "clicked", G_CALLBACK(on_sidebar_files_new_file_clicked), app);
    gtk_box_append(GTK_BOX(files_toolbar), files_new_file_btn);

    GtkWidget* files_new_folder_btn = gtk_button_new_with_label("📁+");
    gtk_button_set_has_frame(GTK_BUTTON(files_new_folder_btn), FALSE);
    gtk_widget_set_focusable(files_new_folder_btn, FALSE);
    gtk_widget_add_css_class(files_new_folder_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(files_new_folder_btn, "New Folder");
    g_signal_connect(files_new_folder_btn, "clicked", G_CALLBACK(on_sidebar_files_new_folder_clicked), app);
    gtk_box_append(GTK_BOX(files_toolbar), files_new_folder_btn);

    GtkWidget* files_hidden_btn = gtk_button_new_with_label("👁");
    gtk_button_set_has_frame(GTK_BUTTON(files_hidden_btn), FALSE);
    gtk_widget_set_focusable(files_hidden_btn, FALSE);
    gtk_widget_add_css_class(files_hidden_btn, "sidebar-icon-btn");
    if (app != NULL && app->show_hidden) {
        gtk_widget_add_css_class(files_hidden_btn, "sidebar-icon-active");
    }
    gtk_widget_set_tooltip_text(files_hidden_btn, "Toggle Hidden Files (i / I)");
    g_signal_connect(files_hidden_btn, "clicked", G_CALLBACK(on_sidebar_files_hidden_clicked), app);
    gtk_box_append(GTK_BOX(files_toolbar), files_hidden_btn);

    GtkWidget* files_sort_btn = gtk_button_new_with_label("⇅");
    gtk_button_set_has_frame(GTK_BUTTON(files_sort_btn), FALSE);
    gtk_widget_set_focusable(files_sort_btn, FALSE);
    gtk_widget_add_css_class(files_sort_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(files_sort_btn, "Cycle Sort: Name / Date / Size / Ext (o)");
    g_signal_connect(files_sort_btn, "clicked", G_CALLBACK(on_sidebar_files_sort_clicked), app);
    gtk_box_append(GTK_BOX(files_toolbar), files_sort_btn);

    GtkWidget* files_collapse_btn = gtk_button_new_with_label("▲▼");
    gtk_button_set_has_frame(GTK_BUTTON(files_collapse_btn), FALSE);
    gtk_widget_set_focusable(files_collapse_btn, FALSE);
    gtk_widget_add_css_class(files_collapse_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(files_collapse_btn, "Toggle Collapse / Expand Folders");
    g_signal_connect(files_collapse_btn, "clicked", G_CALLBACK(on_sidebar_files_collapse_clicked), app);
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

    GtkWidget* bm_add_btn = gtk_button_new_with_label("🔖+");
    gtk_button_set_has_frame(GTK_BUTTON(bm_add_btn), FALSE);
    gtk_widget_set_focusable(bm_add_btn, FALSE);
    gtk_widget_add_css_class(bm_add_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(bm_add_btn, "Add Bookmark (a)");
    g_signal_connect(bm_add_btn, "clicked", G_CALLBACK(on_sidebar_bm_add_clicked), app);
    gtk_box_append(GTK_BOX(bm_toolbar), bm_add_btn);

    GtkWidget* bm_new_folder_btn = gtk_button_new_with_label("📁+");
    gtk_button_set_has_frame(GTK_BUTTON(bm_new_folder_btn), FALSE);
    gtk_widget_set_focusable(bm_new_folder_btn, FALSE);
    gtk_widget_add_css_class(bm_new_folder_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(bm_new_folder_btn, "New Bookmark Folder (Shift+A)");
    g_signal_connect(bm_new_folder_btn, "clicked", G_CALLBACK(on_sidebar_bm_new_folder_clicked), app);
    gtk_box_append(GTK_BOX(bm_toolbar), bm_new_folder_btn);

    GtkWidget* bm_edit_btn = gtk_button_new_with_label("✎");
    gtk_button_set_has_frame(GTK_BUTTON(bm_edit_btn), FALSE);
    gtk_widget_set_focusable(bm_edit_btn, FALSE);
    gtk_widget_add_css_class(bm_edit_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(bm_edit_btn, "Edit / Rename / Move Selected (F2)");
    g_signal_connect(bm_edit_btn, "clicked", G_CALLBACK(on_sidebar_bm_edit_clicked), app);
    gtk_box_append(GTK_BOX(bm_toolbar), bm_edit_btn);

    GtkWidget* bm_delete_btn = gtk_button_new_with_label("🗑");
    gtk_button_set_has_frame(GTK_BUTTON(bm_delete_btn), FALSE);
    gtk_widget_set_focusable(bm_delete_btn, FALSE);
    gtk_widget_add_css_class(bm_delete_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(bm_delete_btn, "Delete Selected (d / Delete)");
    g_signal_connect(bm_delete_btn, "clicked", G_CALLBACK(on_sidebar_bm_delete_clicked), app);
    gtk_box_append(GTK_BOX(bm_toolbar), bm_delete_btn);

    GtkWidget* bm_sort_btn = gtk_button_new_with_label("⇅");
    gtk_button_set_has_frame(GTK_BUTTON(bm_sort_btn), FALSE);
    gtk_widget_set_focusable(bm_sort_btn, FALSE);
    gtk_widget_add_css_class(bm_sort_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(bm_sort_btn, "Toggle Sort: A-Z / Z-A");
    g_signal_connect(bm_sort_btn, "clicked", G_CALLBACK(on_sidebar_bm_sort_clicked), app);
    gtk_box_append(GTK_BOX(bm_toolbar), bm_sort_btn);

    GtkWidget* bm_collapse_btn = gtk_button_new_with_label("▲▼");
    gtk_button_set_has_frame(GTK_BUTTON(bm_collapse_btn), FALSE);
    gtk_widget_set_focusable(bm_collapse_btn, FALSE);
    gtk_widget_add_css_class(bm_collapse_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(bm_collapse_btn, "Toggle Collapse / Expand Bookmarks");
    g_signal_connect(bm_collapse_btn, "clicked", G_CALLBACK(on_sidebar_bm_collapse_clicked), app);
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

    GtkWidget* new_note_btn = gtk_button_new_with_label("➕");
    gtk_button_set_has_frame(GTK_BUTTON(new_note_btn), FALSE);
    gtk_widget_set_focusable(new_note_btn, FALSE);
    gtk_widget_add_css_class(new_note_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(new_note_btn, "New Note (a)");
    g_signal_connect(new_note_btn, "clicked", G_CALLBACK(on_sidebar_notes_new_clicked), app);
    gtk_box_append(GTK_BOX(buf_toolbar), new_note_btn);

    GtkWidget* new_nb_btn = gtk_button_new_with_label("📓+");
    gtk_button_set_has_frame(GTK_BUTTON(new_nb_btn), FALSE);
    gtk_widget_set_focusable(new_nb_btn, FALSE);
    gtk_widget_add_css_class(new_nb_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(new_nb_btn, "New Notebook (Shift+A)");
    g_signal_connect(new_nb_btn, "clicked", G_CALLBACK(on_sidebar_notes_new_notebook_clicked), app);
    gtk_box_append(GTK_BOX(buf_toolbar), new_nb_btn);

    GtkWidget* edit_note_btn = gtk_button_new_with_label("✎");
    gtk_button_set_has_frame(GTK_BUTTON(edit_note_btn), FALSE);
    gtk_widget_set_focusable(edit_note_btn, FALSE);
    gtk_widget_add_css_class(edit_note_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(edit_note_btn, "Rename / Edit Selected (F2 / r)");
    g_signal_connect(edit_note_btn, "clicked", G_CALLBACK(on_sidebar_notes_edit_clicked), app);
    gtk_box_append(GTK_BOX(buf_toolbar), edit_note_btn);

    GtkWidget* move_note_btn = gtk_button_new_with_label("📁→");
    gtk_button_set_has_frame(GTK_BUTTON(move_note_btn), FALSE);
    gtk_widget_set_focusable(move_note_btn, FALSE);
    gtk_widget_add_css_class(move_note_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(move_note_btn, "Move Note to Notebook (m)");
    g_signal_connect(move_note_btn, "clicked", G_CALLBACK(on_sidebar_notes_move_clicked), app);
    gtk_box_append(GTK_BOX(buf_toolbar), move_note_btn);

    GtkWidget* del_note_btn = gtk_button_new_with_label("🗑");
    gtk_button_set_has_frame(GTK_BUTTON(del_note_btn), FALSE);
    gtk_widget_set_focusable(del_note_btn, FALSE);
    gtk_widget_add_css_class(del_note_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(del_note_btn, "Move to Trash / Delete (d / Delete)");
    g_signal_connect(del_note_btn, "clicked", G_CALLBACK(on_sidebar_notes_delete_clicked), app);
    gtk_box_append(GTK_BOX(buf_toolbar), del_note_btn);

    GtkWidget* sort_note_btn = gtk_button_new_with_label("⇅");
    gtk_button_set_has_frame(GTK_BUTTON(sort_note_btn), FALSE);
    gtk_widget_set_focusable(sort_note_btn, FALSE);
    gtk_widget_add_css_class(sort_note_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(sort_note_btn, "Toggle Sort Mode (Manual / A-Z / Z-A)");
    g_signal_connect(sort_note_btn, "clicked", G_CALLBACK(on_sidebar_notes_sort_clicked), app);
    gtk_box_append(GTK_BOX(buf_toolbar), sort_note_btn);

    GtkWidget* collapse_note_btn = gtk_button_new_with_label("▲▼");
    gtk_button_set_has_frame(GTK_BUTTON(collapse_note_btn), FALSE);
    gtk_widget_set_focusable(collapse_note_btn, FALSE);
    gtk_widget_add_css_class(collapse_note_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(collapse_note_btn, "Toggle Collapse / Expand Notebooks");
    g_signal_connect(collapse_note_btn, "clicked", G_CALLBACK(on_sidebar_notes_collapse_clicked), app);
    gtk_box_append(GTK_BOX(buf_toolbar), collapse_note_btn);

    GtkWidget* empty_trash_btn = gtk_button_new_with_label("🧹");
    gtk_button_set_has_frame(GTK_BUTTON(empty_trash_btn), FALSE);
    gtk_widget_set_focusable(empty_trash_btn, FALSE);
    gtk_widget_add_css_class(empty_trash_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(empty_trash_btn, "Empty Trash Bin");
    g_signal_connect(empty_trash_btn, "clicked", G_CALLBACK(on_sidebar_notes_empty_trash_clicked), app);
    gtk_box_append(GTK_BOX(buf_toolbar), empty_trash_btn);

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

    GtkWidget* remote_conn_btn = gtk_button_new_with_label("🔌");
    gtk_button_set_has_frame(GTK_BUTTON(remote_conn_btn), FALSE);
    gtk_widget_set_focusable(remote_conn_btn, FALSE);
    gtk_widget_add_css_class(remote_conn_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(remote_conn_btn, "Connect / Disconnect Selected (Enter / c)");
    g_signal_connect(remote_conn_btn, "clicked", G_CALLBACK(on_sidebar_remote_connect_clicked), app);
    gtk_box_append(GTK_BOX(remote_toolbar), remote_conn_btn);

    GtkWidget* remote_term_btn = gtk_button_new_with_label(">_");
    gtk_button_set_has_frame(GTK_BUTTON(remote_term_btn), FALSE);
    gtk_widget_set_focusable(remote_term_btn, FALSE);
    gtk_widget_add_css_class(remote_term_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(remote_term_btn, "Open Remote SSH Terminal (t)");
    g_signal_connect(remote_term_btn, "clicked", G_CALLBACK(on_sidebar_remote_term_clicked), app);
    gtk_box_append(GTK_BOX(remote_toolbar), remote_term_btn);

    GtkWidget* remote_add_btn = gtk_button_new_with_label("➕");
    gtk_button_set_has_frame(GTK_BUTTON(remote_add_btn), FALSE);
    gtk_widget_set_focusable(remote_add_btn, FALSE);
    gtk_widget_add_css_class(remote_add_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(remote_add_btn, "Add Remote Machine (a)");
    g_signal_connect(remote_add_btn, "clicked", G_CALLBACK(on_sidebar_remote_add_clicked), app);
    gtk_box_append(GTK_BOX(remote_toolbar), remote_add_btn);

    GtkWidget* remote_edit_btn = gtk_button_new_with_label("✎");
    gtk_button_set_has_frame(GTK_BUTTON(remote_edit_btn), FALSE);
    gtk_widget_set_focusable(remote_edit_btn, FALSE);
    gtk_widget_add_css_class(remote_edit_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(remote_edit_btn, "Edit Remote Machine (F2 / r)");
    g_signal_connect(remote_edit_btn, "clicked", G_CALLBACK(on_sidebar_remote_edit_clicked), app);
    gtk_box_append(GTK_BOX(remote_toolbar), remote_edit_btn);

    GtkWidget* remote_del_btn = gtk_button_new_with_label("🗑");
    gtk_button_set_has_frame(GTK_BUTTON(remote_del_btn), FALSE);
    gtk_widget_set_focusable(remote_del_btn, FALSE);
    gtk_widget_add_css_class(remote_del_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(remote_del_btn, "Delete Remote Machine (Delete / d)");
    g_signal_connect(remote_del_btn, "clicked", G_CALLBACK(on_sidebar_remote_delete_clicked), app);
    gtk_box_append(GTK_BOX(remote_toolbar), remote_del_btn);

    GtkWidget* remote_ref_btn = gtk_button_new_with_label("↻");
    gtk_button_set_has_frame(GTK_BUTTON(remote_ref_btn), FALSE);
    gtk_widget_set_focusable(remote_ref_btn, FALSE);
    gtk_widget_add_css_class(remote_ref_btn, "sidebar-icon-btn");
    gtk_widget_set_tooltip_text(remote_ref_btn, "Refresh Remote Machines (R)");
    g_signal_connect(remote_ref_btn, "clicked", G_CALLBACK(on_sidebar_remote_refresh_clicked), app);
    gtk_box_append(GTK_BOX(remote_toolbar), remote_ref_btn);

    gtk_widget_set_visible(remote_toolbar, (app != NULL && app->sidebar_view == SIDEBAR_VIEW_REMOTE));
    gtk_box_append(GTK_BOX(header), remote_toolbar);

    if (app != NULL) {
        app->sidebar_title = title;
        app->sidebar_section = section;
        app->sidebar_files_btn = files_btn;
        app->sidebar_bm_btn = bm_btn;
        app->sidebar_notes_btn = buf_btn;
        app->sidebar_remote_btn = remote_btn;
        app->sidebar_files_toolbar = files_toolbar;
        app->sidebar_bm_toolbar = bm_toolbar;
        app->sidebar_notes_actions = buf_toolbar;
        app->sidebar_remote_toolbar = remote_toolbar;
        app->sidebar_files_hidden_btn = files_hidden_btn;
    }
    return header;
}
