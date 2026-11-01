#include "bookmarks.h"
#include "palette.h"
#include "app_paths.h"
#include "dialog.h"
#include "app_events.h"
#include "tree_nav.h"
#include "file_list.h"
#include "bookmark_icons.h"

#include <string.h>

/* One node of the bookmark tree (GNode data). The root node has NULL data. */
typedef struct {
    guint id;           /* session-unique id, stored in ExplorerRow.bm_id */
    gboolean is_folder; /* virtual organizational folder */
    gchar* name;        /* user-visible name */
    gchar* target;      /* links only: absolute path or URI */
    gboolean expanded;  /* folders only: expanded in the explorer panel */
} BookmarkItem;

enum {
    BM_ACTION_OPEN = 1,
    BM_ACTION_ADD,
    BM_ACTION_ADD_FOLDER,
    BM_ACTION_EDIT,
    BM_ACTION_DELETE,
    BM_ACTION_MOVE_UP,
    BM_ACTION_MOVE_DOWN,
    BM_ACTION_SORT,
    BM_ACTION_COLLAPSE
};

/* ------------------------------------------------------------------------- */
/* Model helpers                                                             */
/* ------------------------------------------------------------------------- */

static BookmarkItem* bm_item(GNode* node) {
    return (node != NULL) ? (BookmarkItem*)node->data : NULL;
}

static gboolean bm_is_folder(GNode* node) {
    BookmarkItem* it = bm_item(node);
    return it != NULL && it->is_folder;
}

static GNode* bm_new_node(AppData* app, gboolean is_folder, const gchar* name, const gchar* target) {
    BookmarkItem* it = g_new0(BookmarkItem, 1);
    it->id = ++app->bm_next_id;
    it->is_folder = is_folder;
    it->name = g_strdup(name != NULL ? name : "");
    it->target = is_folder ? NULL : g_strdup(target != NULL ? target : "");
    return g_node_new(it);
}

static gboolean bm_free_node_data(GNode* node, gpointer data) {
    (void)data;
    BookmarkItem* it = bm_item(node);
    if (it != NULL) {
        g_free(it->name);
        g_free(it->target);
        g_free(it);
        node->data = NULL;
    }
    return FALSE;
}

static void bm_destroy_subtree(GNode* node) {
    if (node == NULL) {
        return;
    }
    g_node_traverse(node, G_POST_ORDER, G_TRAVERSE_ALL, -1, bm_free_node_data, NULL);
    g_node_destroy(node);
}

static GNode* bm_find_by_id(GNode* node, guint id) {
    if (node == NULL || id == 0) {
        return NULL;
    }
    for (GNode* c = node->children; c != NULL; c = c->next) {
        if (bm_item(c)->id == id) {
            return c;
        }
        GNode* r = bm_find_by_id(c, id);
        if (r != NULL) {
            return r;
        }
    }
    return NULL;
}

static GNode* bm_find_link_by_target(GNode* node, const gchar* target) {
    if (node == NULL || target == NULL) {
        return NULL;
    }
    for (GNode* c = node->children; c != NULL; c = c->next) {
        BookmarkItem* it = bm_item(c);
        if (!it->is_folder && g_strcmp0(it->target, target) == 0) {
            return c;
        }
        GNode* r = bm_find_link_by_target(c, target);
        if (r != NULL) {
            return r;
        }
    }
    return NULL;
}

/* "~/..." for paths under the home directory, unchanged otherwise */
static gchar* bm_display_location(const gchar* target) {
    if (target == NULL) {
        return g_strdup("");
    }
    if (bookmark_target_is_uri(target)) {
        return g_strdup(target);
    }
    const gchar* home = g_get_home_dir();
    gsize home_len = (home != NULL) ? strlen(home) : 0;
    if (home_len > 1 && g_str_has_prefix(target, home) &&
        (target[home_len] == '\0' || target[home_len] == '/')) {
        return g_strdup_printf("~%s", target + home_len);
    }
    return g_strdup(target);
}

/* Turn user input into a canonical absolute path, or keep it as a URI */
static gchar* bm_normalize_target(const gchar* text, const gchar* base_dir) {
    gchar* t = g_strstrip(g_strdup(text != NULL ? text : ""));
    if (t[0] == '\0') {
        g_free(t);
        return NULL;
    }
    if (bookmark_target_is_uri(t)) {
        if (g_ascii_strcasecmp(g_uri_peek_scheme(t), "file") != 0) {
            return t;
        }
        gchar* local = g_filename_from_uri(t, NULL, NULL);
        if (local == NULL) {
            return t;
        }
        g_free(t);
        t = local;
    }
    gchar* abs = NULL;
    if (t[0] == '~' && (t[1] == '\0' || t[1] == '/')) {
        abs = g_build_filename(g_get_home_dir(), t + 1, NULL);
    } else {
        abs = g_strdup(t);
    }
    const gchar* rel = (base_dir != NULL && g_path_is_absolute(base_dir)) ? base_dir : NULL;
    gchar* canon = g_canonicalize_filename(abs, rel);
    g_free(abs);
    g_free(t);
    return canon;
}

static gchar* bm_default_name(const gchar* target) {
    if (target == NULL || target[0] == '\0') {
        return g_strdup("");
    }
    if (bookmark_target_is_uri(target)) {
        GUri* uri = g_uri_parse(target, G_URI_FLAGS_NONE, NULL);
        gchar* name = NULL;
        if (uri != NULL) {
            const gchar* host = g_uri_get_host(uri);
            if (host != NULL && host[0] != '\0') {
                name = g_strdup(host);
            }
            g_uri_unref(uri);
        }
        return (name != NULL) ? name : g_strdup(target);
    }
    return g_path_get_basename(target);
}

/* ------------------------------------------------------------------------- */
/* Persistence (XML)                                                         */
/* ------------------------------------------------------------------------- */

static gchar* bm_config_file(const gchar* basename) {
    gchar* dir = app_config_dir();
    g_mkdir_with_parents(dir, 0755);
    gchar* path = g_build_filename(dir, basename, NULL);
    g_free(dir);
    return path;
}

