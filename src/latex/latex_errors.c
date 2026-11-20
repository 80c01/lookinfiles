/* The LaTeX page of the preview notebook (see latex_errors.h) */
#include "latex/latex_errors.h"
#include "latex/latex_log.h"
#include "core/i18n.h"
#include "editor/editor.h"
#include "git/git_tab.h"
#include "theme/icons.h"
#include "theme/palette.h"
#include "theme/theme.h"
#include "widgets/focus_route.h"
#include <string.h>

/* ------------------------------------------------------------------------- */
/* One row: a message of the log                                             */
/* ------------------------------------------------------------------------- */

#define LATEX_TYPE_ERROR_ITEM (latex_error_item_get_type())
G_DECLARE_FINAL_TYPE(LatexErrorItem, latex_error_item, LATEX, ERROR_ITEM, GObject)

struct _LatexErrorItem {
    GObject parent;
    LatexMsgKind kind;
    gchar* path;    /* absolute, or NULL when the log names no file */
    gchar* shown;   /* file shown (as in the log, without "./") */
    gint line;      /* 1-based, 0: unknown */
    gchar* text;
    gchar* context; /* error: the source line, or NULL */
};

G_DEFINE_FINAL_TYPE(LatexErrorItem, latex_error_item, G_TYPE_OBJECT)

static void latex_error_item_finalize(GObject* obj) {
    LatexErrorItem* it = LATEX_ERROR_ITEM(obj);
    g_free(it->path);
    g_free(it->shown);
    g_free(it->text);
    g_free(it->context);
    G_OBJECT_CLASS(latex_error_item_parent_class)->finalize(obj);
}

static void latex_error_item_class_init(LatexErrorItemClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = latex_error_item_finalize;
}

static void latex_error_item_init(LatexErrorItem* it) {
    (void)it;
}

/* ------------------------------------------------------------------------- */
/* The page                                                                  */
/* ------------------------------------------------------------------------- */

typedef struct {
    AppData* app;
    GtkWidget* page;
    GtkWidget* label; /* tab label (owned ref: kept while the page is set aside) */
    GtkWidget* header;
    GtkWidget* list;
    GListStore* store;
    GtkSingleSelection* selection;
} LatexPage;

#define PAGE_KEY "latex-errors-page"

static LatexPage* page_state(GtkWidget* page) {
    return page != NULL ? g_object_get_data(G_OBJECT(page), PAGE_KEY) : NULL;
}

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".latex-errors { background-color: @bg_view; }"
                  ".latex-errors listview { background-color: @bg_view; }"
                  ".latex-errors .latex-header { padding: 6px 10px; color: @fg; }"
                  ".latex-errors .latex-row { padding: 3px 10px; }"
                  ".latex-errors .latex-error { color: @error; }"
                  ".latex-errors .latex-warning { color: @warning; }"
                  ".latex-errors .latex-badbox { color: @fg_subtle; }"
                  ".latex-errors .latex-context { font-family: @font_code; color: @fg_muted; }");
}

static const gchar* kind_class(LatexMsgKind kind) {
    switch (kind) {
    case LATEX_MSG_ERROR: return "latex-error";
    case LATEX_MSG_WARNING: return "latex-warning";
    case LATEX_MSG_BADBOX: break;
    }
    return "latex-badbox";
}

static const gchar* kind_icon(LatexMsgKind kind) {
    switch (kind) {
    case LATEX_MSG_ERROR: return "dialog-error";
    case LATEX_MSG_WARNING: return "dialog-warning";
    case LATEX_MSG_BADBOX: break;
    }
    return "dialog-information";
}

static void on_setup(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    (void)data;
    /* One icon per kind (only the row's is shown), then the message over its source */
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(row, "latex-row");
    for (gint k = LATEX_MSG_ERROR; k <= LATEX_MSG_BADBOX; k++) {
        GtkWidget* icon = icon_image_new(kind_icon((LatexMsgKind)k));
        gtk_widget_add_css_class(icon, kind_class((LatexMsgKind)k));
        gtk_widget_set_valign(icon, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(row), icon);
    }
    GtkWidget* col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    gtk_widget_set_hexpand(col, TRUE);
    for (gint i = 0; i < 2; i++) {
        GtkWidget* label = gtk_label_new(NULL);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_label_set_single_line_mode(GTK_LABEL(label), TRUE);
        if (i == 1) gtk_widget_add_css_class(label, "latex-context");
        gtk_box_append(GTK_BOX(col), label);
    }
    gtk_box_append(GTK_BOX(row), col);
    gtk_list_item_set_child(item, row);
}

