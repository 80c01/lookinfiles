/* Problems tab (LSP diagnostics) and diagnostic stepping (see code_problems.h) */
#include "code/code_problems.h"
#include "code/code_index.h"
#include "code/code_intel.h"
#include "core/i18n.h"
#include "editor/editor.h"
#include "git/git_tab.h"
#include "theme/icons.h"
#include "theme/palette.h"
#include "theme/theme.h"
#include "widgets/focus_route.h"
#include <string.h>

#define PAGE_KEY "code-problems-page"

#define CODE_TYPE_PROB_ITEM (code_prob_item_get_type())
G_DECLARE_FINAL_TYPE(CodeProbItem, code_prob_item, CODE, PROB_ITEM, GObject)

struct _CodeProbItem {
    GObject parent;
    gboolean header;
    gchar* path;
    gchar* shown;       /* relative path for header */
    gchar* message;     /* single-line diagnostic text */
    gchar* source;      /* e.g. "clang" */
    LspSeverity severity;
    gint line;          /* 0-based */
    gint column;        /* byte index */
    guint count;        /* header item count */
};

G_DEFINE_FINAL_TYPE(CodeProbItem, code_prob_item, G_TYPE_OBJECT)

static void code_prob_item_finalize(GObject* obj) {
    CodeProbItem* it = CODE_PROB_ITEM(obj);
    g_free(it->path);
    g_free(it->shown);
    g_free(it->message);
    g_free(it->source);
    G_OBJECT_CLASS(code_prob_item_parent_class)->finalize(obj);
}

static void code_prob_item_class_init(CodeProbItemClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = code_prob_item_finalize;
}

static void code_prob_item_init(CodeProbItem* it) {
    (void)it;
}

typedef struct {
    AppData* app;
    GtkWidget* page;
    GtkWidget* label; /* tab label, owned ref */
    GtkWidget* summary;
    GtkWidget* btn_err;
    GtkWidget* btn_warn;
    GtkWidget* btn_info;
    GtkWidget* filter_entry;
    GtkWidget* list;
    GListStore* store;
    GtkSingleSelection* selection;
    gboolean show_errors;
    gboolean show_warnings;
    gboolean show_info;
    gchar* needle;
    gchar* base_dir; /* workspace folder the list was built for */
} ProblemsPage;

static ProblemsPage* page_state(GtkWidget* page) {
    return page != NULL ? g_object_get_data(G_OBJECT(page), PAGE_KEY) : NULL;
}

/* The notebook holding page (the preview notebook, an editor notebook or a
 * terminal pane), or NULL when it is out of every notebook */
static GtkNotebook* page_notebook(GtkWidget* page) {
    GtkWidget* parent = page != NULL ? gtk_widget_get_parent(page) : NULL;
    GtkWidget* nb = parent != NULL ? gtk_widget_get_ancestor(parent, GTK_TYPE_NOTEBOOK) : NULL;
    return (nb != NULL && gtk_notebook_page_num(GTK_NOTEBOOK(nb), page) >= 0) ? GTK_NOTEBOOK(nb) : NULL;
}

/* Take page out of the notebook holding it (an editor notebook left empty goes) */
static void remove_page(AppData* app, GtkWidget* page) {
    GtkNotebook* nb = page_notebook(page);
    if (nb == NULL) return;
    if (GTK_WIDGET(nb) == app->notebook) {
        gtk_notebook_remove_page(nb, gtk_notebook_page_num(nb, page));
    } else {
        editor_remove_page(app, page);
    }
}

static void close_page(ProblemsPage* pp) {
    if (pp == NULL || pp->app == NULL || pp->page == NULL) return;
    AppData* app = pp->app;
    GtkWidget* page = pp->page;
    GtkWidget* focus_back = focus_route_return_target(app, page);
    g_object_ref(page);
    if (app->problems_page == page) app->problems_page = NULL;
    remove_page(app, page);
    focus_route_return(app, focus_back, page);
    g_object_unref(page); /* frees pp */
}