static void bm_write_children(GString* out, GNode* node, gint depth) {
    for (GNode* c = node->children; c != NULL; c = c->next) {
        BookmarkItem* it = bm_item(c);
        for (gint i = 0; i < depth; i++) {
            g_string_append(out, "  ");
        }
        gchar* line = NULL;
        if (it->is_folder) {
            line = g_markup_printf_escaped("<folder name=\"%s\" expanded=\"%s\"%s>\n",
                                           it->name, it->expanded ? "true" : "false",
                                           c->children != NULL ? "" : "/");
            g_string_append(out, line);
            if (c->children != NULL) {
                bm_write_children(out, c, depth + 1);
                for (gint i = 0; i < depth; i++) {
                    g_string_append(out, "  ");
                }
                g_string_append(out, "</folder>\n");
            }
        } else {
            line = g_markup_printf_escaped("<bookmark name=\"%s\" target=\"%s\"/>\n",
                                           it->name, it->target);
            g_string_append(out, line);
        }
        g_free(line);
    }
}

void bookmarks_save(AppData* app) {
    if (app == NULL || app->bookmarks == NULL) {
        return;
    }
    GString* out = g_string_new("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<bookmarks version=\"2\">\n");
    bm_write_children(out, app->bookmarks, 1);
    g_string_append(out, "</bookmarks>\n");
    gchar* path = bm_config_file("bookmarks.xml");
    g_file_set_contents(path, out->str, (gssize)out->len, NULL);
    g_free(path);
    g_string_free(out, TRUE);
}

typedef struct {
    AppData* app;
    GNode* current;
} BmParseState;

static void bm_parse_start(GMarkupParseContext* ctx, const gchar* element,
                           const gchar** attr_names, const gchar** attr_values,
                           gpointer user_data, GError** error) {
    (void)ctx;
    (void)error;
    BmParseState* st = (BmParseState*)user_data;
    const gchar* name = NULL;
    const gchar* target = NULL;
    const gchar* expanded = NULL;
    for (gint i = 0; attr_names[i] != NULL; i++) {
        if (g_strcmp0(attr_names[i], "name") == 0) {
            name = attr_values[i];
        } else if (g_strcmp0(attr_names[i], "target") == 0) {
            target = attr_values[i];
        } else if (g_strcmp0(attr_names[i], "expanded") == 0) {
            expanded = attr_values[i];
        }
    }
    if (g_strcmp0(element, "folder") == 0) {
        GNode* node = bm_new_node(st->app, TRUE, (name != NULL && name[0] != '\0') ? name : "Folder", NULL);
        bm_item(node)->expanded = (g_strcmp0(expanded, "true") == 0);
        g_node_append(st->current, node);
        st->current = node;
    } else if (g_strcmp0(element, "bookmark") == 0 && target != NULL && target[0] != '\0') {
        gchar* fallback = bm_default_name(target);
        GNode* node = bm_new_node(st->app, FALSE, (name != NULL && name[0] != '\0') ? name : fallback, target);
        g_free(fallback);
        g_node_append(st->current, node);
    }
}

static void bm_parse_end(GMarkupParseContext* ctx, const gchar* element,
                         gpointer user_data, GError** error) {
    (void)ctx;
    (void)error;
    BmParseState* st = (BmParseState*)user_data;
    if (g_strcmp0(element, "folder") == 0 && st->current->parent != NULL) {
        st->current = st->current->parent;
    }
}

/* Import the pre-v2 flat file: one path per line, "BM::name" for folders */
static gboolean bm_import_legacy(AppData* app) {
    gchar* path = bm_config_file("bookmarks");
    gchar* content = NULL;
    gboolean imported = FALSE;
    if (g_file_get_contents(path, &content, NULL, NULL)) {
        gchar** lines = g_strsplit(content, "\n", -1);
        for (gint i = 0; lines[i] != NULL; i++) {
            gchar* line = g_strstrip(lines[i]);
            if (line[0] == '\0') {
                continue;
            }
            GNode* node = NULL;
            if (g_str_has_prefix(line, "BM::")) {
                node = bm_new_node(app, TRUE, line + 4, NULL);
            } else if (bm_find_link_by_target(app->bookmarks, line) == NULL) {
                gchar* name = bm_default_name(line);
                node = bm_new_node(app, FALSE, name, line);
                g_free(name);
            }
            if (node != NULL) {
                g_node_append(app->bookmarks, node);
                imported = TRUE;
            }
        }
        g_strfreev(lines);
        g_free(content);
    }
    g_free(path);
    return imported;
}

void bookmarks_init(AppData* app) {
    if (app == NULL) {
        return;
    }
    app->bookmarks = g_node_new(NULL);
    app->bm_next_id = 0;
    app->bm_last_folder_id = 0;
    app->show_bookmarks = FALSE;

    gchar* path = bm_config_file("bookmarks.xml");
    gchar* content = NULL;
    gsize length = 0;
    if (g_file_get_contents(path, &content, &length, NULL)) {
        static const GMarkupParser parser = { bm_parse_start, bm_parse_end, NULL, NULL, NULL };
        BmParseState st = { app, app->bookmarks };
        GMarkupParseContext* ctx = g_markup_parse_context_new(&parser, 0, &st, NULL);
        if (!g_markup_parse_context_parse(ctx, content, (gssize)length, NULL)) {
            g_warning("Bookmarks file %s is malformed; entries after the error were skipped", path);
        }
        g_markup_parse_context_free(ctx);
        g_free(content);
    } else if (bm_import_legacy(app)) {
        bookmarks_save(app);
    }
    g_free(path);
}

void bookmarks_free(AppData* app) {
    if (app == NULL || app->bookmarks == NULL) {
        return;
    }
    bm_destroy_subtree(app->bookmarks);
    app->bookmarks = NULL;
}

/* ------------------------------------------------------------------------- */
/* Explorer panel rendering                                                  */
/* ------------------------------------------------------------------------- */