static void on_bind(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    (void)data;
    GtkWidget* row = gtk_list_item_get_child(item);
    LatexErrorItem* it = gtk_list_item_get_item(item);
    GtkWidget* child = gtk_widget_get_first_child(row);
    for (gint k = LATEX_MSG_ERROR; k <= LATEX_MSG_BADBOX; k++, child = gtk_widget_get_next_sibling(child)) {
        gtk_widget_set_visible(child, (LatexMsgKind)k == it->kind);
    }
    GtkWidget* message = gtk_widget_get_first_child(child);
    GtkWidget* context = gtk_widget_get_last_child(child);
    gchar* place = it->shown == NULL ? g_strdup("")
                   : it->line > 0    ? g_strdup_printf("%s:%d", it->shown, it->line)
                                     : g_strdup(it->shown);
    gchar* markup = g_markup_printf_escaped("%s  <span foreground=\"%s\">%s</span>", it->text,
                                            palette_color(PAL_FG_SUBTLE), place);
    gtk_label_set_markup(GTK_LABEL(message), markup);
    gtk_widget_set_tooltip_text(message, it->text);
    g_free(markup);
    g_free(place);
    gtk_label_set_text(GTK_LABEL(context), it->context != NULL ? it->context : "");
    gtk_widget_set_visible(context, it->context != NULL && it->context[0] != '\0');
}

static void on_activate(GtkListView* view, guint position, gpointer data) {
    (void)view;
    LatexPage* lp = data;
    LatexErrorItem* it = g_list_model_get_item(G_LIST_MODEL(lp->store), position);
    if (it == NULL) return;
    if (it->path != NULL) editor_open_at(lp->app, it->path, MAX(it->line - 1, 0), 0);
    g_object_unref(it);
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

static void on_close_clicked(GtkButton* button, gpointer data) {
    (void)button;
    LatexPage* lp = data;
    AppData* app = lp->app;
    if (app->latex_page == NULL || app->notebook == NULL) return;
    GtkWidget* closed = app->latex_page;
    GtkWidget* focus_back = focus_route_return_target(app, closed);
    g_object_ref(closed);
    app->latex_page = NULL;
    remove_page(app, closed);
    focus_route_return(app, focus_back, closed);
    g_object_unref(closed); /* frees lp */
}

static void on_menu_close(GtkWidget* child, gpointer data) {
    (void)child;
    on_close_clicked(NULL, data);
}

/* The tab menu, as for an editor tab: move, close */
static void on_label_right_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press; (void)x; (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    LatexPage* lp = data;
    editor_page_show_context_menu(lp->app, lp->page, lp->label, on_menu_close, lp);
}

static void on_label_middle_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    on_close_clicked(NULL, data);
}

static void page_free(gpointer p) {
    LatexPage* lp = p;
    if (lp->app != NULL && lp->app->latex_page == lp->page) lp->app->latex_page = NULL;
    g_object_unref(lp->store);
    g_clear_object(&lp->label);
    g_free(lp);
}

/* Put the page in the preview notebook, right after the Git tab and the
 * result pages; it moves from there like an editor tab */
static void insert_page(AppData* app, LatexPage* lp) {
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    GtkWidget* git_page = git_tab_page(app);
    gint after = git_page != NULL ? gtk_notebook_page_num(nb, git_page) : 0;
    if (app->refs_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->refs_page));
    if (app->search_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->search_page));
    gtk_notebook_insert_page(nb, lp->page, lp->label, after + 1);
    gtk_notebook_set_tab_reorderable(nb, lp->page, TRUE);
    gtk_notebook_set_tab_detachable(nb, lp->page, TRUE);
    app->latex_page = lp->page;
}

/* The page, made when missing (create) right after the Git tab and the
 * result pages; NULL when it is not open and create is FALSE */
