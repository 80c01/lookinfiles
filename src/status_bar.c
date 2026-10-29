/* VS Code-style bottom status bar: Git branch, selection, size, file type and editor mode menu. */
#include "status_bar.h"
#include "editor.h"
#include "file_types.h"
#include "git_utils.h"

static void on_status_mode_clicked_cb(GtkButton* btn, gpointer user_data);

static GtkWidget* status_item_new(void) {
    GtkWidget* label = gtk_label_new("");
    gtk_widget_add_css_class(label, "status-item");
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_single_line_mode(GTK_LABEL(label), TRUE);
    gtk_widget_set_visible(label, FALSE);
    return label;
}

/* VS Code-style bottom status bar: Git branch + selection on the left, size + type on the right */
GtkWidget* create_status_bar(AppData* app) {
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(bar, "bottom-statusbar");

    GtkWidget* status_branch = status_item_new();
    gtk_widget_add_css_class(status_branch, "status-branch");
    gtk_box_append(GTK_BOX(bar), status_branch);

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
    gtk_label_set_text(GTK_LABEL(status_encoding), "UTF-8");
    gtk_widget_set_visible(status_encoding, TRUE);
    gtk_box_append(GTK_BOX(bar), status_encoding);

    GtkWidget* status_mode_btn = gtk_button_new();
    gtk_button_set_has_frame(GTK_BUTTON(status_mode_btn), FALSE);
    GtkWidget* status_mode_label = gtk_label_new("NOTEPAD");
    gtk_button_set_child(GTK_BUTTON(status_mode_btn), status_mode_label);
    gtk_widget_add_css_class(status_mode_btn, "status-item");
    gtk_widget_set_visible(status_mode_btn, TRUE);
    if (app != NULL) {
        g_object_set_data(G_OBJECT(status_mode_btn), "app-data", app);
        g_signal_connect(status_mode_btn, "clicked", G_CALLBACK(on_status_mode_clicked_cb), app);
    }
    gtk_box_append(GTK_BOX(bar), status_mode_btn);

    if (app != NULL) {
        app->status_branch = status_branch;
        app->status_selection = status_selection;
        app->status_size = status_size;
        app->status_type = status_type;
        app->status_encoding = status_encoding;
        app->status_mode = status_mode_label;
        app->status_mode_btn = status_mode_btn;
    }

    return bar;
}

static void on_mode_item_clicked(GtkButton* b, gpointer data) {
    GtkWidget* popover = GTK_WIDGET(data);
    AppData* app = g_object_get_data(G_OBJECT(b), "app");
    EditorTab* tab = g_object_get_data(G_OBJECT(b), "tab");
    gint mode = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "mode"));
    if (app != NULL && tab == NULL && app->editors != NULL) {
        tab = app->last_focused_editor ? app->last_focused_editor : (EditorTab*)app->editors->data;
    }
    if (app != NULL && tab != NULL) {
        editor_set_tab_mode(app, tab, (EditorMode)mode);
    }
    if (popover != NULL) {
        gtk_popover_popdown(GTK_POPOVER(popover));
    }
}