static void bm_append_placeholder(ExplorerRow* parent, const gchar* label) {
    explorer_row_append(parent, "-", "", label, "");
}

static void bm_append_children(AppData* app, ExplorerRow* parent, GNode* node) {
    for (GNode* c = node->children; c != NULL; c = c->next) {
        BookmarkItem* it = bm_item(c);
        if (it->is_folder) {
            ExplorerRow* row = explorer_row_append_container(parent, "bm_folder", it->name, it->name, "");
            explorer_row_set_glyph(row, "folder");
            guint n = g_node_n_children(c);
            if (n > 0) {
                gchar* badge = g_strdup_printf("<span foreground=\"%s\">%u</span>", palette_color(PAL_FG_FAINT), n);
                explorer_row_set_badge(row, badge);
                g_free(badge);
            }
            explorer_row_set_bm_id(row, it->id);
            if (c->children != NULL) {
                bm_append_children(app, row, c);
            } else {
                bm_append_placeholder(row, "  (empty)");
            }
        } else {
            bookmark_icon_check(app, it->target);
            gchar* loc = bm_display_location(it->target);
            gchar* badge = g_markup_printf_escaped("<span foreground=\"%s\" size=\"small\">%s</span>",
                                                   palette_color(PAL_FG_FAINT), loc);
            ExplorerRow* row = explorer_row_append(parent, "bookmark", it->name, it->name, it->target);
            bookmark_icon_apply(row);
            explorer_row_set_badge(row, badge);
            explorer_row_set_bm_id(row, it->id);
            g_free(badge);
            g_free(loc);
        }
    }
}

void bookmarks_append_rows(ExplorerRow* root, AppData* app) {
    if (root == NULL || app == NULL || app->bookmarks == NULL) {
        return;
    }
    if (app->bookmarks->children == NULL) {
        bm_append_placeholder(root, "No bookmarks yet — press a or Ctrl+D to add one");
        return;
    }
    bm_append_children(app, root, app->bookmarks);
}

static void bm_reexpand_level(AppData* app, ExplorerRow* parent) {
    for (guint i = 0; i < explorer_row_n_children(parent); i++) {
        ExplorerRow* child = explorer_row_child(parent, i);
        GNode* node = bm_find_by_id(app->bookmarks, child->bm_id);
        if (bm_is_folder(node) && bm_item(node)->expanded) {
            explorer_tree_set_expanded(app->tree, child, TRUE);
            bm_reexpand_level(app, child);
        }
    }
}

void bookmarks_reexpand(AppData* app) {
    if (app == NULL || app->tree == NULL || app->sidebar_view != SIDEBAR_VIEW_BOOKMARKS) {
        return;
    }
    bm_reexpand_level(app, explorer_tree_get_root(app->tree));
}

static gboolean bm_match_id(ExplorerRow* row, gpointer data) {
    return row->bm_id == GPOINTER_TO_UINT(data);
}

static ExplorerRow* bm_find_row_by_id(AppData* app, guint id) {
    if (id == 0) {
        return NULL;
    }
    return explorer_row_find(explorer_tree_get_root(app->tree), bm_match_id, GUINT_TO_POINTER(id));
}

static gboolean bm_select_id(AppData* app, guint id) {
    ExplorerRow* row = bm_find_row_by_id(app, id);
    if (row == NULL) {
        return FALSE;
    }
    explorer_tree_expand_to(app->tree, row);
    select_row(app, row);
    return TRUE;
}

static GNode* bm_selected_node(AppData* app) {
    if (app == NULL || app->tree == NULL || app->sidebar_view != SIDEBAR_VIEW_BOOKMARKS) {
        return NULL;
    }
    ExplorerRow* row = tree_nav_selected(app);
    return row != NULL ? bm_find_by_id(app->bookmarks, row->bm_id) : NULL;
}

/* Rebuild the bookmarks view synchronously and select the given item */
static void bm_refresh_select(AppData* app, guint select_id) {
    if (app == NULL || app->tree == NULL || app->sidebar_view != SIDEBAR_VIEW_BOOKMARKS) {
        if (app != NULL) {
            app_emit(app, APP_EVENT_CHROME_CHANGED);
        }
        return;
    }
    /* A pending directory scan would overwrite the bookmarks listing */
    if (app->dir_cancellable != NULL) {
        g_cancellable_cancel(app->dir_cancellable);
        g_clear_object(&app->dir_cancellable);
    }
    app->dir_scan_pending = FALSE;

    tree_nav_block_selection_handler(app);
    explorer_tree_clear(app->tree);
    bookmarks_append_rows(explorer_tree_get_root(app->tree), app);
    bm_reexpand_level(app, explorer_tree_get_root(app->tree));
    tree_nav_unblock_selection_handler(app);
    if (!bm_select_id(app, select_id)) {
        select_first_row(app);
    }
    app_emit(app, APP_EVENT_CHROME_CHANGED);
}

void bookmarks_refresh(AppData* app) {
    GNode* sel = bm_selected_node(app);
    bm_refresh_select(app, sel != NULL ? bm_item(sel)->id : 0);
}

