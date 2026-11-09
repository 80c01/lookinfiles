/* Quick open of a project file (see quick_open.h) */
#include "search/quick_open.h"
#include "core/app_events.h"
#include "code/code_index.h"
#include "widgets/dialog.h"
#include "editor/editor.h"
#include "explorer/file_list.h"
#include "widgets/focus_route.h"
#include "search/fuzzy_match.h"
#include "theme/icons.h"
#include "core/i18n.h"
#include "explorer/nav_history.h"
#include "theme/palette.h"
#include "explorer/tree_nav.h"

#include <string.h>

#define RESPONSE_SHOW 1
#define RESPONSE_EDIT 2
#define MAX_SHOWN 200

typedef struct {
    AppData* app;
    GtkWidget* dialog;
    GtkWidget* entry;
    GtkWidget* list;
    GtkWidget* note;
    GtkStringList* shown;     /* relative paths of the best matches */
    GtkSingleSelection* selection;
    GCancellable* cancellable;
    gchar* root;
    GPtrArray* files;         /* relative paths (recent: absolute), NULL while loading */
    gboolean truncated;
    gchar* query;
    gboolean recent_mode;     /* Alt+R: recent folders and files */
    GHashTable* folders;      /* recent mode: the entries that are folders */
    GHashTable* recent_rank;  /* project mode: relative path -> 1 + rank in the recent files */
} Picker;

static void picker_free(Picker* p) {
    g_cancellable_cancel(p->cancellable);
    g_object_unref(p->cancellable);
    if (p->files != NULL) g_ptr_array_unref(p->files);
    if (p->folders != NULL) g_hash_table_destroy(p->folders);
    if (p->recent_rank != NULL) g_hash_table_destroy(p->recent_rank);
    g_free(p->root);
    g_free(p->query);
    g_free(p);
}

typedef struct {
    gint score;
    guint index;
} Scored;

static gint compare_scored(gconstpointer a, gconstpointer b) {
    const Scored* x = a;
    const Scored* y = b;
    if (x->score != y->score) return y->score - x->score;
    return (gint)x->index - (gint)y->index;
}

static void update_note(Picker* p, guint matches) {
    gchar* text;
    if (p->files == NULL) {
        text = g_strdup(_("Listing the project files…"));
    } else if (p->recent_mode) {
        text = g_strdup_printf(_("%1$u of %2$u recent places · Enter opens a folder or shows a file, "
                                 "Ctrl+Enter edits a file"),
                               matches, p->files->len);
    } else {
        text = g_strdup_printf(p->truncated ? _("%1$u of %2$u files (list cut) · Enter shows it, Ctrl+Enter edits it")
                                            : _("%1$u of %2$u files · Enter shows it, Ctrl+Enter edits it"),
                               matches, p->files->len);
    }
    gtk_label_set_text(GTK_LABEL(p->note), text);
    g_free(text);
}

/* Best matches of the query, best first */
static void refilter(Picker* p) {
    if (p->files == NULL) {
        update_note(p, 0);
        return;
    }
    gboolean empty = p->query == NULL || p->query[0] == '\0';
    GArray* scored = g_array_new(FALSE, FALSE, sizeof(Scored));
    for (guint i = 0; i < p->files->len; i++) {
        const gchar* path = g_ptr_array_index(p->files, i);
        gint score = fuzzy_match_score(p->query, path);
        if (score < 0) continue;
        if (empty && p->recent_mode) {
            score = 0; /* most recent first: the list order */
        } else if (empty && p->recent_rank != NULL) {
            /* Nothing typed: the recently opened files first */
            gint rank = GPOINTER_TO_INT(g_hash_table_lookup(p->recent_rank, path));
            if (rank > 0) score = G_MAXINT / 2 - rank;
        }
        Scored s = { score, i };
        g_array_append_val(scored, s);
    }
    g_array_sort(scored, compare_scored);
    guint n = MIN(scored->len, MAX_SHOWN);
    const gchar** paths = g_new0(const gchar*, n + 1);
    for (guint i = 0; i < n; i++) {
        paths[i] = g_ptr_array_index(p->files, g_array_index(scored, Scored, i).index);
    }
    guint old = g_list_model_get_n_items(G_LIST_MODEL(p->shown));
    gtk_string_list_splice(p->shown, 0, old, paths);
    g_free(paths);
    if (n > 0) {
        gtk_single_selection_set_selected(p->selection, 0);
        gtk_list_view_scroll_to(GTK_LIST_VIEW(p->list), 0, GTK_LIST_SCROLL_NONE, NULL);
    }
    update_note(p, scored->len);
    g_array_unref(scored);
}