static LatexPage* ensure_page(AppData* app, gboolean create) {
    LatexPage* lp = page_state(app->latex_page);
    if (lp != NULL || !create || app->notebook == NULL) return lp;
    ensure_css();
    lp = g_new0(LatexPage, 1);
    lp->app = app;
    lp->store = g_list_store_new(LATEX_TYPE_ERROR_ITEM);

    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(page, "latex-errors");
    gtk_widget_set_hexpand(page, TRUE);
    gtk_widget_set_vexpand(page, TRUE);

    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(bar, "latex-header");
    lp->header = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(lp->header), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(lp->header), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(lp->header, TRUE);
    gtk_box_append(GTK_BOX(bar), lp->header);
    GtkWidget* close = icon_button_new("window-close", _("Close LaTeX"));
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), lp);
    gtk_box_append(GTK_BOX(bar), close);
    gtk_box_append(GTK_BOX(page), bar);

    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_setup), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(on_bind), NULL);
    lp->selection = gtk_single_selection_new(G_LIST_MODEL(g_object_ref(lp->store)));
    lp->list = gtk_list_view_new(GTK_SELECTION_MODEL(lp->selection), factory);
    g_signal_connect(lp->list, "activate", G_CALLBACK(on_activate), lp);
    focus_route_attach_list(app, lp->list, lp->selection);
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), lp->list);
    gtk_box_append(GTK_BOX(page), scroll);

    lp->page = page;
    g_object_set_data_full(G_OBJECT(page), PAGE_KEY, lp, page_free);
    lp->label = g_object_ref_sink(icon_label_new("dialog-error", _("LaTeX")));
    GtkGesture* mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(mclick, "released", G_CALLBACK(on_label_middle_click), lp);
    gtk_widget_add_controller(lp->label, GTK_EVENT_CONTROLLER(mclick));
    GtkGesture* rclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclick), GDK_BUTTON_SECONDARY);
    g_signal_connect(rclick, "pressed", G_CALLBACK(on_label_right_click), lp);
    gtk_widget_add_controller(lp->label, GTK_EVENT_CONTROLLER(rclick));
    editor_page_set_movable(page);

    insert_page(app, lp);
    return lp;
}

/* The name of root shown in the header */
static gchar* root_markup(const gchar* root) {
    gchar* base = g_path_get_basename(root);
    gchar* bold = g_markup_printf_escaped("<b>%s</b>", base);
    g_free(base);
    return bold;
}

void latex_errors_compiling(AppData* app, const gchar* root) {
    LatexPage* lp = ensure_page(app, FALSE);
    if (lp == NULL) return;
    gchar* name = root_markup(root);
    gchar* format = g_markup_escape_text(_("Compiling %s…"), -1);
    gchar* markup = g_strdup_printf(format, name);
    gtk_label_set_markup(GTK_LABEL(lp->header), markup);
    g_free(markup);
    g_free(format);
    g_free(name);
}

/* "2 errors, 1 warning" (nothing for a kind not found), escaped for markup */
static gchar* counts_markup(const GPtrArray* msgs) {
    guint errors = latex_log_count(msgs, LATEX_MSG_ERROR);
    guint warnings = latex_log_count(msgs, LATEX_MSG_WARNING);
    guint boxes = latex_log_count(msgs, LATEX_MSG_BADBOX);
    GString* s = g_string_new(NULL);
    if (errors > 0) g_string_append_printf(s, ngettext("%u error", "%u errors", errors), errors);
    if (warnings > 0) {
        if (s->len > 0) g_string_append(s, ", ");
        g_string_append_printf(s, ngettext("%u warning", "%u warnings", warnings), warnings);
    }
    if (boxes > 0) {
        if (s->len > 0) g_string_append(s, ", ");
        g_string_append_printf(s, ngettext("%u bad box", "%u bad boxes", boxes), boxes);
    }
    if (s->len == 0) g_string_append(s, _("no errors"));
    gchar* out = g_markup_escape_text(s->str, -1);
    g_string_free(s, TRUE);
    return out;
}