gboolean bookmarks_handle_row_toggled(AppData* app, ExplorerRow* row, gboolean expanded) {
    if (app == NULL || !explorer_row_is(row, "bm_folder")) {
        return FALSE;
    }
    GNode* node = bm_find_by_id(app->bookmarks, row->bm_id);
    if (bm_is_folder(node)) {
        BookmarkItem* it = bm_item(node);
        if (it->expanded != expanded) {
            it->expanded = expanded;
            bookmarks_save(app);
        }
    }
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Dialogs                                                                   */
/* ------------------------------------------------------------------------- */

typedef struct {
    GMainLoop* loop;
    gint response;
    GtkWidget* name_entry;   /* required when validating a folder */
    GtkWidget* target_entry; /* required when validating a link */
} BmDialog;

static gboolean bm_entry_is_blank(GtkWidget* entry) {
    const gchar* text = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (text == NULL) {
        return TRUE;
    }
    for (const gchar* p = text; *p != '\0'; p++) {
        if (!g_ascii_isspace(*p)) {
            return FALSE;
        }
    }
    return TRUE;
}

static void bm_on_dialog_response(AppDialog* dlg, gint response_id, gpointer user_data) {
    (void)dlg;
    BmDialog* d = (BmDialog*)user_data;
    if (response_id == APP_RESPONSE_OK) {
        GtkWidget* required = (d->target_entry != NULL) ? d->target_entry : d->name_entry;
        if (required != NULL && bm_entry_is_blank(required)) {
            gtk_widget_add_css_class(required, "error");
            gtk_widget_grab_focus(required);
            return; /* keep the dialog open */
        }
    }
    d->response = response_id;
    if (g_main_loop_is_running(d->loop)) {
        g_main_loop_quit(d->loop);
    }
}

static gint bm_run_modal(GtkWidget* dlg, BmDialog* d, GtkWidget* focus) {
    d->loop = g_main_loop_new(NULL, FALSE);
    d->response = APP_RESPONSE_CANCEL;
    g_signal_connect(dlg, "response", G_CALLBACK(bm_on_dialog_response), d);
    app_dialog_present(APP_DIALOG(dlg));
    if (focus != NULL) {
        gtk_widget_grab_focus(focus);
    }
    g_main_loop_run(d->loop);
    g_main_loop_unref(d->loop);
    return d->response;
}

static GtkWidget* bm_dialog_body(GtkWidget* dlg) {
    GtkWidget* content = app_dialog_get_content_area(APP_DIALOG(dlg));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_box_append(GTK_BOX(content), box);
    return box;
}

/* Simple message / confirmation dialog; returns TRUE when ok_label was chosen */
static gboolean bm_message(AppData* app, const gchar* title, const gchar* message,
                           const gchar* ok_label, const gchar* cancel_label) {
    GtkWidget* dlg = app_dialog_new(title, app->window,
                                    NULL, NULL);
    if (cancel_label != NULL) {
        app_dialog_add_button(APP_DIALOG(dlg), cancel_label, APP_RESPONSE_CANCEL);
    }
    app_dialog_add_button(APP_DIALOG(dlg), ok_label, APP_RESPONSE_OK);
    app_dialog_set_default_response(APP_DIALOG(dlg), cancel_label != NULL ? APP_RESPONSE_CANCEL : APP_RESPONSE_OK);
    GtkWidget* box = bm_dialog_body(dlg);
    GtkWidget* label = gtk_label_new(message);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 60);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_box_append(GTK_BOX(box), label);

    BmDialog d = { NULL, APP_RESPONSE_CANCEL, NULL, NULL };
    gint resp = bm_run_modal(dlg, &d, NULL);
    app_dialog_destroy(APP_DIALOG(dlg));
    return resp == APP_RESPONSE_OK;
}

/* Folder chooser entries: "Top level" + every folder except `exclude` and its subtree */
static void bm_collect_folders(GNode* node, gint depth, GNode* exclude,
                               GPtrArray* nodes, GPtrArray* labels) {
    for (GNode* c = node->children; c != NULL; c = c->next) {
        if (!bm_is_folder(c) || c == exclude) {
            continue;
        }
        GString* s = g_string_new("");
        for (gint i = 0; i < depth; i++) {
            g_string_append(s, "    ");
        }
        g_string_append(s, bm_item(c)->name);
        g_ptr_array_add(labels, g_string_free(s, FALSE));
        g_ptr_array_add(nodes, c);
        bm_collect_folders(c, depth + 1, exclude, nodes, labels);
    }
}

static void bm_on_use_current_dir(GtkButton* btn, gpointer data) {
    GtkWidget* entry = GTK_WIDGET(data);
    const gchar* dir = (const gchar*)g_object_get_data(G_OBJECT(btn), "base-dir");
    if (dir != NULL) {
        gchar* shown = bm_display_location(dir);
        gtk_editable_set_text(GTK_EDITABLE(entry), shown);
        g_free(shown);
    }
}

static void bm_on_entry_changed(GtkEditable* editable, gpointer data) {
    (void)data;
    gtk_widget_remove_css_class(GTK_WIDGET(editable), "error");
}

static GtkWidget* bm_field_label(const gchar* text) {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "dim-label");
    return label;
}

/* Add / edit dialog for a bookmark (is_folder FALSE) or a folder (is_folder TRUE).
 * Returns APP_RESPONSE_OK (outputs filled), APP_RESPONSE_REJECT (remove requested)
 * or another response for cancel. */
