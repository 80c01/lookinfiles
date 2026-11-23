/* The Build page of the preview notebook (see task_page.h) */
#include "tasks/task_page.h"
#include "core/i18n.h"
#include "core/run_time.h"
#include "editor/editor.h"
#include "git/git_tab.h"
#include "theme/icons.h"
#include "theme/palette.h"
#include "theme/theme.h"
#include "widgets/focus_route.h"
#include "widgets/panels.h"
#include <string.h>

/* Output lines shown at most (the messages are still read past them) */
#define MAX_OUTPUT_LINES 20000
/* Output added to the view at most this often */
#define FLUSH_MS 100

/* ------------------------------------------------------------------------- */
/* One row: a message of the output                                          */
/* ------------------------------------------------------------------------- */

#define TASK_TYPE_MSG_ITEM (task_msg_item_get_type())
G_DECLARE_FINAL_TYPE(TaskMsgItem, task_msg_item, TASK, MSG_ITEM, GObject)

struct _TaskMsgItem {
    GObject parent;
    TaskMsgKind kind;
    gchar* path;  /* absolute, or NULL when the output names no file */
    gchar* shown; /* the file as shown: relative to the task's folder when under it */
    gint line;    /* 1-based, 0: unknown */
    gint column;  /* 1-based, 0: unknown */
    gchar* text;
    guint output_line;
};

G_DEFINE_FINAL_TYPE(TaskMsgItem, task_msg_item, G_TYPE_OBJECT)

static void task_msg_item_finalize(GObject* obj) {
    TaskMsgItem* it = TASK_MSG_ITEM(obj);
    g_free(it->path);
    g_free(it->shown);
    g_free(it->text);
    G_OBJECT_CLASS(task_msg_item_parent_class)->finalize(obj);
}

static void task_msg_item_class_init(TaskMsgItemClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = task_msg_item_finalize;
}

static void task_msg_item_init(TaskMsgItem* it) {
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
    GtkWidget* run_again;
    GtkWidget* stop;
    GtkWidget* list;
    GListStore* store;
    GtkSingleSelection* selection;
    GtkWidget* output;
    GtkTextBuffer* buffer;
    GtkTextMark* end_mark;
    GString* pending; /* output not shown yet */
    guint flush_id;
    guint out_lines;  /* lines in the buffer */
    gboolean truncated;
    gboolean running;
    gchar* task_label; /* what runs, or NULL before the first run */
    gchar* dir;
    guint errors, warnings;
} BuildPage;

#define PAGE_KEY "task-build-page"

static TaskPageAction action_run_again;
static TaskPageAction action_stop;

void task_page_set_actions(TaskPageAction run_again, TaskPageAction stop) {
    action_run_again = run_again;
    action_stop = stop;
}

static BuildPage* page_state(GtkWidget* page) {
    return page != NULL ? g_object_get_data(G_OBJECT(page), PAGE_KEY) : NULL;
}

static void ensure_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;
    theme_add_css(".task-build { background-color: @bg_view; }"
                  ".task-build listview { background-color: @bg_view; }"
                  ".task-build .task-header { padding: 4px 10px; color: @fg; }"
                  ".task-build .task-row { padding: 3px 10px; }"
                  ".task-build .task-error { color: @error; }"
                  ".task-build .task-warning { color: @warning; }"
                  ".task-build .task-note { color: @fg_subtle; }"
                  ".task-build .task-output, .task-build .task-output text {"
                  "  font-family: @font_code; background-color: @bg_view; color: @fg; }");
}

static const gchar* kind_class(TaskMsgKind kind) {
    switch (kind) {
    case TASK_MSG_ERROR: return "task-error";
    case TASK_MSG_WARNING: return "task-warning";
    case TASK_MSG_NOTE: break;
    }
    return "task-note";
}

static const gchar* kind_icon(TaskMsgKind kind) {
    switch (kind) {
    case TASK_MSG_ERROR: return "dialog-error";
    case TASK_MSG_WARNING: return "dialog-warning";
    case TASK_MSG_NOTE: break;
    }
    return "dialog-information";
}