void latex_errors_show(AppData* app, const gchar* root, const GPtrArray* msgs, const gchar* program) {
    gboolean wanted = latex_log_count(msgs, LATEX_MSG_ERROR) + latex_log_count(msgs, LATEX_MSG_WARNING) > 0;
    LatexPage* lp = ensure_page(app, wanted);
    if (lp == NULL) return;
    gchar* dir = g_path_get_dirname(root);
    g_list_store_remove_all(lp->store);
    /* Errors first, then warnings and bad boxes, each in log order */
    for (gint k = LATEX_MSG_ERROR; k <= LATEX_MSG_BADBOX; k++) {
        for (guint i = 0; i < msgs->len; i++) {
            const LatexMessage* m = g_ptr_array_index(msgs, i);
            if ((gint)m->kind != k) continue;
            LatexErrorItem* it = g_object_new(LATEX_TYPE_ERROR_ITEM, NULL);
            it->kind = m->kind;
            it->line = m->line;
            it->text = g_strdup(m->text);
            it->context = g_strdup(m->context);
            if (m->file != NULL) {
                it->shown = g_strdup(g_str_has_prefix(m->file, "./") ? m->file + 2 : m->file);
                it->path = g_canonicalize_filename(m->file, dir);
            }
            g_list_store_append(lp->store, it);
            g_object_unref(it);
        }
    }
    g_free(dir);

    gchar* name = root_markup(root);
    gchar* counts = counts_markup(msgs);
    gchar* hint = g_markup_printf_escaped(msgs->len > 0 ? _("%s; Enter opens") : "%s", program);
    gchar* markup = g_strdup_printf("%s: %s <span foreground=\"%s\">(%s)</span>", name, counts,
                                    palette_color(PAL_FG_SUBTLE), hint);
    gtk_label_set_markup(GTK_LABEL(lp->header), markup);
    g_free(markup);
    g_free(hint);
    g_free(counts);
    g_free(name);
}

/* The page of the workspace left, an owned ref (or NULL): taken out of the
 * preview notebook with its tab; in the editor panel or a terminal pane, it
 * already left with them (editor_detach_tabs()) */
static GtkWidget* detach_page(AppData* app) {
    GtkWidget* page = app->latex_page;
    if (page_state(page) == NULL || app->notebook == NULL) return NULL;
    GtkRoot* root = gtk_widget_get_root(page);
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    if (focus != NULL && gtk_widget_is_ancestor(focus, page)) gtk_root_set_focus(root, NULL);
    g_object_ref(page);
    app->latex_page = NULL;
    GtkNotebook* nb = page_notebook(page);
    if (nb != NULL && GTK_WIDGET(nb) == app->notebook) gtk_notebook_remove_page(nb, gtk_notebook_page_num(nb, page));
    return page;
}

void latex_errors_workspace_hidden(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    GtkWidget* page = detach_page(app);
    if (page == NULL) return;
    if (ws->latex_page != NULL) g_object_unref(ws->latex_page);
    ws->latex_page = page;
}

void latex_errors_workspace_shown(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || ws->latex_page == NULL) return;
    GtkWidget* page = ws->latex_page;
    ws->latex_page = NULL;
    LatexPage* lp = page_state(page);
    if (lp != NULL && app->notebook != NULL && app->latex_page == NULL) {
        if (page_notebook(page) != NULL) {
            app->latex_page = page; /* back with the editor panel or the terminal panes */
        } else {
            insert_page(app, lp);
        }
    }
    g_object_unref(page);
}

void latex_errors_workspace_removed(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces && ws == &app->workspaces[app->current_ws]) {
        GtkWidget* page = detach_page(app);
        if (page != NULL) {
            remove_page(app, page); /* still in the editor panel shown */
            g_object_unref(page);
        }
    }
    g_clear_object(&ws->latex_page);
}

/* page has the focus, or is the current page of the preview notebook, and shows */
static gboolean page_shown(AppData* app, GtkWidget* page) {
    if (page == NULL || app->notebook == NULL) return FALSE;
    GtkWidget* focus = app->window != NULL ? gtk_window_get_focus(app->window) : NULL;
    gboolean focused = focus != NULL && (focus == page || gtk_widget_is_ancestor(focus, page));
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    if (!focused && gtk_notebook_get_current_page(nb) != gtk_notebook_page_num(nb, page)) return FALSE;
    return gtk_widget_get_mapped(page);
}

gboolean latex_errors_close_shown(AppData* app) {
    if (app == NULL) return FALSE;
    LatexPage* lp = page_state(app->latex_page);
    if (lp == NULL || !page_shown(app, app->latex_page)) return FALSE;
    on_close_clicked(NULL, lp);
    return TRUE;
}