static void on_close_clicked(GtkButton* btn, gpointer data) {
    (void)btn;
    close_page(data);
}

static void on_label_middle_click(GtkGestureClick* g, gint n, gdouble x, gdouble y, gpointer data) {
    (void)g; (void)n; (void)x; (void)y;
    close_page(data);
}

static void on_menu_close(GtkWidget* child, gpointer data) {
    (void)child;
    close_page(data);
}

/* The tab menu, as for an editor tab: move, close */
static void on_label_right_click(GtkGestureClick* g, gint n, gdouble x, gdouble y, gpointer data) {
    (void)n; (void)x; (void)y;
    gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
    ProblemsPage* pp = data;
    editor_page_show_context_menu(pp->app, pp->page, pp->label, on_menu_close, pp);
}

static void page_free(gpointer data) {
    ProblemsPage* pp = data;
    if (pp->app != NULL && pp->app->problems_page == pp->page) {
        pp->app->problems_page = NULL;
    }
    g_object_unref(pp->store);
    g_clear_object(&pp->label);
    g_free(pp->needle);
    g_free(pp->base_dir);
    g_free(pp);
}

/* Flatten newlines in diagnostic messages for single-line display */
static gchar* one_line_message(const gchar* msg) {
    if (msg == NULL) return g_strdup("");
    GString* s = g_string_new(NULL);
    gboolean prev_space = FALSE;
    for (const gchar* p = msg; *p != '\0'; p++) {
        if (*p == '\n' || *p == '\r' || *p == '\t') {
            if (!prev_space && s->len > 0) g_string_append_c(s, ' ');
            prev_space = TRUE;
        } else {
            g_string_append_c(s, *p);
            prev_space = (*p == ' ');
        }
    }
    return g_string_free(s, FALSE);
}

static gboolean severity_visible(ProblemsPage* pp, LspSeverity sev) {
    if (sev == LSP_SEVERITY_ERROR) return pp->show_errors;
    if (sev == LSP_SEVERITY_WARNING) return pp->show_warnings;
    return pp->show_info;
}

static gboolean matches_filter(ProblemsPage* pp, const CodeDiagEntry* e) {
    if (!severity_visible(pp, e->diag.severity)) return FALSE;
    if (pp->needle == NULL || pp->needle[0] == '\0') return TRUE;
    gchar* low_msg = g_utf8_strdown(e->diag.message != NULL ? e->diag.message : "", -1);
    gchar* low_path = g_utf8_strdown(e->path != NULL ? e->path : "", -1);
    gboolean hit = strstr(low_msg, pp->needle) != NULL || strstr(low_path, pp->needle) != NULL;
    if (!hit && e->diag.source != NULL) {
        gchar* low_src = g_utf8_strdown(e->diag.source, -1);
        hit = strstr(low_src, pp->needle) != NULL;
        g_free(low_src);
    }
    g_free(low_msg);
    g_free(low_path);
    return hit;
}

static gchar* rel_path_for(AppData* app, const gchar* path) {
    if (path == NULL) return g_strdup("");
    const gchar* base = app != NULL ? app->base_dir : NULL;
    if (base != NULL) {
        gsize n = strlen(base);
        while (n > 1 && base[n - 1] == '/') n--;
        if (n > 0 && strncmp(path, base, n) == 0 && path[n] == '/') {
            return g_strdup(path + n + 1);
        }
    }
    gchar* root = code_index_project_root(path);
    if (root != NULL) {
        gsize n = strlen(root);
        while (n > 1 && root[n - 1] == '/') n--;
        if (n > 0 && strncmp(path, root, n) == 0 && path[n] == '/') {
            gchar* rel = g_strdup(path + n + 1);
            g_free(root);
            return rel;
        }
        g_free(root);
    }
    return g_strdup(path);
}

/* TRUE when path is dir or inside it */
static gboolean path_under(const gchar* dir, const gchar* path) {
    gsize n = strlen(dir);
    while (n > 0 && dir[n - 1] == '/') n--;
    if (n == 0) return TRUE; /* "/" */
    return strncmp(path, dir, n) == 0 && (path[n] == '/' || path[n] == '\0');
}