static void on_setup(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    (void)data;
    /* One icon per kind (only the row's is shown), then the message */
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(row, "task-row");
    for (gint k = TASK_MSG_ERROR; k <= TASK_MSG_NOTE; k++) {
        GtkWidget* icon = icon_image_new(kind_icon((TaskMsgKind)k));
        gtk_widget_add_css_class(icon, kind_class((TaskMsgKind)k));
        gtk_box_append(GTK_BOX(row), icon);
    }
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_single_line_mode(GTK_LABEL(label), TRUE);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(row), label);
    gtk_list_item_set_child(item, row);
}

static void on_bind(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    (void)data;
    GtkWidget* row = gtk_list_item_get_child(item);
    TaskMsgItem* it = gtk_list_item_get_item(item);
    GtkWidget* child = gtk_widget_get_first_child(row);
    for (gint k = TASK_MSG_ERROR; k <= TASK_MSG_NOTE; k++, child = gtk_widget_get_next_sibling(child)) {
        gtk_widget_set_visible(child, (TaskMsgKind)k == it->kind);
    }
    gchar* place = it->shown == NULL ? g_strdup("")
                   : it->column > 0  ? g_strdup_printf("%s:%d:%d", it->shown, it->line, it->column)
                   : it->line > 0    ? g_strdup_printf("%s:%d", it->shown, it->line)
                                     : g_strdup(it->shown);
    gchar* markup = g_markup_printf_escaped("%s  <span foreground=\"%s\">%s</span>", it->text,
                                            palette_color(PAL_FG_SUBTLE), place);
    gtk_label_set_markup(GTK_LABEL(child), markup);
    gtk_widget_set_tooltip_text(child, it->text);
    g_free(markup);
    g_free(place);
}

/* Scroll the output to the line a message was read on */
static void show_output_line(BuildPage* bp, guint line) {
    if (line >= bp->out_lines) return;
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_line(bp->buffer, &it, (gint)line);
    gtk_text_buffer_place_cursor(bp->buffer, &it);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(bp->output), &it, 0.0, TRUE, 0.0, 0.3);
}

static void open_item(BuildPage* bp, guint position) {
    TaskMsgItem* it = g_list_model_get_item(G_LIST_MODEL(bp->store), position);
    if (it == NULL) return;
    show_output_line(bp, it->output_line);
    if (it->path != NULL) editor_open_at(bp->app, it->path, MAX(it->line - 1, 0), MAX(it->column - 1, 0));
    g_object_unref(it);
}

static void on_activate(GtkListView* view, guint position, gpointer data) {
    (void)view;
    open_item(data, position);
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
    BuildPage* bp = data;
    AppData* app = bp->app;
    if (app->build_page != bp->page || app->notebook == NULL) return;
    GtkWidget* closed = app->build_page;
    GtkWidget* focus_back = focus_route_return_target(app, closed);
    g_object_ref(closed);
    app->build_page = NULL;
    remove_page(app, closed);
    focus_route_return(app, focus_back, closed);
    g_object_unref(closed); /* frees bp unless a run still writes to it */
}

static void on_run_again_clicked(GtkButton* button, gpointer data) {
    (void)button;
    BuildPage* bp = data;
    if (action_run_again != NULL) action_run_again(bp->app);
}

static void on_stop_clicked(GtkButton* button, gpointer data) {
    (void)button;
    BuildPage* bp = data;
    if (action_stop != NULL) action_stop(bp->app);
}

static void on_menu_close(GtkWidget* child, gpointer data) {
    (void)child;
    on_close_clicked(NULL, data);
}

/* The tab menu, as for an editor tab: move, close */
static void on_label_right_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    BuildPage* bp = data;
    editor_page_show_context_menu(bp->app, bp->page, bp->label, on_menu_close, bp);
}

static void on_label_middle_click(GtkGestureClick* gesture, gint n_press, gdouble x, gdouble y, gpointer data) {
    (void)n_press;
    (void)x;
    (void)y;
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    on_close_clicked(NULL, data);
}