static void on_search_changed(GtkSearchEntry* entry, gpointer data) {
    Picker* p = data;
    g_free(p->query);
    p->query = g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry)));
    refilter(p);
}

/* ------------------------------------------------------------------------- */
/* Listing the project in a worker                                           */
/* ------------------------------------------------------------------------- */

typedef struct {
    Picker* picker;           /* only touched when not cancelled */
    GCancellable* cancellable;
    gchar* root;
    GPtrArray* files;
    gboolean truncated;
} Listing;

static void listing_free(gpointer data) {
    Listing* l = data;
    g_object_unref(l->cancellable);
    g_free(l->root);
    if (l->files != NULL) g_ptr_array_unref(l->files);
    g_free(l);
}

static void listing_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)task;
    (void)source;
    Listing* l = data;
    GPtrArray* abs = code_index_all_files(l->root, cancellable, &l->truncated);
    gsize n = strlen(l->root);
    l->files = g_ptr_array_new_full(abs->len, g_free);
    for (guint i = 0; i < abs->len; i++) {
        const gchar* path = g_ptr_array_index(abs, i);
        gboolean under = strncmp(path, l->root, n) == 0 && path[n] == '/';
        g_ptr_array_add(l->files, g_strdup(under ? path + n + 1 : path));
    }
    g_ptr_array_unref(abs);
}

static void listing_done(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    (void)data;
    Listing* l = g_task_get_task_data(G_TASK(res));
    if (g_cancellable_is_cancelled(l->cancellable)) return; /* the dialog is gone */
    Picker* p = l->picker;
    p->files = l->files;
    p->truncated = l->truncated;
    l->files = NULL;
    /* Recent files of this project, by rank */
    p->recent_rank = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    const RecentList* recent = nav_history_recent(p->app);
    gsize n = strlen(p->root);
    for (guint i = 0; i < recent->files->len; i++) {
        const gchar* path = g_ptr_array_index(recent->files, i);
        if (strncmp(path, p->root, n) == 0 && path[n] == '/') {
            g_hash_table_insert(p->recent_rank, g_strdup(path + n + 1), GINT_TO_POINTER((gint)i + 1));
        }
    }
    refilter(p);
}

/* ------------------------------------------------------------------------- */
/* Rows                                                                      */
/* ------------------------------------------------------------------------- */

/* text[from..to) as markup, the bytes at positions in bold accent */
static void append_highlighted(GString* out, const gchar* text, gsize from, gsize to, const gsize* positions) {
    const gchar* accent = palette_color(PAL_ACCENT_FG);
    const gchar* c = text + from;
    const gchar* end = text + to;
    while (c < end) {
        const gchar* next = g_utf8_find_next_char(c, end);
        if (next == NULL) next = end;
        gboolean hit = FALSE;
        for (const gsize* q = positions; q != NULL && *q != G_MAXSIZE; q++) {
            if (*q >= (gsize)(c - text) && *q < (gsize)(next - text)) hit = TRUE;
        }
        gchar* esc = g_markup_escape_text(c, next - c);
        if (hit) {
            g_string_append_printf(out, "<span foreground=\"%s\"><b>%s</b></span>", accent, esc);
        } else {
            g_string_append(out, esc);
        }
        g_free(esc);
        c = next;
    }
}

static void on_setup(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    (void)data;
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);
    gtk_box_append(GTK_BOX(box), file_type_image_new());
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_START);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(box), label);
    gtk_list_item_set_child(item, box);
}

/* File-type icon, then "name  folder", the matched characters highlighted.
 * Folders get the folder glyph: in this flat list they need one. */
static void on_bind(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    Picker* p = data;
    const gchar* rel = gtk_string_object_get_string(gtk_list_item_get_item(item));
    gsize* pos = (p->query != NULL && p->query[0] != '\0') ? fuzzy_match_positions(p->query, rel) : NULL;
    const gchar* slash = strrchr(rel, '/');
    gsize base = slash != NULL ? (gsize)(slash - rel) + 1 : 0;
    gsize len = strlen(rel);

    GtkWidget* row = gtk_list_item_get_child(item);
    gboolean is_folder = p->folders != NULL && g_hash_table_contains(p->folders, rel);
    file_type_image_set(gtk_widget_get_first_child(row), rel + base, is_folder);
    GString* markup = g_string_new(NULL);
    append_highlighted(markup, rel, base, len, pos);
    if (base > 0) {
        g_string_append_printf(markup, "  <span foreground=\"%s\">", palette_color(PAL_FG_SUBTLE));
        append_highlighted(markup, rel, 0, base - 1, pos);
        g_string_append(markup, "</span>");
    }
    gtk_label_set_markup(GTK_LABEL(gtk_widget_get_last_child(row)), markup->str);
    g_string_free(markup, TRUE);
    g_free(pos);
}