/* Files of the shown workspace: those under its folder and those open in its
 * editor tabs (canonical paths, as the diagnostics are keyed) */
static GHashTable* workspace_open_paths(AppData* app) {
    GHashTable* set = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t != NULL && t->path != NULL) g_hash_table_add(set, g_canonicalize_filename(t->path, NULL));
    }
    return set;
}

static gboolean in_workspace(const gchar* base, GHashTable* open, const gchar* path) {
    if (path == NULL) return FALSE;
    if (base != NULL && path_under(base, path)) return TRUE;
    return g_hash_table_contains(open, path);
}

static void populate(ProblemsPage* pp) {
    if (pp == NULL || pp->app == NULL) return;
    GPtrArray* all = code_intel_all_diagnostics(pp->app);
    guint total_err = 0, total_warn = 0, total_info = 0, total = 0;
    GPtrArray* kept = g_ptr_array_new();

    g_free(pp->base_dir);
    pp->base_dir = g_strdup(pp->app->base_dir);
    gchar* base = pp->base_dir != NULL && g_path_is_absolute(pp->base_dir)
                      ? g_canonicalize_filename(pp->base_dir, NULL)
                      : NULL;
    GHashTable* open = workspace_open_paths(pp->app);

    for (guint i = 0; i < all->len; i++) {
        CodeDiagEntry* e = g_ptr_array_index(all, i);
        if (!in_workspace(base, open, e->path)) continue;
        total++;
        if (e->diag.severity == LSP_SEVERITY_ERROR) total_err++;
        else if (e->diag.severity == LSP_SEVERITY_WARNING) total_warn++;
        else total_info++;
        if (matches_filter(pp, e)) g_ptr_array_add(kept, e);
    }

    g_list_store_remove_all(pp->store);
    const gchar* cur_path = NULL;
    for (guint i = 0; i < kept->len; i++) {
        CodeDiagEntry* e = g_ptr_array_index(kept, i);
        if (cur_path == NULL || g_strcmp0(cur_path, e->path) != 0) {
            guint count = 0;
            for (guint j = i; j < kept->len && g_strcmp0(((CodeDiagEntry*)g_ptr_array_index(kept, j))->path, e->path) == 0; j++) {
                count++;
            }
            CodeProbItem* hdr = g_object_new(CODE_TYPE_PROB_ITEM, NULL);
            hdr->header = TRUE;
            hdr->path = g_strdup(e->path);
            hdr->shown = rel_path_for(pp->app, e->path);
            hdr->count = count;
            g_list_store_append(pp->store, hdr);
            g_object_unref(hdr);
            cur_path = e->path;
        }
        CodeProbItem* it = g_object_new(CODE_TYPE_PROB_ITEM, NULL);
        it->path = g_strdup(e->path);
        it->message = one_line_message(e->diag.message);
        it->source = g_strdup(e->diag.source);
        it->severity = e->diag.severity;
        it->line = e->diag.line;
        it->column = e->diag.column;
        g_list_store_append(pp->store, it);
        g_object_unref(it);
    }

    g_free(base);
    g_hash_table_destroy(open);

    if (total == 0) {
        gchar* m = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>",
                                           palette_color(PAL_FG_SUBTLE),
                                           _("No problems detected in this workspace's files"));
        gtk_label_set_markup(GTK_LABEL(pp->summary), m);
        g_free(m);
    } else {
        gchar* es = g_strdup_printf(ngettext("%u error", "%u errors", total_err), total_err);
        gchar* ws = g_strdup_printf(ngettext("%u warning", "%u warnings", total_warn), total_warn);
        gchar* m;
        if (total_info > 0) {
            gchar* is = g_strdup_printf(ngettext("%u info", "%u infos", total_info), total_info);
            m = g_markup_printf_escaped("<span foreground=\"%s\"><b>%s</b></span> · "
                                        "<span foreground=\"%s\"><b>%s</b></span> · "
                                        "<span foreground=\"%s\">%s</span>",
                                        palette_color(total_err > 0 ? PAL_ERROR : PAL_FG_SUBTLE), es,
                                        palette_color(total_warn > 0 ? PAL_WARNING : PAL_FG_SUBTLE), ws,
                                        palette_color(PAL_LINK), is);
            g_free(is);
        } else {
            m = g_markup_printf_escaped("<span foreground=\"%s\"><b>%s</b></span> · "
                                        "<span foreground=\"%s\"><b>%s</b></span>",
                                        palette_color(total_err > 0 ? PAL_ERROR : PAL_FG_SUBTLE), es,
                                        palette_color(total_warn > 0 ? PAL_WARNING : PAL_FG_SUBTLE), ws);
        }
        gtk_label_set_markup(GTK_LABEL(pp->summary), m);
        g_free(m);
        g_free(es);
        g_free(ws);
    }

    g_ptr_array_free(kept, TRUE);
    g_ptr_array_unref(all);
}