static gint bm_run_item_dialog(AppData* app, const gchar* title, const gchar* ok_label,
                               gboolean is_folder, gboolean allow_remove,
                               const gchar* init_name, const gchar* init_target,
                               GNode* init_parent, GNode* exclude,
                               gchar** out_name, gchar** out_target, GNode** out_parent) {
    GtkWidget* dlg = app_dialog_new(title, app->window,
                                    NULL, NULL);
    if (allow_remove) {
        GtkWidget* rm = app_dialog_add_button(APP_DIALOG(dlg), "_Remove Bookmark", APP_RESPONSE_REJECT);
        gtk_widget_add_css_class(rm, "destructive-action");
    }
    app_dialog_add_button(APP_DIALOG(dlg), "_Cancel", APP_RESPONSE_CANCEL);
    GtkWidget* ok = app_dialog_add_button(APP_DIALOG(dlg), ok_label, APP_RESPONSE_OK);
    gtk_widget_add_css_class(ok, "suggested-action");
    app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_OK);
    app_dialog_set_width(APP_DIALOG(dlg), 480);

    GtkWidget* box = bm_dialog_body(dlg);

    /* Name */
    gtk_box_append(GTK_BOX(box), bm_field_label("Name"));
    GtkWidget* name_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry),
                                   is_folder ? "Folder name" : "Defaults to the location's name");
    if (init_name != NULL) {
        gtk_editable_set_text(GTK_EDITABLE(name_entry), init_name);
    }
    gtk_entry_set_activates_default(GTK_ENTRY(name_entry), TRUE);
    g_signal_connect(name_entry, "changed", G_CALLBACK(bm_on_entry_changed), NULL);
    gtk_box_append(GTK_BOX(box), name_entry);

    /* Location (links only) */
    GtkWidget* target_entry = NULL;
    if (!is_folder) {
        gtk_box_append(GTK_BOX(box), bm_field_label("Location"));
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        target_entry = gtk_entry_new();
        gtk_widget_set_hexpand(target_entry, TRUE);
        gtk_entry_set_placeholder_text(GTK_ENTRY(target_entry), "~/projects, /etc/hosts or https://…");
        if (init_target != NULL) {
            gchar* shown = bm_display_location(init_target);
            gtk_editable_set_text(GTK_EDITABLE(target_entry), shown);
            g_free(shown);
        }
        gtk_entry_set_activates_default(GTK_ENTRY(target_entry), TRUE);
        g_signal_connect(target_entry, "changed", G_CALLBACK(bm_on_entry_changed), NULL);
        gtk_box_append(GTK_BOX(row), target_entry);

        GtkWidget* cur_btn = gtk_button_new_with_label("Current Directory");
        gtk_widget_set_tooltip_text(cur_btn, app->base_dir);
        g_object_set_data_full(G_OBJECT(cur_btn), "base-dir", g_strdup(app->base_dir), g_free);
        g_signal_connect(cur_btn, "clicked", G_CALLBACK(bm_on_use_current_dir), target_entry);
        gtk_box_append(GTK_BOX(row), cur_btn);
        gtk_box_append(GTK_BOX(box), row);
    }

    /* Parent folder */
    gtk_box_append(GTK_BOX(box), bm_field_label(is_folder ? "Parent folder" : "Folder"));
    GPtrArray* nodes = g_ptr_array_new();
    GPtrArray* labels = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(nodes, app->bookmarks);
    g_ptr_array_add(labels, g_strdup("Bookmarks (top level)"));
    bm_collect_folders(app->bookmarks, 1, exclude, nodes, labels);
    guint selected = 0;
    for (guint i = 0; i < nodes->len; i++) {
        if (g_ptr_array_index(nodes, i) == init_parent) {
            selected = i;
            break;
        }
    }
    g_ptr_array_add(labels, NULL);
    GtkWidget* folder_dd = gtk_drop_down_new_from_strings((const char* const*)labels->pdata);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(folder_dd), selected);
    gtk_box_append(GTK_BOX(box), folder_dd);

    BmDialog d = { NULL, APP_RESPONSE_CANCEL, is_folder ? name_entry : NULL, target_entry };
    GtkWidget* focus = (!is_folder && init_target == NULL) ? target_entry : name_entry;
    gint resp = bm_run_modal(dlg, &d, focus);

    if (resp == APP_RESPONSE_OK) {
        guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(folder_dd));
        *out_parent = (idx < nodes->len) ? (GNode*)g_ptr_array_index(nodes, idx) : app->bookmarks;
        *out_target = (target_entry != NULL)
                          ? bm_normalize_target(gtk_editable_get_text(GTK_EDITABLE(target_entry)),
                                                app->base_dir)
                          : NULL;
        gchar* name = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(name_entry))));
        if (name[0] == '\0') {
            g_free(name);
            name = bm_default_name(*out_target);
        }
        *out_name = name;
    }

    g_ptr_array_unref(nodes);
    g_ptr_array_unref(labels);
    app_dialog_destroy(APP_DIALOG(dlg));
    return resp;
}

/* ------------------------------------------------------------------------- */
/* Actions                                                                   */
/* ------------------------------------------------------------------------- */

static guint bm_folder_id(GNode* folder) {
    return bm_is_folder(folder) ? bm_item(folder)->id : 0;
}

/* Folder where a new item should go: selected folder, parent of the selected link, or root */
static GNode* bm_context_folder(AppData* app) {
    GNode* sel = bm_selected_node(app);
    if (sel == NULL) {
        return app->bookmarks;
    }
    return bm_is_folder(sel) ? sel : sel->parent;
}

static void bm_after_change(AppData* app, guint select_id) {
    bookmarks_save(app);
    bm_refresh_select(app, select_id);
}

static void bm_add_dialog(AppData* app, const gchar* init_target, GNode* parent) {
    gchar* init_name = (init_target != NULL) ? bm_default_name(init_target) : NULL;
    gchar* name = NULL;
    gchar* target = NULL;
    GNode* out_parent = NULL;
    gint resp = bm_run_item_dialog(app, "Add Bookmark", "_Add", FALSE, FALSE,
                                   init_name, init_target, parent, NULL,
                                   &name, &target, &out_parent);
    if (resp == APP_RESPONSE_OK && target != NULL && out_parent != NULL) {
        GNode* node = bm_new_node(app, FALSE, name, target);
        g_node_append(out_parent, node);
        if (bm_is_folder(out_parent)) {
            bm_item(out_parent)->expanded = TRUE;
        }
        app->bm_last_folder_id = bm_folder_id(out_parent);
        bm_after_change(app, bm_item(node)->id);
    }
    g_free(init_name);
    g_free(name);
    g_free(target);
}