static void on_status_mode_clicked_cb(GtkButton* btn, gpointer user_data) {
    AppData* app = (AppData*)user_data;
    if (app == NULL) return;
    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_parent(popover, GTK_WIDGET(btn));
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), TRUE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_TOP);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);

    EditorTab* cur = app->last_focused_editor;
    if (cur == NULL && app->editors != NULL) {
        cur = (EditorTab*)app->editors->data;
    }

    const struct { const gchar* label; EditorMode mode; } modes[] = {
        {"Notepad", EDITOR_MODE_NOTEPAD},
        {"Emacs", EDITOR_MODE_EMACS},
        {"Vim", EDITOR_MODE_VIM}
    };

    for (gsize i = 0; i < G_N_ELEMENTS(modes); i++) {
        GtkWidget* b = gtk_button_new_with_label(modes[i].label);
        gtk_button_set_has_frame(GTK_BUTTON(b), FALSE);
        g_object_set_data(G_OBJECT(b), "app", app);
        g_object_set_data(G_OBJECT(b), "tab", cur);
        g_object_set_data(G_OBJECT(b), "mode", GINT_TO_POINTER(modes[i].mode));
        g_signal_connect(b, "clicked", G_CALLBACK(on_mode_item_clicked), popover);
        gtk_box_append(GTK_BOX(box), b);
    }

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect_swapped(popover, "closed", G_CALLBACK(gtk_widget_unparent), popover);
    gtk_popover_popup(GTK_POPOVER(popover));
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
    if (g_strcmp0(type, "trash") == 0) {
        return g_strdup("Trash");
    }
    if (g_strcmp0(type, "trash_note") == 0) {
        return g_strdup("Trashed Note");
    }
    if (g_strcmp0(type, "note") == 0 || g_strcmp0(type, "buffer") == 0) {
        return g_strdup("Note");
    }
    switch (kind) {
    case FILE_KIND_MARKDOWN: return g_strdup("Markdown");
    case FILE_KIND_ORG:      return g_strdup("Org-mode");
    case FILE_KIND_HTML:     return g_strdup("HTML");
    case FILE_KIND_PDF:      return g_strdup("PDF");
    case FILE_KIND_IMAGE:    return g_strdup("Image");
    case FILE_KIND_VIDEO:    return g_strdup("Video");
    case FILE_KIND_AUDIO:    return g_strdup("Audio");
    case FILE_KIND_BINARY:   return g_strdup("Binary");
    case FILE_KIND_EXECUTABLE: return g_strdup("ELF");
    case FILE_KIND_DOCUMENT: return g_strdup("Word");
    case FILE_KIND_TEXT:     break;
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

/* Refresh the bottom status bar from the current directory and explorer selection */
void update_status_bar(AppData* app) {
    if (app == NULL || app->status_branch == NULL) {
        return;
    }

    /* The branch only makes sense for the files view (like the Git Commits tab) */
    gboolean files_view = (app->sidebar_view == SIDEBAR_VIEW_FILES && !app->show_bookmarks);
    gchar* branch = files_view ? git_read_branch(app->base_dir) : NULL;
    if (branch != NULL) {
        gchar* text = g_strdup_printf("⎇ %s", branch);
        gtk_label_set_text(GTK_LABEL(app->status_branch), text);
        gtk_widget_set_tooltip_text(app->status_branch, "Git Branch of the Current Directory");
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

        gboolean is_file = (g_strcmp0(type, "dir") != 0 && g_strcmp0(type, "bookmark") != 0 &&
                            g_strcmp0(type, "bm_folder") != 0 && g_strcmp0(type, "notebook") != 0 &&
                            g_strcmp0(type, "trash") != 0);
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

    /* Update editor mode in status bar */
    if (app->status_mode != NULL) {
        EditorTab* cur = NULL;
        /* Try to find current editor tab */
        if (app->editors != NULL) {
            /* Prefer last focused */
            if (app->last_focused_editor != NULL) {
                cur = app->last_focused_editor;
            } else {
                cur = (EditorTab*)app->editors->data;
            }
        }
        const gchar* mstr = "Notepad";
        if (cur != NULL) {
            if (cur->mode == EDITOR_MODE_ORGMODE || (cur->name != NULL && is_org_file(cur->name))) {
                mstr = "Org-mode";
            } else if (cur->mode == EDITOR_MODE_VIM) {
                mstr = "Vim";
            } else if (cur->mode == EDITOR_MODE_EMACS) {
                mstr = "Emacs";
            } else {
                mstr = "Notepad";
            }
        } else if (name != NULL && is_org_file(name)) {
            mstr = "Org-mode";
        }
        gtk_label_set_text(GTK_LABEL(app->status_mode), mstr);
        gtk_widget_set_visible(app->status_mode, TRUE);
    }

    g_free(name);
    g_free(type);
    g_free(full);
}
