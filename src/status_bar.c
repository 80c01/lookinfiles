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
    gtk_widget_set_tooltip_text(status_branch, "Git Branch of the Current Directory");
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
        gchar* text = g_strdup_printf("⎇ %s", branch);
        gtk_label_set_text(GTK_LABEL(app->status_branch), text);
        g_free(text);
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
        struct stat st;
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