static void page_free(gpointer p) {
    BuildPage* bp = p;
    if (bp->app != NULL && bp->app->build_page == bp->page) bp->app->build_page = NULL;
    if (bp->flush_id != 0) g_source_remove(bp->flush_id);
    g_string_free(bp->pending, TRUE);
    g_object_unref(bp->store);
    g_clear_object(&bp->label);
    g_free(bp->task_label);
    g_free(bp->dir);
    g_free(bp);
}

/* Put the page in the preview notebook, after the Git tab and the result
 * pages; it moves from there like an editor tab */
static void insert_page(AppData* app, BuildPage* bp) {
    GtkNotebook* nb = GTK_NOTEBOOK(app->notebook);
    GtkWidget* git_page = git_tab_page(app);
    gint after = git_page != NULL ? gtk_notebook_page_num(nb, git_page) : 0;
    if (app->refs_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->refs_page));
    if (app->search_page != NULL) after = MAX(after, gtk_notebook_page_num(nb, app->search_page));
    gtk_notebook_insert_page(nb, bp->page, bp->label, after + 1);
    gtk_notebook_set_tab_reorderable(nb, bp->page, TRUE);
    gtk_notebook_set_tab_detachable(nb, bp->page, TRUE);
    app->build_page = bp->page;
}

static GtkWidget* scrolled(GtkWidget* child) {
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), child);
    return scroll;
}

/* The page, made when missing (create); NULL when it is not open and create
 * is FALSE */
static BuildPage* ensure_page(AppData* app, gboolean create) {
    BuildPage* bp = page_state(app->build_page);
    if (bp != NULL || !create || app->notebook == NULL) return bp;
    ensure_css();
    bp = g_new0(BuildPage, 1);
    bp->app = app;
    bp->store = g_list_store_new(TASK_TYPE_MSG_ITEM);
    bp->pending = g_string_new(NULL);

    GtkWidget* page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(page, "task-build");
    gtk_widget_set_hexpand(page, TRUE);
    gtk_widget_set_vexpand(page, TRUE);

    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(bar, "task-header");
    bp->header = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(bp->header), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(bp->header), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(bp->header, TRUE);
    gtk_box_append(GTK_BOX(bar), bp->header);
    bp->run_again = icon_button_new("view-refresh", _("Run Again (F7)"));
    g_signal_connect(bp->run_again, "clicked", G_CALLBACK(on_run_again_clicked), bp);
    gtk_box_append(GTK_BOX(bar), bp->run_again);
    bp->stop = icon_button_new("process-stop", _("Stop (Ctrl+F7)"));
    gtk_widget_set_sensitive(bp->stop, FALSE);
    g_signal_connect(bp->stop, "clicked", G_CALLBACK(on_stop_clicked), bp);
    gtk_box_append(GTK_BOX(bar), bp->stop);
    GtkWidget* close = icon_button_new("window-close", _("Close Build"));
    g_signal_connect(close, "clicked", G_CALLBACK(on_close_clicked), bp);
    gtk_box_append(GTK_BOX(bar), close);
    gtk_box_append(GTK_BOX(page), bar);

    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_setup), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(on_bind), NULL);
    bp->selection = gtk_single_selection_new(G_LIST_MODEL(g_object_ref(bp->store)));
    gtk_single_selection_set_autoselect(bp->selection, FALSE);
    bp->list = gtk_list_view_new(GTK_SELECTION_MODEL(bp->selection), factory);
    g_signal_connect(bp->list, "activate", G_CALLBACK(on_activate), bp);
    focus_route_attach_list(app, bp->list, bp->selection);

    bp->output = gtk_text_view_new();
    gtk_widget_add_css_class(bp->output, "task-output");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(bp->output), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(bp->output), FALSE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(bp->output), 10);
    bp->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bp->output));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(bp->buffer, &end);
    bp->end_mark = gtk_text_buffer_create_mark(bp->buffer, NULL, &end, FALSE);

    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    paned_use_thin_handle(paned);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_paned_set_start_child(GTK_PANED(paned), scrolled(bp->list));
    gtk_paned_set_shrink_start_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_end_child(GTK_PANED(paned), scrolled(bp->output));
    gtk_paned_set_shrink_end_child(GTK_PANED(paned), FALSE);
    gtk_paned_set_position(GTK_PANED(paned), 160);
    gtk_box_append(GTK_BOX(page), paned);

    bp->page = page;
    g_object_set_data_full(G_OBJECT(page), PAGE_KEY, bp, page_free);
    bp->label = g_object_ref_sink(icon_label_new("applications-engineering", _("Build")));
    GtkGesture* mclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(mclick), GDK_BUTTON_MIDDLE);
    g_signal_connect(mclick, "released", G_CALLBACK(on_label_middle_click), bp);
    gtk_widget_add_controller(bp->label, GTK_EVENT_CONTROLLER(mclick));
    GtkGesture* rclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclick), GDK_BUTTON_SECONDARY);
    g_signal_connect(rclick, "pressed", G_CALLBACK(on_label_right_click), bp);
    gtk_widget_add_controller(bp->label, GTK_EVENT_CONTROLLER(rclick));
    editor_page_set_movable(page);
    editor_page_set_layout_key(page, "build");

    insert_page(app, bp);
    return bp;
}