/* ------------------------------------------------------------------------- */
/* Choosing                                                                  */
/* ------------------------------------------------------------------------- */

/* Show the preview panel if hidden, switch to its Preview tab and focus it */
static void focus_preview_tab(AppData* app) {
    if (app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
        app_layout_begin(app);
        gtk_widget_set_visible(app->viewer_box, TRUE);
        app_emit(app, APP_EVENT_CHROME_CHANGED);
        app_emit(app, APP_EVENT_LAYOUT_REBUILT);
    }
    if (app->notebook == NULL) return;
    gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
    focus_route_focus_page(app, gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), 0));
}

static void choose(Picker* p, gboolean edit) {
    GtkStringObject* obj = gtk_single_selection_get_selected_item(p->selection);
    if (obj == NULL) return;
    const gchar* rel = gtk_string_object_get_string(obj);
    gchar* path = g_path_is_absolute(rel) || p->root == NULL ? g_strdup(rel) : g_build_filename(p->root, rel, NULL);
    AppData* app = p->app;
    gboolean is_folder = p->folders != NULL && g_hash_table_contains(p->folders, rel);
    app_dialog_destroy(APP_DIALOG(p->dialog)); /* frees p, and rel */
    if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
        gchar* msg = g_strdup_printf(_("“%s” no longer exists"), path);
        app_notice(app, msg, FALSE);
        g_free(msg);
        g_free(path);
        return;
    }
    if (is_folder) {
        /* A recent folder becomes the explorer's folder */
        explorer_set_view(app, SIDEBAR_VIEW_FILES);
        reload_directory(app, path);
        if (app->tree_view != NULL) gtk_widget_grab_focus(app->tree_view);
        g_free(path);
        return;
    }
    nav_history_file_opened(app, path);
    reveal_path_in_tree(app, path);
    if (edit) {
        gchar* name = g_path_get_basename(path);
        editor_open_file(app, name, path);
        g_free(name);
    } else {
        focus_preview_tab(app);
    }
    g_free(path);
}

static void on_response(AppDialog* dialog, gint response, gpointer data) {
    Picker* p = data;
    if (response == RESPONSE_SHOW || response == RESPONSE_EDIT) {
        choose(p, response == RESPONSE_EDIT);
        return;
    }
    app_dialog_destroy(dialog);
}

static void on_activate_row(GtkListView* view, guint position, gpointer data) {
    (void)view;
    Picker* p = data;
    gtk_single_selection_set_selected(p->selection, position);
    choose(p, FALSE);
}

static void move_selection(Picker* p, gint delta) {
    guint n = g_list_model_get_n_items(G_LIST_MODEL(p->shown));
    if (n == 0) return;
    guint cur = gtk_single_selection_get_selected(p->selection);
    gint next = cur == GTK_INVALID_LIST_POSITION ? 0 : (gint)cur + delta;
    next = CLAMP(next, 0, (gint)n - 1);
    gtk_single_selection_set_selected(p->selection, (guint)next);
    gtk_list_view_scroll_to(GTK_LIST_VIEW(p->list), (guint)next, GTK_LIST_SCROLL_NONE, NULL);
}

/* Keys of the entry: Up / Down (also Ctrl+N / Ctrl+P, Ctrl+J / Ctrl+K),
 * Page Up / Down move in the list; Enter shows, Ctrl+Enter edits; Escape
 * cancels (the search entry would otherwise swallow it) */
static gboolean on_entry_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType state,
                             gpointer data) {
    (void)ctrl;
    (void)keycode;
    Picker* p = data;
    gboolean ctrl_down = (state & GDK_CONTROL_MASK) != 0;
    switch (keyval) {
    case GDK_KEY_Down: case GDK_KEY_KP_Down: move_selection(p, 1); return TRUE;
    case GDK_KEY_Up: case GDK_KEY_KP_Up: move_selection(p, -1); return TRUE;
    case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down: move_selection(p, 10); return TRUE;
    case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up: move_selection(p, -10); return TRUE;
    case GDK_KEY_Return: case GDK_KEY_KP_Enter: case GDK_KEY_ISO_Enter:
        choose(p, ctrl_down);
        return TRUE;
    case GDK_KEY_Escape:
        app_dialog_response(APP_DIALOG(p->dialog), APP_RESPONSE_CANCEL);
        return TRUE;
    default: break;
    }
    if (ctrl_down) {
        switch (gdk_keyval_to_lower(keyval)) {
        case GDK_KEY_n: case GDK_KEY_j: move_selection(p, 1); return TRUE;
        case GDK_KEY_p: case GDK_KEY_k: move_selection(p, -1); return TRUE;
        default: break;
        }
    }
    return FALSE;
}

