/* VS Code-style bottom status bar: Git branch, selection, size, file type and global mode menu. */
#include "status_bar.h"
#include "code_intel.h"
#include "code_nav.h"
#include "editor.h"
#include "file_types.h"
#include "git_utils.h"
#include "nav_keys.h"
#include "search.h"

static void on_status_mode_clicked_cb(GtkButton* btn, gpointer user_data);

/* The editor tab whose code navigation backend the status bar shows, or NULL
 * when the preview (or the Git tab) is the active page */
static EditorTab* status_mode_tab(AppData* app) {
    if (app == NULL || app->editors == NULL) {
        return NULL;
    }
    /* Keyboard focus inside an editor tab */
    GtkWidget* w = (app->window != NULL) ? gtk_window_get_focus(app->window) : NULL;
    for (; w != NULL; w = gtk_widget_get_parent(w)) {
        for (GList* l = app->editors; l != NULL; l = l->next) {
            EditorTab* t = (EditorTab*)l->data;
            /* Ignore a focus left in a tab that is no longer shown */
            if (t != NULL && t->container == w && gtk_widget_get_mapped(w)) {
                return t;
            }
        }
    }
    if (app->editor_box != NULL && app->active_panel == app->editor_box) {
        return editor_current_tab(app);
    }
    /* Preview panel hidden: only editor tabs are left */
    if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
        return editor_current_tab(app);
    }
    /* Editor tab shown in the preview notebook */
    if (app->notebook != NULL) {
        gint p = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
        GtkWidget* child = (p >= 0) ? gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), p) : NULL;
        for (GList* l = app->editors; child != NULL && l != NULL; l = l->next) {
            EditorTab* t = (EditorTab*)l->data;
            if (t != NULL && t->container == child) {
                return t;
            }
        }
    }
    return NULL;
}

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

    GtkWidget* status_code = status_item_new();
    gtk_box_append(GTK_BOX(bar), status_code);

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
        app->status_code = status_code;
        app->status_encoding = status_encoding;
        app->status_mode = status_mode_label;
        app->status_mode_btn = status_mode_btn;
    }

    return bar;
}

/* The global mode only: an editor tab's own mode is set from its tab menu */
static void on_mode_item_clicked(GtkButton* b, gpointer data) {
    GtkWidget* popover = GTK_WIDGET(data);
    AppData* app = g_object_get_data(G_OBJECT(b), "app");
    gint mode = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "mode"));
    if (app != NULL) {
        app->default_editor_mode = (EditorMode)mode;
        app->nav_prefix = NAV_PREFIX_NONE; /* a prefix of the old mode (g, C-x) */
        app->nav_recenter_step = 0;
        search_mode_changed(app);
        update_status_bar(app);
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

    GtkWidget* title = gtk_label_new("Global Mode");
    gtk_widget_add_css_class(title, "dim-label");
    gtk_widget_set_margin_bottom(title, 2);
    gtk_box_append(GTK_BOX(box), title);

    const struct { const gchar* label; EditorMode mode; } modes[] = {
        {"Notepad", EDITOR_MODE_NOTEPAD},
        {"Emacs", EDITOR_MODE_EMACS},
        {"Vim", EDITOR_MODE_VIM}
    };

    for (gsize i = 0; i < G_N_ELEMENTS(modes); i++) {
        GtkWidget* b = gtk_button_new_with_label(modes[i].label);
        gtk_button_set_has_frame(GTK_BUTTON(b), FALSE);
        if (modes[i].mode == app->default_editor_mode) {
            gtk_widget_add_css_class(b, "menu-item-current");
        }
        g_object_set_data(G_OBJECT(b), "app", app);
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
        /* Encoding of a previewed text file, as detected */
        gtk_label_set_text(GTK_LABEL(app->status_encoding),
                           previewed && app->viewed_encoding != NULL ? app->viewed_encoding : "UTF-8");

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

    /* Code navigation backend of the editor tab: language server, ctags or index */
    if (app->status_code != NULL) {
        EditorTab* cur = status_mode_tab(app);
        gchar* server = code_intel_server_label(app, cur);
        const gchar* backend = server != NULL ? server : code_nav_backend_label(cur);
        if (backend != NULL) {
            gtk_label_set_text(GTK_LABEL(app->status_code), backend);
            gchar* tip = server != NULL ? g_strdup_printf("Code Navigation: language server %s", server)
                                        : g_strdup_printf("Code Navigation: %s (no language server)", backend);
            gtk_widget_set_tooltip_text(app->status_code, tip);
            g_free(tip);
        }
        gtk_widget_set_visible(app->status_code, backend != NULL);
        g_free(server);
    }

    /* The global mode (never an editor tab's own mode) */
    if (app->status_mode != NULL) {
        const gchar* mstr = "Notepad";
        if (app->default_editor_mode == EDITOR_MODE_VIM) {
            mstr = "Vim";
        } else if (app->default_editor_mode == EDITOR_MODE_EMACS) {
            mstr = "Emacs";
        }
        gtk_label_set_text(GTK_LABEL(app->status_mode), mstr);
        if (app->status_mode_btn != NULL) {
            gtk_widget_set_tooltip_text(app->status_mode_btn,
                                        "Global Mode: Keys of the Explorer and Preview, Mode of New Editor Tabs");
        }
        gtk_widget_set_visible(app->status_mode, TRUE);
    }

    g_free(name);
    g_free(type);
    g_free(full);
}