/* dir with the home folder as "~" */
static gchar* short_dir(const gchar* dir) {
    const gchar* home = g_get_home_dir();
    gsize n = strlen(home);
    if (dir != NULL && n > 1 && strncmp(dir, home, n) == 0 && (dir[n] == '\0' || dir[n] == G_DIR_SEPARATOR)) {
        return g_strconcat("~", dir + n, NULL);
    }
    return g_strdup(dir != NULL ? dir : "");
}

/* "2 errors, 1 warning", or NULL when there are none */
static gchar* counts_text(guint errors, guint warnings) {
    if (errors == 0 && warnings == 0) return NULL;
    GString* s = g_string_new(NULL);
    if (errors > 0) g_string_append_printf(s, ngettext("%u error", "%u errors", errors), errors);
    if (warnings > 0) {
        if (s->len > 0) g_string_append(s, ", ");
        g_string_append_printf(s, ngettext("%u warning", "%u warnings", warnings), warnings);
    }
    return g_string_free(s, FALSE);
}

/* The header: "<b>make test</b>  ~/proj  state" */
static void set_header(BuildPage* bp, const gchar* state) {
    gchar* dir = short_dir(bp->dir);
    gchar* markup = g_markup_printf_escaped("<b>%s</b>  <span foreground=\"%s\">%s</span>  %s",
                                            bp->task_label != NULL ? bp->task_label : "",
                                            palette_color(PAL_FG_SUBTLE), dir, state);
    gtk_label_set_markup(GTK_LABEL(bp->header), markup);
    g_free(markup);
    g_free(dir);
}

static void running_header(BuildPage* bp) {
    gchar* counts = counts_text(bp->errors, bp->warnings);
    gchar* state = counts != NULL ? g_strdup_printf(_("running… %s"), counts) : g_strdup(_("running…"));
    set_header(bp, state);
    g_free(state);
    g_free(counts);
}

static void flush_output(BuildPage* bp) {
    if (bp->flush_id != 0) {
        g_source_remove(bp->flush_id);
        bp->flush_id = 0;
    }
    if (bp->pending->len == 0) return;
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(bp->buffer, &end);
    gtk_text_buffer_insert(bp->buffer, &end, bp->pending->str, (gint)bp->pending->len);
    g_string_truncate(bp->pending, 0);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(bp->output), bp->end_mark);
}

static gboolean on_flush(gpointer data) {
    BuildPage* bp = data;
    bp->flush_id = 0;
    flush_output(bp);
    return G_SOURCE_REMOVE;
}