/* The dialog, its list empty */
static Picker* picker_new(AppData* app, const gchar* title, const gchar* placeholder) {
    Picker* p = g_new0(Picker, 1);
    p->app = app;
    p->cancellable = g_cancellable_new();
    p->query = g_strdup("");
    p->dialog = app_dialog_new(title, app->window, _("_Cancel"), APP_RESPONSE_CANCEL, _("_Edit"), RESPONSE_EDIT,
                               _("_Open"), RESPONSE_SHOW, NULL);
    app_dialog_set_width(APP_DIALOG(p->dialog), 640);
    app_dialog_set_default_response(APP_DIALOG(p->dialog), RESPONSE_SHOW);
    GtkWidget* area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(area, 14);
    gtk_widget_set_margin_bottom(area, 14);
    gtk_widget_set_margin_start(area, 16);
    gtk_widget_set_margin_end(area, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(p->dialog))), area);

    p->entry = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(p->entry), placeholder);
    gtk_search_entry_set_search_delay(GTK_SEARCH_ENTRY(p->entry), 60);
    g_signal_connect(p->entry, "search-changed", G_CALLBACK(on_search_changed), p);
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_entry_key), p);
    gtk_widget_add_controller(p->entry, keys);
    gtk_box_append(GTK_BOX(area), p->entry);

    p->shown = gtk_string_list_new(NULL);
    p->selection = gtk_single_selection_new(G_LIST_MODEL(p->shown)); /* takes p->shown */
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_setup), p);
    g_signal_connect(factory, "bind", G_CALLBACK(on_bind), p);
    p->list = gtk_list_view_new(GTK_SELECTION_MODEL(p->selection), factory);
    g_signal_connect(p->list, "activate", G_CALLBACK(on_activate_row), p);
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 340);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), p->list);
    gtk_widget_add_css_class(scroll, "dialog-list");
    gtk_widget_set_margin_top(scroll, 6);
    gtk_box_append(GTK_BOX(area), scroll);

    p->note = gtk_label_new(NULL);
    gtk_widget_add_css_class(p->note, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(p->note), 0.0f);
    gtk_widget_set_margin_top(p->note, 4);
    gtk_box_append(GTK_BOX(area), p->note);
    update_note(p, 0);

    g_signal_connect(p->dialog, "response", G_CALLBACK(on_response), p);
    g_object_set_data_full(G_OBJECT(p->dialog), "picker", p, (GDestroyNotify)picker_free);
    app_dialog_present(APP_DIALOG(p->dialog));
    gtk_widget_grab_focus(p->entry);
    return p;
}

void quick_open_show(AppData* app) {
    if (app == NULL || app->window == NULL || app->base_dir == NULL || app_dialog_is_open(app->window)) {
        return;
    }
    gchar* root = code_index_project_root(app->base_dir);
    gchar* root_name = g_path_get_basename(root);
    gchar* title = g_strdup_printf(_("Go to File in %s"), root_name);
    Picker* p = picker_new(app, title, _("Type part of a file name or path"));
    p->root = root;
    g_free(title);
    g_free(root_name);

    Listing* l = g_new0(Listing, 1);
    l->picker = p;
    l->cancellable = g_object_ref(p->cancellable);
    l->root = g_strdup(p->root);
    GTask* task = g_task_new(NULL, p->cancellable, listing_done, NULL);
    g_task_set_task_data(task, l, listing_free);
    g_task_run_in_thread(task, listing_thread);
    g_object_unref(task);
}

void quick_open_show_recent(AppData* app) {
    if (app == NULL || app->window == NULL || app_dialog_is_open(app->window)) {
        return;
    }
    Picker* p = picker_new(app, _("Recent Folders and Files"), _("Type part of a folder or file name"));
    p->recent_mode = TRUE;
    p->folders = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    p->files = g_ptr_array_new_with_free_func(g_free);
    const RecentList* recent = nav_history_recent(app);
    for (guint i = 0; i < recent->folders->len; i++) {
        const gchar* dir = g_ptr_array_index(recent->folders, i);
        if (g_strcmp0(dir, app->base_dir) == 0) continue; /* the folder shown */
        g_ptr_array_add(p->files, g_strdup(dir));
        g_hash_table_add(p->folders, g_strdup(dir));
    }
    for (guint i = 0; i < recent->files->len; i++) {
        g_ptr_array_add(p->files, g_strdup(g_ptr_array_index(recent->files, i)));
    }
    refilter(p);
}