static void bm_delete_node(AppData* app, GNode* node, gboolean confirm) {
    if (node == NULL || node == app->bookmarks) {
        return;
    }
    BookmarkItem* it = bm_item(node);
    if (confirm) {
        /* Always confirm, like the other views: bookmarks have no undo or trash */
        gchar* msg = NULL;
        if (it->is_folder && node->children != NULL) {
            guint n = g_node_n_nodes(node, G_TRAVERSE_ALL) - 1;
            msg = g_strdup_printf("Delete the folder “%s” and the %u item%s it contains?",
                                  it->name, n, n == 1 ? "" : "s");
        } else if (it->is_folder) {
            msg = g_strdup_printf("Delete the empty folder “%s”?", it->name);
        } else {
            msg = g_strdup_printf("Delete the bookmark “%s”?", it->name);
        }
        gboolean ok = bm_message(app, it->is_folder ? "Delete Folder" : "Delete Bookmark", msg,
                                 "_Delete", "_Cancel");
        g_free(msg);
        if (!ok) {
            return;
        }
    }
    GNode* next = (node->next != NULL) ? node->next : node->prev;
    guint select_id = (next != NULL) ? bm_item(next)->id : bm_folder_id(node->parent);
    if (app->bm_last_folder_id != 0 && bm_find_by_id(node, app->bm_last_folder_id) != NULL) {
        app->bm_last_folder_id = 0;
    }
    if (it->is_folder && it->id == app->bm_last_folder_id) {
        app->bm_last_folder_id = 0;
    }
    g_node_unlink(node);
    bm_destroy_subtree(node);
    bm_after_change(app, select_id);
}

static void bm_edit_node(AppData* app, GNode* node, gboolean allow_remove) {
    BookmarkItem* it = bm_item(node);
    if (it == NULL) {
        return;
    }
    gchar* name = NULL;
    gchar* target = NULL;
    GNode* out_parent = NULL;
    gint resp = bm_run_item_dialog(app, it->is_folder ? "Edit Folder" : "Edit Bookmark", "_Save",
                                   it->is_folder, allow_remove, it->name, it->target,
                                   node->parent, it->is_folder ? node : NULL,
                                   &name, &target, &out_parent);
    if (resp == APP_RESPONSE_REJECT) {
        bm_delete_node(app, node, FALSE);
    } else if (resp == APP_RESPONSE_OK) {
        g_free(it->name);
        it->name = name;
        name = NULL;
        if (!it->is_folder && target != NULL) {
            g_free(it->target);
            it->target = target;
            target = NULL;
        }
        if (out_parent != NULL && out_parent != node->parent) {
            g_node_unlink(node);
            g_node_append(out_parent, node);
            if (bm_is_folder(out_parent)) {
                bm_item(out_parent)->expanded = TRUE;
            }
        }
        bm_after_change(app, it->id);
    }
    g_free(name);
    g_free(target);
}

void bookmarks_add(AppData* app) {
    if (app == NULL) {
        return;
    }
    bm_add_dialog(app, app->base_dir, bm_context_folder(app));
}

/* Ctrl+D: in the files view, bookmark the selected entry (or the current
 * directory); an existing bookmark for it opens in the edit dialog instead,
 * where it can also be removed. In the bookmarks view, add a new bookmark. */
void bookmarks_add_from_context(AppData* app) {
    if (app == NULL || app->bookmarks == NULL) {
        return;
    }
    if (app->sidebar_view == SIDEBAR_VIEW_BOOKMARKS) {
        bookmarks_add(app);
        return;
    }
    gchar* target = NULL;
    if (app->sidebar_view == SIDEBAR_VIEW_FILES) {
        ExplorerRow* row = tree_nav_selected(app);
        if ((explorer_row_is(row, "dir") || explorer_row_is(row, "file")) && row->path[0] != '\0') {
            target = g_strdup(row->path);
        }
    }
    if (target == NULL) {
        target = g_strdup(app->base_dir);
    }
    if (target == NULL) {
        return;
    }
    GNode* existing = bm_find_link_by_target(app->bookmarks, target);
    if (existing != NULL) {
        bm_edit_node(app, existing, TRUE);
    } else {
        GNode* parent = bm_find_by_id(app->bookmarks, app->bm_last_folder_id);
        bm_add_dialog(app, target, parent != NULL ? parent : app->bookmarks);
    }
    g_free(target);
}

void bookmarks_new_folder(AppData* app) {
    if (app == NULL || app->bookmarks == NULL) {
        return;
    }
    gchar* name = NULL;
    gchar* target = NULL;
    GNode* out_parent = NULL;
    gint resp = bm_run_item_dialog(app, "New Bookmark Folder", "_Create", TRUE, FALSE,
                                   NULL, NULL, bm_context_folder(app), NULL,
                                   &name, &target, &out_parent);
    if (resp == APP_RESPONSE_OK && name != NULL && name[0] != '\0' && out_parent != NULL) {
        GNode* node = bm_new_node(app, TRUE, name, NULL);
        bm_item(node)->expanded = TRUE;
        g_node_append(out_parent, node);
        if (bm_is_folder(out_parent)) {
            bm_item(out_parent)->expanded = TRUE;
        }
        bm_after_change(app, bm_item(node)->id);
    }
    g_free(name);
    g_free(target);
}

void bookmarks_edit_selected(AppData* app) {
    GNode* node = bm_selected_node(app);
    if (node != NULL) {
        bm_edit_node(app, node, FALSE);
    }
}

void bookmarks_delete_selected(AppData* app) {
    GNode* node = bm_selected_node(app);
    if (node != NULL) {
        bm_delete_node(app, node, TRUE);
    }
}

void bookmarks_move_selected(AppData* app, gint delta) {
    GNode* node = bm_selected_node(app);
    if (node == NULL) {
        return;
    }
    GNode* parent = node->parent;
    if (delta < 0 && node->prev != NULL) {
        GNode* sibling = node->prev;
        g_node_unlink(node);
        g_node_insert_before(parent, sibling, node);
    } else if (delta > 0 && node->next != NULL) {
        GNode* sibling = node->next;
        g_node_unlink(node);
        g_node_insert_after(parent, sibling, node);
    } else {
        return;
    }
    bm_after_change(app, bm_item(node)->id);
}

static gint bm_compare(gconstpointer a, gconstpointer b, gpointer user_data) {
    BookmarkItem* ia = bm_item(*(GNode* const*)a);
    BookmarkItem* ib = bm_item(*(GNode* const*)b);
    if (ia->is_folder != ib->is_folder) {
        return ia->is_folder ? -1 : 1; /* folders first */
    }
    gchar* ka = g_utf8_casefold(ia->name, -1);
    gchar* kb = g_utf8_casefold(ib->name, -1);
    gint res = g_utf8_collate(ka, kb);
    g_free(ka);
    g_free(kb);
    return GPOINTER_TO_INT(user_data) ? -res : res;
}