GtkWidget* task_page_begin(AppData* app, const gchar* label, const gchar* dir, gboolean front) {
    BuildPage* bp = ensure_page(app, TRUE);
    if (bp == NULL) return NULL;
    g_list_store_remove_all(bp->store);
    if (bp->flush_id != 0) {
        g_source_remove(bp->flush_id);
        bp->flush_id = 0;
    }
    g_string_truncate(bp->pending, 0);
    gtk_text_buffer_set_text(bp->buffer, "", 0);
    bp->out_lines = 0;
    bp->truncated = FALSE;
    bp->errors = bp->warnings = 0;
    bp->running = TRUE;
    g_free(bp->task_label);
    bp->task_label = g_strdup(label);
    g_free(bp->dir);
    bp->dir = g_strdup(dir);
    gtk_widget_set_sensitive(bp->stop, TRUE);
    running_header(bp);
    if (front) task_page_present(bp->page);
    return g_object_ref(bp->page);
}

void task_page_present(GtkWidget* page) {
    GtkNotebook* nb = page_notebook(page);
    if (nb != NULL) gtk_notebook_set_current_page(nb, gtk_notebook_page_num(nb, page));
}

void task_page_append_output(GtkWidget* page, const gchar* text) {
    BuildPage* bp = page_state(page);
    if (bp == NULL || text == NULL || bp->truncated) return;
    for (const gchar* c = text; *c != '\0'; c++) {
        if (*c == '\n') bp->out_lines++;
    }
    g_string_append(bp->pending, text);
    if (bp->out_lines >= MAX_OUTPUT_LINES) {
        bp->truncated = TRUE;
        g_string_append_printf(bp->pending, "%s\n", _("(the rest of the output is not shown)"));
    }
    if (bp->flush_id == 0) bp->flush_id = g_timeout_add(FLUSH_MS, on_flush, bp);
}

/* The file of a message as shown: relative to the task's folder when under it */
static gchar* shown_file(const BuildPage* bp, const gchar* path) {
    gsize n = bp->dir != NULL ? strlen(bp->dir) : 0;
    if (n > 0 && strncmp(path, bp->dir, n) == 0 && (path[n] == '/' || path[n] == '\\')) return g_strdup(path + n + 1);
    return g_strdup(path);
}

void task_page_add_message(GtkWidget* page, const TaskMessage* msg) {
    BuildPage* bp = page_state(page);
    if (bp == NULL || msg == NULL) return;
    TaskMsgItem* it = g_object_new(TASK_TYPE_MSG_ITEM, NULL);
    it->kind = msg->kind;
    it->path = g_strdup(msg->file);
    it->shown = msg->file != NULL ? shown_file(bp, msg->file) : NULL;
    it->line = msg->line;
    it->column = msg->column;
    it->text = g_strdup(msg->text);
    it->output_line = msg->output_line;
    g_list_store_append(bp->store, it);
    g_object_unref(it);
    if (msg->kind == TASK_MSG_ERROR) bp->errors++;
    if (msg->kind == TASK_MSG_WARNING) bp->warnings++;
    if (bp->running) running_header(bp);
}

void task_page_finish(GtkWidget* page, gboolean success, gint status, gboolean stopped, gint64 usec,
                      guint errors, guint warnings) {
    BuildPage* bp = page_state(page);
    if (bp == NULL) return;
    flush_output(bp);
    bp->running = FALSE;
    gtk_widget_set_sensitive(bp->stop, FALSE);
    gchar* time = run_time_format(usec);
    gchar* counts = counts_text(errors, warnings);
    gchar* end = stopped    ? g_strdup(_("stopped"))
                 : success  ? g_strdup(_("done"))
                 : status > 0 ? g_strdup_printf(_("failed (exit status %d)"), status)
                              : g_strdup(_("failed"));
    gchar* state = counts != NULL ? g_strdup_printf("%s, %s · %s", counts, end, time)
                                  : g_strdup_printf("%s · %s", end, time);
    set_header(bp, state);
    g_free(state);
    g_free(end);
    g_free(counts);
    g_free(time);
}