static void on_filter_toggled(GtkToggleButton* btn, gpointer data) {
    (void)btn;
    ProblemsPage* pp = data;
    pp->show_errors = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pp->btn_err));
    pp->show_warnings = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pp->btn_warn));
    pp->show_info = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pp->btn_info));
    populate(pp);
}

static void on_search_changed(GtkSearchEntry* entry, gpointer data) {
    ProblemsPage* pp = data;
    g_free(pp->needle);
    pp->needle = g_utf8_strdown(gtk_editable_get_text(GTK_EDITABLE(entry)), -1);
    populate(pp);
}

static void on_activate(GtkListView* view, guint position, gpointer data) {
    (void)view;
    ProblemsPage* pp = data;
    CodeProbItem* it = g_list_model_get_item(G_LIST_MODEL(pp->store), position);
    if (it == NULL) return;
    if (!it->header && it->path != NULL) {
        editor_open_at(pp->app, it->path, it->line, it->column);
    }
    g_object_unref(it);
}

static void on_setup(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f; (void)data;
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* icon = file_type_image_new();
    gtk_box_append(GTK_BOX(box), icon);
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(box), label);
    gtk_list_item_set_child(item, box);
}

static const gchar* sev_tag(LspSeverity sev, PaletteRole* out_role) {
    switch (sev) {
    case LSP_SEVERITY_ERROR:
        *out_role = PAL_ERROR;
        return _("error");
    case LSP_SEVERITY_WARNING:
        *out_role = PAL_WARNING;
        return _("warning");
    case LSP_SEVERITY_INFO:
        *out_role = PAL_LINK;
        return _("info");
    default:
        *out_role = PAL_FG_MUTED;
        return _("hint");
    }
}

static void on_bind(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f; (void)data;
    CodeProbItem* it = gtk_list_item_get_item(item);
    GtkWidget* box = gtk_list_item_get_child(item);
    GtkWidget* icon = gtk_widget_get_first_child(box);
    GtkWidget* label = gtk_widget_get_last_child(box);

    if (it->header) {
        gtk_widget_set_margin_start(box, 6);
        gtk_widget_set_margin_end(box, 6);
        gtk_widget_set_visible(icon, TRUE);
        file_type_image_set(icon, it->shown, FALSE);
        gchar* m = g_markup_printf_escaped("<b>%s</b>  <span foreground=\"%s\">(%u)</span>",
                                           it->shown, palette_color(PAL_FG_SUBTLE), it->count);
        gtk_label_set_markup(GTK_LABEL(label), m);
        g_free(m);
    } else {
        gtk_widget_set_margin_start(box, 22);
        gtk_widget_set_margin_end(box, 6);
        gtk_widget_set_visible(icon, FALSE);
        PaletteRole role = PAL_ERROR;
        const gchar* tag = sev_tag(it->severity, &role);
        gchar* src = it->source != NULL && it->source[0] != '\0'
                         ? g_strdup_printf(" [%s]", it->source) : g_strdup("");
        gchar* m = g_markup_printf_escaped(
            "<span foreground=\"%s\"><b>%s</b></span>  %s  "
            "<span foreground=\"%s\">%s (%d:%d)</span>",
            palette_color(role), tag, it->message,
            palette_color(PAL_FG_SUBTLE), src, it->line + 1, it->column + 1);
        gtk_label_set_markup(GTK_LABEL(label), m);
        g_free(m);
        g_free(src);
    }
}

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    static const gchar* const CSS =
        ".code-problems { background-color: @bg_view; }\n"
        ".code-problems listview { background-color: @bg_view; }\n"
        ".code-problems-bar {\n"
        "  background-color: @bg_bar;\n"
        "  border-bottom: 1px solid @border;\n"
        "  padding: 4px 8px;\n"
        "}\n"
        ".code-problems-bar label {\n"
        "  font-size: @size_bar;\n"
        "}\n";
    theme_add_css(CSS);
}