static void bm_sort_children(GNode* node, gboolean desc) {
    GPtrArray* kids = g_ptr_array_new();
    while (node->children != NULL) {
        GNode* c = node->children;
        g_node_unlink(c);
        g_ptr_array_add(kids, c);
    }
    g_ptr_array_sort_with_data(kids, bm_compare, GINT_TO_POINTER(desc ? 1 : 0));
    for (guint i = 0; i < kids->len; i++) {
        GNode* c = (GNode*)g_ptr_array_index(kids, i);
        g_node_append(node, c);
        if (bm_is_folder(c)) {
            bm_sort_children(c, desc);
        }
    }
    g_ptr_array_unref(kids);
}

void bookmarks_toggle_sort(AppData* app) {
    if (app == NULL || app->bookmarks == NULL) {
        return;
    }
    GNode* sel = bm_selected_node(app);
    guint sel_id = (sel != NULL) ? bm_item(sel)->id : 0;
    app->bookmarks_sort_desc = !app->bookmarks_sort_desc;
    bm_sort_children(app->bookmarks, app->bookmarks_sort_desc);
    bm_after_change(app, sel_id);
}

static gboolean bm_any_expanded(GNode* node) {
    for (GNode* c = node->children; c != NULL; c = c->next) {
        if (bm_is_folder(c) && (bm_item(c)->expanded || bm_any_expanded(c))) {
            return TRUE;
        }
    }
    return FALSE;
}

static void bm_set_expanded_all(GNode* node, gboolean expanded) {
    for (GNode* c = node->children; c != NULL; c = c->next) {
        if (bm_is_folder(c)) {
            bm_item(c)->expanded = expanded;
            bm_set_expanded_all(c, expanded);
        }
    }
}

void bookmarks_toggle_collapse_expand(AppData* app) {
    if (app == NULL || app->bookmarks == NULL) {
        return;
    }
    gboolean expand = !bm_any_expanded(app->bookmarks);
    bm_set_expanded_all(app->bookmarks, expand);
    GNode* sel = bm_selected_node(app);
    /* Keep a visible row selected when collapsing */
    while (!expand && sel != NULL && sel->parent != NULL && sel->parent != app->bookmarks) {
        sel = sel->parent;
    }
    bm_after_change(app, sel != NULL ? bm_item(sel)->id : 0);
}

void bookmarks_activate_selected(AppData* app) {
    GNode* node = bm_selected_node(app);
    BookmarkItem* it = bm_item(node);
    if (it == NULL) {
        return;
    }
    if (it->is_folder) {
        explorer_tree_toggle_expanded(app->tree, bm_find_row_by_id(app, it->id));
        return;
    }

    if (bookmark_target_is_uri(it->target)) {
        GError* err = NULL;
        if (!g_app_info_launch_default_for_uri(it->target, NULL, &err)) {
            gchar* msg = g_strdup_printf("Could not open “%s”:\n%s", it->target,
                                         err != NULL ? err->message : "unknown error");
            bm_message(app, "Open Bookmark", msg, "_Close", NULL);
            g_free(msg);
        }
        g_clear_error(&err);
        return;
    }

    if (!g_file_test(it->target, G_FILE_TEST_EXISTS)) {
        gchar* msg = g_strdup_printf("The location of “%s” no longer exists:\n%s\n\nRemove this bookmark?",
                                     it->name, it->target);
        if (bm_message(app, "Missing Location", msg, "_Remove", "_Keep")) {
            bm_delete_node(app, node, FALSE);
        }
        g_free(msg);
        return;
    }

    gchar* target = g_strdup(it->target);
    app->sidebar_view = SIDEBAR_VIEW_FILES;
    app->show_bookmarks = FALSE;
    navigate_to_path(app, target);
    g_free(target);
}

/* Keys specific to the bookmarks view; returns TRUE when consumed */
gboolean bookmarks_handle_key(AppData* app, guint keyval, GdkModifierType state) {
    if (app == NULL || app->sidebar_view != SIDEBAR_VIEW_BOOKMARKS) {
        return FALSE;
    }
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    if ((state & GDK_ALT_MASK) != 0) {
        return FALSE;
    }
    switch (keyval) {
    case GDK_KEY_a:
        if (ctrl) {
            return FALSE; /* Ctrl+A: switch view */
        }
        bookmarks_add(app);
        break;
    case GDK_KEY_A:
        if (ctrl) {
            return FALSE;
        }
        bookmarks_new_folder(app);
        break;
    case GDK_KEY_e:
    case GDK_KEY_r:
    case GDK_KEY_R:
    case GDK_KEY_F2:
        bookmarks_edit_selected(app);
        break;
    case GDK_KEY_d:
    case GDK_KEY_D:
        if (ctrl) {
            bookmarks_add(app);
        } else {
            bookmarks_delete_selected(app);
        }
        break;
    case GDK_KEY_Delete:
    case GDK_KEY_KP_Delete:
        bookmarks_delete_selected(app);
        break;
    case GDK_KEY_J:
        if (ctrl) {
            return FALSE;
        }
        bookmarks_move_selected(app, +1);
        break;
    case GDK_KEY_K:
        if (ctrl) {
            return FALSE;
        }
        bookmarks_move_selected(app, -1);
        break;
    default:
        return FALSE;
    }
    app->nav_prefix = 0; /* NAV_PREFIX_NONE */
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Right-click context menu                                                  */
/* ------------------------------------------------------------------------- */

typedef struct {
    AppData* app;
    gint action;
} BmMenuAction;

static gboolean bm_run_menu_action(gpointer data) {
    BmMenuAction* a = (BmMenuAction*)data;
    AppData* app = a->app;
    switch (a->action) {
    case BM_ACTION_OPEN:       bookmarks_activate_selected(app); break;
    case BM_ACTION_ADD:        bookmarks_add(app); break;
    case BM_ACTION_ADD_FOLDER: bookmarks_new_folder(app); break;
    case BM_ACTION_EDIT:       bookmarks_edit_selected(app); break;
    case BM_ACTION_DELETE:     bookmarks_delete_selected(app); break;
    case BM_ACTION_MOVE_UP:    bookmarks_move_selected(app, -1); break;
    case BM_ACTION_MOVE_DOWN:  bookmarks_move_selected(app, +1); break;
    case BM_ACTION_SORT:       bookmarks_toggle_sort(app); break;
    case BM_ACTION_COLLAPSE:   bookmarks_toggle_collapse_expand(app); break;
    default: break;
    }
    g_free(a);
    return G_SOURCE_REMOVE;
}

static void bm_on_menu_item_clicked(GtkButton* btn, gpointer data) {
    GtkWidget* popover = GTK_WIDGET(data);
    BmMenuAction* a = g_new0(BmMenuAction, 1);
    a->app = (AppData*)g_object_get_data(G_OBJECT(btn), "app");
    a->action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "action"));
    gtk_popover_popdown(GTK_POPOVER(popover));
    /* Run after the popover is gone: actions may open modal dialogs */
    g_idle_add(bm_run_menu_action, a);
}