gboolean task_page_step(AppData* app, gboolean forward) {
    BuildPage* bp = page_state(app->build_page);
    if (bp == NULL) return FALSE;
    guint n = g_list_model_get_n_items(G_LIST_MODEL(bp->store));
    if (n == 0) return FALSE;
    guint cur = gtk_single_selection_get_selected(bp->selection);
    for (guint k = 1; k <= n; k++) {
        guint i = cur == GTK_INVALID_LIST_POSITION ? (forward ? k - 1 : n - k)
                  : forward                        ? (cur + k) % n
                                                   : (cur + n - k) % n;
        TaskMsgItem* it = g_list_model_get_item(G_LIST_MODEL(bp->store), i);
        gboolean wanted = it->path != NULL && it->kind != TASK_MSG_NOTE;
        g_object_unref(it);
        if (!wanted) continue;
        gtk_single_selection_set_selected(bp->selection, i);
        gtk_list_view_scroll_to(GTK_LIST_VIEW(bp->list), i, GTK_LIST_SCROLL_NONE, NULL);
        open_item(bp, i);
        return TRUE;
    }
    return FALSE;
}

/* The page of the workspace left, an owned ref (or NULL): taken out of the
 * preview notebook with its tab; in the editor panel or a terminal pane, it
 * already left with them (editor_detach_tabs()) */
static GtkWidget* detach_page(AppData* app) {
    GtkWidget* page = app->build_page;
    if (page_state(page) == NULL || app->notebook == NULL) return NULL;
    GtkRoot* root = gtk_widget_get_root(page);
    GtkWidget* focus = root != NULL ? gtk_root_get_focus(root) : NULL;
    if (focus != NULL && gtk_widget_is_ancestor(focus, page)) gtk_root_set_focus(root, NULL);
    g_object_ref(page);
    app->build_page = NULL;
    GtkNotebook* nb = page_notebook(page);
    if (nb != NULL && GTK_WIDGET(nb) == app->notebook) gtk_notebook_remove_page(nb, gtk_notebook_page_num(nb, page));
    return page;
}

void task_page_workspace_hidden(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    GtkWidget* page = detach_page(app);
    if (page == NULL) return;
    if (ws->build_page != NULL) g_object_unref(ws->build_page);
    ws->build_page = page;
}

void task_page_workspace_shown(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL || ws->build_page == NULL) return;
    GtkWidget* page = ws->build_page;
    ws->build_page = NULL;
    BuildPage* bp = page_state(page);
    if (bp != NULL && app->notebook != NULL && app->build_page == NULL) {
        if (page_notebook(page) != NULL) {
            app->build_page = page; /* back with the editor panel or the terminal panes */
        } else {
            insert_page(app, bp);
        }
    }
    g_object_unref(page);
}

void task_page_workspace_removed(AppData* app, VirtualLayout* ws) {
    if (app == NULL || ws == NULL) return;
    if (app->current_ws >= 0 && app->current_ws < app->num_workspaces && ws == &app->workspaces[app->current_ws]) {
        GtkWidget* page = detach_page(app);
        if (page != NULL) {
            remove_page(app, page); /* still in the editor panel shown */
            g_object_unref(page);
        }
    }
    g_clear_object(&ws->build_page);
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

gboolean task_page_close_shown(AppData* app) {
    if (app == NULL) return FALSE;
    BuildPage* bp = page_state(app->build_page);
    if (bp == NULL || !page_shown(app, app->build_page)) return FALSE;
    on_close_clicked(NULL, bp);
    return TRUE;
}

GtkWidget* task_page_open(AppData* app) {
    if (app == NULL) return NULL;
    gboolean fresh = (app->build_page == NULL);
    BuildPage* bp = ensure_page(app, TRUE);
    if (bp == NULL) return NULL;
    if (fresh) {
        gchar* markup = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>", palette_color(PAL_FG_SUBTLE),
                                                _("No task run yet — press F7 to build the project"));
        gtk_label_set_markup(GTK_LABEL(bp->header), markup);
        g_free(markup);
    }
    return bp->page;
}

void task_page_close(AppData* app) {
    if (app == NULL) return;
    BuildPage* bp = page_state(app->build_page);
    if (bp != NULL) on_close_clicked(NULL, bp);
}