/* Put the page in app->notebook, after the Git, References and Search tabs;
 * it moves from there like an editor tab (drag, tab menu) */
static void insert_page(AppData* app, ProblemsPage* pp) {
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    GtkWidget* git_page = git_tab_page(app);
    gint after = git_page != NULL ? gtk_notebook_page_num(nb, git_page) : 0;
    if (app->refs_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->refs_page));
    if (app->search_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->search_page));
    gtk_notebook_insert_page(nb, pp->page, pp->label, after + 1);
    gtk_notebook_set_tab_reorderable(nb, pp->page, TRUE);
    gtk_notebook_set_tab_detachable(nb, pp->page, TRUE);
    app->problems_page = pp->page;
}

static ProblemsPage* ensure_page(AppData* app) {
    ProblemsPage* pp = page_state(app->problems_page);
    if (pp != NULL) return pp;
    if (app->notebook == NULL) return NULL;
    ensure_css();

    pp = g_new0(ProblemsPage, 1);
    pp->app = app;
    pp->show_errors = TRUE;
    pp->show_warnings = TRUE;
    pp->show_info = TRUE;
    pp->store = g_list_store_new(CODE_TYPE_PROB_ITEM);

    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(page, "code-problems");
    gtk_widget_set_hexpand(page, TRUE);
    gtk_widget_set_vexpand(page, TRUE);

    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(bar, "code-problems-bar");

    pp->summary = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(pp->summary), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(pp->summary), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(pp->summary, TRUE);
    gtk_box_append(GTK_BOX(bar), pp->summary);

    pp->btn_err = gtk_toggle_button_new_with_label(_("Errors"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pp->btn_err), TRUE);
    gtk_widget_add_css_class(pp->btn_err, "flat");
    g_signal_connect(pp->btn_err, "toggled", G_CALLBACK(on_filter_toggled), pp);
    gtk_box_append(GTK_BOX(bar), pp->btn_err);

    pp->btn_warn = gtk_toggle_button_new_with_label(_("Warnings"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pp->btn_warn), TRUE);
    gtk_widget_add_css_class(pp->btn_warn, "flat");
    g_signal_connect(pp->btn_warn, "toggled", G_CALLBACK(on_filter_toggled), pp);
    gtk_box_append(GTK_BOX(bar), pp->btn_warn);

    pp->btn_info = gtk_toggle_button_new_with_label(_("Info"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pp->btn_info), TRUE);
    gtk_widget_add_css_class(pp->btn_info, "flat");
    g_signal_connect(pp->btn_info, "toggled", G_CALLBACK(on_filter_toggled), pp);
    gtk_box_append(GTK_BOX(bar), pp->btn_info);

    pp->filter_entry = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(pp->filter_entry), _("Filter…"));
    gtk_editable_set_width_chars(GTK_EDITABLE(pp->filter_entry), 14);
    g_signal_connect(pp->filter_entry, "search-changed", G_CALLBACK(on_search_changed), pp);
    gtk_box_append(GTK_BOX(bar), pp->filter_entry);

    GtkWidget* close = icon_button_new("window-close", _("Close Problems"));
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), pp);
    gtk_box_append(GTK_BOX(bar), close);

    gtk_box_append(GTK_BOX(page), bar);

    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_setup), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(on_bind), NULL);
    pp->selection = gtk_single_selection_new(G_LIST_MODEL(g_object_ref(pp->store)));
    pp->list = gtk_list_view_new(GTK_SELECTION_MODEL(pp->selection), factory);
    g_signal_connect(pp->list, "activate", G_CALLBACK(on_activate), pp);
    focus_route_attach_list(app, pp->list, pp->selection);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), pp->list);
    gtk_box_append(GTK_BOX(page), scroll);

    pp->page = page;
    g_object_set_data_full(G_OBJECT(page), PAGE_KEY, pp, page_free);
    /* No close button on the tab header (ui-conventions.md) */
    pp->label = g_object_ref_sink(icon_label_new("dialog-warning", _("Problems")));
    GtkGesture* mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(mclick, "released", G_CALLBACK(on_label_middle_click), pp);
    gtk_widget_add_controller(pp->label, GTK_EVENT_CONTROLLER(mclick));
    GtkGesture* rclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclick), GDK_BUTTON_SECONDARY);
    g_signal_connect(rclick, "pressed", G_CALLBACK(on_label_right_click), pp);
    gtk_widget_add_controller(pp->label, GTK_EVENT_CONTROLLER(rclick));
    editor_page_set_movable(page);
    editor_page_set_layout_key(page, "problems");

    insert_page(app, pp);
    return pp;
}