static void bm_menu_item(GtkWidget* box, GtkWidget* popover, AppData* app,
                         const gchar* text, const gchar* hint, gint action, gboolean sensitive) {
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
    gtk_widget_set_sensitive(btn, sensitive);
    g_object_set_data(G_OBJECT(btn), "app", app);
    g_object_set_data(G_OBJECT(btn), "action", GINT_TO_POINTER(action));
    g_signal_connect(btn, "clicked", G_CALLBACK(bm_on_menu_item_clicked), popover);
    gtk_box_append(GTK_BOX(box), btn);
}

static void bm_menu_separator(GtkWidget* box) {
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep, 3);
    gtk_widget_set_margin_bottom(sep, 3);
    gtk_box_append(GTK_BOX(box), sep);
}

static gboolean bm_unparent_idle(gpointer data) {
    gtk_widget_unparent(GTK_WIDGET(data));
    g_object_unref(data);
    return G_SOURCE_REMOVE;
}

static void bm_on_popover_closed(GtkPopover* popover, gpointer data) {
    (void)data;
    g_idle_add(bm_unparent_idle, g_object_ref(popover));
}

static void bm_on_right_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    AppData* app = (AppData*)data;
    if (app == NULL || app->sidebar_view != SIDEBAR_VIEW_BOOKMARKS) {
        return;
    }
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);

    ExplorerRow* row = explorer_tree_row_at(app->tree, x, y);
    if (row != NULL) {
        select_row(app, row);
    } else {
        explorer_tree_unselect_all(app->tree);
    }

    GNode* node = bm_selected_node(app);
    GtkWidget* popover = gtk_popover_new();
    gtk_widget_set_parent(popover, GTK_WIDGET(app->tree_view));
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    GdkRectangle rect = { (gint)x, (gint)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_widget_set_halign(popover, GTK_ALIGN_START);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    if (node != NULL) {
        gboolean folder = bm_is_folder(node);
        if (folder) {
            bm_menu_item(box, popover, app, "Add Bookmark Here…", "a", BM_ACTION_ADD, TRUE);
            bm_menu_item(box, popover, app, "New Subfolder…", "Shift+A", BM_ACTION_ADD_FOLDER, TRUE);
            bm_menu_separator(box);
            bm_menu_item(box, popover, app, "Rename / Move…", "F2", BM_ACTION_EDIT, TRUE);
            bm_menu_item(box, popover, app, "Delete Folder", "Delete", BM_ACTION_DELETE, TRUE);
        } else {
            bm_menu_item(box, popover, app, "Open", "Enter", BM_ACTION_OPEN, TRUE);
            bm_menu_item(box, popover, app, "Edit…", "F2", BM_ACTION_EDIT, TRUE);
            bm_menu_item(box, popover, app, "Delete", "Delete", BM_ACTION_DELETE, TRUE);
        }
        bm_menu_separator(box);
        bm_menu_item(box, popover, app, "Move Up", "Shift+K", BM_ACTION_MOVE_UP, node->prev != NULL);
        bm_menu_item(box, popover, app, "Move Down", "Shift+J", BM_ACTION_MOVE_DOWN, node->next != NULL);
        if (!folder) {
            bm_menu_separator(box);
            bm_menu_item(box, popover, app, "Add Bookmark…", "a", BM_ACTION_ADD, TRUE);
            bm_menu_item(box, popover, app, "New Folder…", "Shift+A", BM_ACTION_ADD_FOLDER, TRUE);
        }
    } else {
        bm_menu_item(box, popover, app, "Add Bookmark…", "a", BM_ACTION_ADD, TRUE);
        bm_menu_item(box, popover, app, "New Folder…", "Shift+A", BM_ACTION_ADD_FOLDER, TRUE);
    }
    bm_menu_separator(box);
    bm_menu_item(box, popover, app, "Sort A–Z / Z–A", "o", BM_ACTION_SORT, app->bookmarks->children != NULL);
    bm_menu_item(box, popover, app, "Collapse / Expand All", NULL, BM_ACTION_COLLAPSE, app->bookmarks->children != NULL);

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    g_signal_connect(popover, "closed", G_CALLBACK(bm_on_popover_closed), NULL);
    gtk_popover_popup(GTK_POPOVER(popover));
}

void bookmarks_attach_context_menu(AppData* app) {
    if (app == NULL || app->tree_view == NULL) {
        return;
    }
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
    g_signal_connect(click, "pressed", G_CALLBACK(bm_on_right_click), app);
    gtk_widget_add_controller(GTK_WIDGET(app->tree_view), GTK_EVENT_CONTROLLER(click));
}