void code_problems_show(AppData* app, EditorTab* tab) {
    (void)tab;
    if (app == NULL) return;
    ProblemsPage* pp = ensure_page(app);
    if (pp == NULL) return;
    populate(pp);
    GtkNotebook* nb = page_notebook(pp->page);
    if (nb == NULL) return;
    /* The preview panel shows when the tab is there and the panel hidden */
    if (GTK_WIDGET(nb) == app->notebook && app->viewer_box != NULL && !gtk_widget_get_visible(app->viewer_box)) {
        gtk_widget_set_visible(app->viewer_box, TRUE);
    }
    gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, pp->page));
    if (g_list_model_get_n_items(G_LIST_MODEL(pp->store)) > 1) {
        gtk_single_selection_set_selected(pp->selection, 1);
    }
    gtk_widget_grab_focus(pp->list);
}

void code_problems_refresh(AppData* app) {
    if (app == NULL || app->problems_page == NULL) return;
    ProblemsPage* pp = page_state(app->problems_page);
    if (pp != NULL) populate(pp);
}

void code_problems_folder_changed(AppData* app) {
    if (app == NULL || app->problems_page == NULL) return;
    ProblemsPage* pp = page_state(app->problems_page);
    if (pp != NULL && g_strcmp0(pp->base_dir, app->base_dir) != 0) populate(pp);
}

/* The page of the workspace left, an owned ref (or NULL): taken out of the
 * preview notebook with its tab; in the editor panel or a terminal pane, it
 * already left with them (editor_detach_tabs()) */
static GtkWidget* detach_page(AppData* app) {
    GtkWidget* page = app->problems_page;
    if (page == NULL) return NULL;
    GtkRoot* root = gtk_widget_get_root(page);
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    if (focus != NULL && gtk_widget_is_ancestor(focus, page)) gtk_root_set_focus(root, NULL);
    g_object_ref(page);
    app->problems_page = NULL;
    GtkNotebook* nb = page_notebook(page);
    if (nb != NULL && GTK_WIDGET(nb) == app->notebook) gtk_notebook_remove_page(nb, gtk_notebook_page_num(nb, page));
    return page;
}

void code_problems_workspace_hidden(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    GtkWidget* page = detach_page(app);
    if (page == NULL) return;
    if (ws->problems_page != NULL) g_object_unref(ws->problems_page);
    ws->problems_page = page;
}

void code_problems_workspace_shown(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || ws->problems_page == NULL) return;
    GtkWidget* page = ws->problems_page;
    ws->problems_page = NULL;
    ProblemsPage* pp = page_state(page);
    if (pp != NULL && app->notebook != NULL && app->problems_page == NULL) {
        if (page_notebook(page) != NULL) {
            app->problems_page = page; /* back with the editor panel or the terminal panes */
        } else {
            insert_page(app, pp);
        }
        populate(pp);
    }
    g_object_unref(page);
}

void code_problems_workspace_removed(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces && ws == &app->workspaces[app->current_ws]) {
        GtkWidget* page = detach_page(app);
        if (page != NULL) {
            remove_page(app, page); /* still in the editor panel shown */
            g_object_unref(page);
        }
    }
    g_clear_object(&ws->problems_page);
}

gboolean code_problems_close_shown(AppData* app) {
    if (app == NULL || app->notebook == NULL || app->problems_page == NULL) return FALSE;
    GtkWidget* page = app->problems_page;
    GtkWidget* focus = app->window != NULL ? gtk_window_get_focus(app->window) : NULL;
    gboolean focused = focus != NULL && (focus == page || gtk_widget_is_ancestor(focus, page));
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    if (!focused && gtk_notebook_get_current_page(nb) != gtk_notebook_page_num(nb, page)) return FALSE;
    if (!gtk_widget_get_mapped(page)) return FALSE;
    close_page(page_state(page));
    return TRUE;
}

gboolean code_problems_step(AppData* app, EditorTab* tab, gboolean forward, CodeNavMessageFunc message) {
    if (app == NULL || tab == NULL || tab->buffer == NULL || tab->view == NULL) return FALSE;
    GtkTextIter cur;
    gtk_text_buffer_get_iter_at_mark(tab->buffer, &cur, gtk_text_buffer_get_insert(tab->buffer));
    gint line = gtk_text_iter_get_line(&cur);
    gint idx = gtk_text_iter_get_line_index(&cur);

    gint target_line = 0, target_idx = 0;
    LspDiagnostic* diag = NULL;
    if (!code_intel_step_diagnostic(app, tab, line, idx, forward, &target_line, &target_idx, &diag)) {
        if (message != NULL) message(tab, _("No diagnostics in this file"));
        return FALSE;
    }

    gint lines = gtk_text_buffer_get_line_count(tab->buffer);
    GtkTextIter dst;
    gtk_text_buffer_get_iter_at_line(tab->buffer, &dst, CLAMP(target_line, 0, lines - 1));
    GtkTextIter eol = dst;
    if (!gtk_text_iter_ends_line(&eol)) gtk_text_iter_forward_to_line_end(&eol);
    gtk_text_iter_set_line_index(&dst, CLAMP(target_idx, 0, gtk_text_iter_get_line_index(&eol)));
    gtk_text_buffer_place_cursor(tab->buffer, &dst);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(tab->view), gtk_text_buffer_get_insert(tab->buffer),
                                 0.15, FALSE, 0.0, 0.3);

    if (message != NULL && diag != NULL) {
        PaletteRole unused = PAL_ERROR;
        const gchar* tag = sev_tag(diag->severity, &unused);
        gchar* flat = one_line_message(diag->message);
        gchar* msg = g_strdup_printf("[%s] %s (%d:%d)", tag, flat, target_line + 1, target_idx + 1);
        message(tab, msg);
        g_free(msg);
        g_free(flat);
    }
    lsp_diagnostic_free(diag);
    return TRUE;
}

GtkWidget* code_problems_open(AppData* app) {
    if (app == NULL) return NULL;
    ProblemsPage* pp = ensure_page(app);
    if (pp == NULL) return NULL;
    populate(pp);
    return pp->page;
}

void code_problems_close(AppData* app) {
    if (app != NULL) close_page(page_state(app->problems_page));
}
