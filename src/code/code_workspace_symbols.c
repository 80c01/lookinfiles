/* Project-wide symbol picker dialog (see code_workspace_symbols.h) */
#include "code/code_workspace_symbols.h"
#include "code/code_index.h"
#include "code/code_intel.h"
#include "code/code_outline_tree.h"
#include "code/code_tags.h"
#include "core/app_paths.h"
#include "core/i18n.h"
#include "theme/palette.h"
#include "widgets/dialog.h"
#include <string.h>

#define RESPONSE_GO     1
#define DEBOUNCE_MS     120
#define MAX_RESULTS     200

/* Kinds the picker offers, in the order of the tagbar groups; row 0 of the
 * drop-down is every kind */
static const CodeSymbolKind KIND_CHOICES[] = {
    CODE_SYM_FUNCTION, CODE_SYM_METHOD, CODE_SYM_MACRO,  CODE_SYM_TYPE,   CODE_SYM_CLASS,
    CODE_SYM_VARIABLE, CODE_SYM_CONSTANT, CODE_SYM_FIELD, CODE_SYM_MODULE, CODE_SYM_TARGET,
};
#define N_KIND_CHOICES G_N_ELEMENTS(KIND_CHOICES)

/* Row of the drop-down chosen last, kept for the next picker */
static guint last_kind_row = 0;

#define CODE_TYPE_WS_SYM (code_ws_sym_get_type())
G_DECLARE_FINAL_TYPE(CodeWsSym, code_ws_sym, CODE, WS_SYM, GObject)

struct _CodeWsSym {
    GObject parent;
    gchar* name;
    gchar* detail;
    const gchar* kind;
    gchar* path;
    gchar* rel_path;
    gint line;
    gint column;
};

G_DEFINE_FINAL_TYPE(CodeWsSym, code_ws_sym, G_TYPE_OBJECT)

static void code_ws_sym_finalize(GObject* obj) {
    CodeWsSym* it = CODE_WS_SYM(obj);
    g_free(it->name);
    g_free(it->detail);
    g_free(it->path);
    g_free(it->rel_path);
    G_OBJECT_CLASS(code_ws_sym_parent_class)->finalize(obj);
}

static void code_ws_sym_class_init(CodeWsSymClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = code_ws_sym_finalize;
}

static void code_ws_sym_init(CodeWsSym* it) {
    (void)it;
}

typedef struct {
    AppData* app;
    EditorTab* tab;
    gchar* root;
    CodeLang lang;
    GtkWidget* dialog;
    GtkWidget* entry;
    GtkWidget* kind_drop;
    GtkStringList* kind_list;               /* rows of kind_drop */
    guint kind_counts[CODE_SYM_N_KINDS];    /* matches of each kind, last answer */
    GtkWidget* list;
    GtkWidget* note;
    GListStore* store;
    GtkSingleSelection* selection;
    guint debounce_id;
    guint serial;
    gint64 lsp_req_id;
    GCancellable* cancellable;
    gboolean closed;
} WsPicker;

static const gchar* rel_of(const gchar* root, const gchar* path) {
    if (path == NULL) return "";
    if (root == NULL) return path;
    gsize n = strlen(root);
    while (n > 1 && root[n - 1] == '/') n--;
    if (n > 0 && strncmp(path, root, n) == 0 && path[n] == '/') return path + n + 1;
    return path;
}

/* Mask of the kinds chosen in the drop-down */
static guint chosen_kinds(WsPicker* p) {
    guint row = gtk_drop_down_get_selected(GTK_DROP_DOWN(p->kind_drop));
    if (row == 0 || row > N_KIND_CHOICES) return CODE_SYM_KINDS_ALL;
    return CODE_SYM_KIND_BIT(KIND_CHOICES[row - 1]);
}

static void on_kind_changed(GObject* drop, GParamSpec* pspec, gpointer data);

/* Label of a row of the drop-down: the kind and how many symbols of it
 * match the query (no number before the first answer) */
static gchar* kind_row_label(WsPicker* p, guint row, gboolean counted) {
    const gchar* name = row == 0 ? _("All kinds") : code_outline_group_label(KIND_CHOICES[row - 1]);
    if (!counted) return g_strdup(name);
    guint n = 0;
    if (row == 0) {
        for (guint k = 0; k < CODE_SYM_N_KINDS; k++) n += p->kind_counts[k];
    } else {
        n = p->kind_counts[KIND_CHOICES[row - 1]];
    }
    /* TRANSLATORS: a kind of symbol and how many match, "Functions (12)" */
    return g_strdup_printf(C_("symbol kind count", "%1$s (%2$u)"), name, n);
}

/* Shows counts (CODE_SYM_N_KINDS entries) in the drop-down, the row chosen
 * kept */
static void set_kind_counts(WsPicker* p, const guint* counts) {
    memcpy(p->kind_counts, counts, sizeof(p->kind_counts));
    guint rows = N_KIND_CHOICES + 1;
    const gchar* labels[N_KIND_CHOICES + 2];
    gchar* owned[N_KIND_CHOICES + 1];
    for (guint i = 0; i < rows; i++) labels[i] = owned[i] = kind_row_label(p, i, TRUE);
    labels[rows] = NULL;
    guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(p->kind_drop));
    g_signal_handlers_block_by_func(p->kind_drop, on_kind_changed, p);
    gtk_string_list_splice(p->kind_list, 0, rows, labels);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(p->kind_drop), sel);
    g_signal_handlers_unblock_by_func(p->kind_drop, on_kind_changed, p);
    for (guint i = 0; i < rows; i++) g_free(owned[i]);
}

/* Next / previous row of the drop-down (step +1 / -1), past the kinds no
 * symbol matches; every kind (row 0) is always a stop */
static void step_kind(WsPicker* p, gint step) {
    guint rows = N_KIND_CHOICES + 1;
    guint row = gtk_drop_down_get_selected(GTK_DROP_DOWN(p->kind_drop));
    if (row >= rows) row = 0;
    for (guint i = 0; i < rows; i++) {
        row = (row + rows + (guint)(step > 0 ? 1 : rows - 1)) % rows;
        if (row == 0 || p->kind_counts[KIND_CHOICES[row - 1]] > 0) break;
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(p->kind_drop), row);
}

static void cancel_pending(WsPicker* p) {
    g_clear_handle_id(&p->debounce_id, g_source_remove);
    if (p->lsp_req_id > 0 && p->tab != NULL && g_list_find(p->app->editors, p->tab) != NULL) {
        code_intel_cancel(p->app, p->tab, p->lsp_req_id);
    }
    p->lsp_req_id = 0;
    if (p->cancellable != NULL) {
        g_cancellable_cancel(p->cancellable);
        g_clear_object(&p->cancellable);
    }
}

static void picker_free(WsPicker* p) {
    p->closed = TRUE;
    cancel_pending(p);
    g_clear_object(&p->store);
    g_free(p->root);
    g_free(p);
}

static void add_row(WsPicker* p, const gchar* name, const gchar* detail, const gchar* kind,
                    const gchar* path, gint line, gint col) {
    CodeWsSym* it = g_object_new(CODE_TYPE_WS_SYM, NULL);
    it->name = g_strdup(name != NULL ? name : "");
    it->detail = detail != NULL && detail[0] != '\0' ? g_strdup(detail) : NULL;
    it->kind = kind != NULL ? kind : "";
    it->path = g_strdup(path != NULL ? path : "");
    it->rel_path = g_strdup(rel_of(p->root, path));
    it->line = line;
    it->column = col;
    g_list_store_append(p->store, it);
    g_object_unref(it);
}

static void update_note(WsPicker* p, const gchar* source, gboolean truncated) {
    guint n = g_list_model_get_n_items(G_LIST_MODEL(p->store));
    gchar* base = g_strdup_printf(ngettext("%1$u symbol (%2$s)", "%1$u symbols (%2$s)", n), n, source);
    if (truncated) {
        gchar* full = g_strdup_printf(_("%s — partial search"), base);
        gtk_label_set_text(GTK_LABEL(p->note), full);
        g_free(full);
    } else {
        gtk_label_set_text(GTK_LABEL(p->note), base);
    }
    g_free(base);
    if (n > 0) gtk_single_selection_set_selected(p->selection, 0);
}

/* Worker fallback: ctags (when installed) or built-in scanners */
typedef struct {
    WsPicker* picker;
    guint serial;
    gchar* root;
    gchar* query;
    guint kinds;
    CodeLang lang;
    GHashTable* texts; /* open buffer snapshots */
    gchar* cache_path;
    GPtrArray* matches; /* CodeMatch* */
    guint counts[CODE_SYM_N_KINDS];
    gchar* source;
    gboolean truncated;
} SearchJob;

static void search_job_free(gpointer data) {
    SearchJob* j = data;
    g_free(j->root);
    g_free(j->query);
    if (j->texts != NULL) g_hash_table_destroy(j->texts);
    g_free(j->cache_path);
    if (j->matches != NULL) g_ptr_array_unref(j->matches);
    g_free(j->source);
    g_free(j);
}

static void search_worker(GTask* task, gpointer source_obj, gpointer task_data, GCancellable* cancellable) {
    (void)source_obj;
    SearchJob* j = task_data;
    if (g_cancellable_is_cancelled(cancellable)) {
        g_task_return_boolean(task, FALSE);
        return;
    }
    CodeLang family = code_lang_family(j->lang);
    GPtrArray* files = family != CODE_LANG_NONE
                           ? code_index_project_files(j->root, family, cancellable, &j->truncated)
                           : code_index_all_files(j->root, cancellable, &j->truncated);

    CodeTagsIndex* tags = NULL;
    const gchar* prog = code_tags_program();
    if (prog != NULL) {
        tags = code_tags_index_new(prog, j->cache_path);
    }
    gboolean scan_trunc = FALSE;
    j->matches = code_index_search_symbols(j->query, j->kinds, j->lang, tags, j->texts, files,
                                           MAX_RESULTS, cancellable, &scan_trunc, j->counts);
    if (scan_trunc) j->truncated = TRUE;
    j->source = g_strdup(tags != NULL ? "ctags" : _("built-in index"));
    if (tags != NULL) code_tags_index_free(tags);
    if (files != NULL) g_ptr_array_unref(files);
    g_task_return_boolean(task, !g_cancellable_is_cancelled(cancellable));
}

static void on_worker_done(GObject* src, GAsyncResult* res, gpointer data) {
    (void)src;
    (void)data;
    GTask* task = G_TASK(res);
    SearchJob* j = g_task_get_task_data(task);
    if (j == NULL || j->picker == NULL || j->picker->closed) return;
    WsPicker* p = j->picker;
    if (j->serial != p->serial) return;
    if (!g_task_propagate_boolean(task, NULL)) return;

    g_list_store_remove_all(p->store);
    for (guint i = 0; j->matches != NULL && i < j->matches->len; i++) {
        CodeMatch* m = g_ptr_array_index(j->matches, i);
        const gchar* detail = m->symbol->declaration ? _("declaration") : m->symbol->signature;
        add_row(p, m->symbol->name, detail, code_symbol_kind_name(m->symbol->kind),
                m->path, m->symbol->line, m->symbol->column);
    }
    set_kind_counts(p, j->counts);
    update_note(p, j->source != NULL ? j->source : _("built-in index"), j->truncated);
}

static GHashTable* snapshot_open_buffers(AppData* app) {
    GHashTable* texts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* t = l->data;
        if (t == NULL || t->path == NULL || t->buffer == NULL || t->is_buffer) continue;
        GtkTextIter s, e;
        gtk_text_buffer_get_bounds(t->buffer, &s, &e);
        g_hash_table_replace(texts, g_canonicalize_filename(t->path, NULL),
                             gtk_text_buffer_get_text(t->buffer, &s, &e, TRUE));
    }
    return texts;
}

static void start_worker_search(WsPicker* p, const gchar* query) {
    SearchJob* j = g_new0(SearchJob, 1);
    j->picker = p;
    j->serial = p->serial;
    j->root = g_strdup(p->root);
    j->query = g_strdup(query);
    j->kinds = chosen_kinds(p);
    j->lang = p->lang;
    j->texts = snapshot_open_buffers(p->app);
    gchar* cdir = app_cache_dir();
    gchar* sum = g_compute_checksum_for_string(G_CHECKSUM_SHA256, p->root, -1);
    j->cache_path = g_build_filename(cdir, "tags", sum, NULL);
    g_free(sum);
    g_free(cdir);

    p->cancellable = g_cancellable_new();
    GTask* task = g_task_new(NULL, p->cancellable, on_worker_done, NULL);
    g_task_set_task_data(task, j, search_job_free);
    g_task_run_in_thread(task, search_worker);
    g_object_unref(task);
}

typedef struct {
    WsPicker* picker;
    guint serial;
    gchar* query;
} LspQuery;

static void on_lsp_symbols(AppData* app, EditorTab* tab, GPtrArray* symbols, gpointer data) {
    LspQuery* lq = data;
    WsPicker* p = lq->picker;
    gchar* query = lq->query;
    guint serial = lq->serial;
    g_free(lq);

    if (p == NULL || p->closed || serial != p->serial) {
        g_free(query);
        return;
    }
    p->lsp_req_id = 0;
    if (symbols == NULL) {
        /* Server failed or gave no answer: fall back to index */
        start_worker_search(p, query);
        g_free(query);
        return;
    }
    g_free(query);

    g_list_store_remove_all(p->store);
    guint counts[CODE_SYM_N_KINDS] = {0};
    for (guint i = 0; i < symbols->len; i++) {
        LspSymbol* s = g_ptr_array_index(symbols, i);
        if ((guint)s->kind < CODE_SYM_N_KINDS) counts[s->kind]++;
    }
    set_kind_counts(p, counts);
    guint kinds = chosen_kinds(p);
    guint taken = 0;
    gboolean more = FALSE;
    for (guint i = 0; i < symbols->len; i++) {
        LspSymbol* s = g_ptr_array_index(symbols, i);
        if (!code_symbol_kind_in(s->kind, kinds)) continue;
        if (taken == MAX_RESULTS) {
            more = TRUE;
            break;
        }
        taken++;
        const gchar* path = s->path != NULL ? s->path : (tab != NULL ? tab->path : NULL);
        gchar* detail = s->detail != NULL && s->container != NULL
                            ? g_strdup_printf(_("%1$s · in %2$s"), s->detail, s->container)
                        : s->container != NULL ? g_strdup_printf(_("in %s"), s->container)
                                               : g_strdup(s->detail);
        add_row(p, s->name, detail, code_symbol_kind_name(s->kind), path, s->line, s->column);
        g_free(detail);
    }
    gchar* srv = code_intel_server_label(app, tab);
    update_note(p, srv != NULL ? srv : "LSP", more);
    g_free(srv);
}

static gboolean run_search(gpointer data) {
    WsPicker* p = data;
    p->debounce_id = 0;
    if (p->closed) return G_SOURCE_REMOVE;
    cancel_pending(p);
    p->serial++;

    const gchar* query = gtk_editable_get_text(GTK_EDITABLE(p->entry));
    gtk_label_set_text(GTK_LABEL(p->note), _("Searching…"));

    if (p->tab != NULL && g_list_find(p->app->editors, p->tab) != NULL) {
        LspQuery* lq = g_new0(LspQuery, 1);
        lq->picker = p;
        lq->serial = p->serial;
        lq->query = g_strdup(query);
        gint64 id = code_intel_workspace_symbols(p->app, p->tab, query, on_lsp_symbols, lq);
        if (id > 0) {
            p->lsp_req_id = id;
            return G_SOURCE_REMOVE;
        }
        g_free(lq->query);
        g_free(lq);
    }

    start_worker_search(p, query);
    return G_SOURCE_REMOVE;
}

static void on_search_changed(GtkSearchEntry* entry, gpointer data) {
    (void)entry;
    WsPicker* p = data;
    g_clear_handle_id(&p->debounce_id, g_source_remove);
    p->debounce_id = g_timeout_add(DEBOUNCE_MS, run_search, p);
}

static void go_to_selected(WsPicker* p) {
    CodeWsSym* it = gtk_single_selection_get_selected_item(p->selection);
    if (it == NULL || it->path == NULL || it->path[0] == '\0') return;
    gchar* path = g_strdup(it->path);
    gint line = it->line, col = it->column;
    AppData* app = p->app;
    EditorTab* tab = p->tab;
    app_dialog_destroy(APP_DIALOG(p->dialog));
    code_nav_jump(app, tab, path, line, col);
    g_free(path);
}

static void on_response(AppDialog* dialog, gint response, gpointer data) {
    WsPicker* p = data;
    if (response == RESPONSE_GO) {
        go_to_selected(p);
        return;
    }
    app_dialog_destroy(dialog);
}

static void on_activate_row(GtkListView* view, guint position, gpointer data) {
    (void)view;
    WsPicker* p = data;
    gtk_single_selection_set_selected(p->selection, position);
    go_to_selected(p);
}

static void on_entry_activate(GtkSearchEntry* entry, gpointer data) {
    (void)entry;
    go_to_selected(data);
}

/* Rows a key of the entry moves in the list: Up / Down one, Page Up /
 * Page Down ten (as in Quick Open); 0 for the entry's own keys */
static gint list_key_delta(guint keyval) {
    switch (keyval) {
    case GDK_KEY_Down: case GDK_KEY_KP_Down: return 1;
    case GDK_KEY_Up: case GDK_KEY_KP_Up: return -1;
    case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down: return 10;
    case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up: return -10;
    default: return 0;
    }
}

static gboolean on_entry_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType state,
                             gpointer data) {
    (void)ctrl; (void)keycode;
    WsPicker* p = data;
    /* Alt+K / Alt+Shift+K: next / previous kind */
    if ((state & (GDK_ALT_MASK | GDK_CONTROL_MASK)) == GDK_ALT_MASK &&
        (keyval == GDK_KEY_k || keyval == GDK_KEY_K)) {
        step_kind(p, (state & GDK_SHIFT_MASK) != 0 ? -1 : 1);
        return TRUE;
    }
    /* Escape closes the picker (the search entry would only clear its text) */
    if (keyval == GDK_KEY_Escape) {
        app_dialog_response(APP_DIALOG(p->dialog), APP_RESPONSE_CANCEL);
        return TRUE;
    }
    gint delta = list_key_delta(keyval);
    if (delta == 0) return FALSE;
    guint n = g_list_model_get_n_items(G_LIST_MODEL(p->store));
    if (n == 0) return TRUE;
    guint cur = gtk_single_selection_get_selected(p->selection);
    gint next = cur == GTK_INVALID_LIST_POSITION ? 0 : CLAMP((gint)cur + delta, 0, (gint)n - 1);
    gtk_single_selection_set_selected(p->selection, (guint)next);
    gtk_list_view_scroll_to(GTK_LIST_VIEW(p->list), (guint)next, GTK_LIST_SCROLL_NONE, NULL);
    return TRUE;
}

static void on_kind_changed(GObject* drop, GParamSpec* pspec, gpointer data) {
    (void)drop;
    (void)pspec;
    WsPicker* p = data;
    last_kind_row = gtk_drop_down_get_selected(GTK_DROP_DOWN(p->kind_drop));
    g_clear_handle_id(&p->debounce_id, g_source_remove);
    run_search(p);
}

static void on_setup(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f; (void)data;
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_margin_start(label, 6);
    gtk_widget_set_margin_end(label, 6);
    gtk_list_item_set_child(item, label);
}

static void on_bind(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f; (void)data;
    CodeWsSym* it = gtk_list_item_get_item(item);
    gchar* markup = g_markup_printf_escaped(
        "<b>%s</b>  <span foreground=\"%s\">%s%s%s · %s:%d</span>",
        it->name, palette_color(PAL_FG_SUBTLE),
        it->kind, it->detail != NULL ? " · " : "",
        it->detail != NULL ? it->detail : "",
        it->rel_path, it->line + 1);
    gtk_label_set_markup(GTK_LABEL(gtk_list_item_get_child(item)), markup);
    g_free(markup);
}

void code_workspace_symbols_show(AppData* app, EditorTab* tab, CodeNavMessageFunc message) {
    if (app == NULL || app->window == NULL || app_dialog_is_open(app->window)) return;
    gchar* root = code_index_project_root(tab != NULL && tab->path != NULL ? tab->path : app->base_dir);
    if (root == NULL) {
        if (message != NULL && tab != NULL) message(tab, _("No project folder open"));
        return;
    }

    WsPicker* p = g_new0(WsPicker, 1);
    p->app = app;
    p->tab = tab;
    p->root = root;
    p->lang = (tab != NULL && tab->path != NULL) ? code_lang_from_name(tab->path) : CODE_LANG_NONE;
    p->store = g_list_store_new(CODE_TYPE_WS_SYM);

    p->dialog = app_dialog_new(_("Workspace Symbols"), app->window,
                               _("_Cancel"), APP_RESPONSE_CANCEL, _("_Go To"), RESPONSE_GO, NULL);
    app_dialog_set_width(APP_DIALOG(p->dialog), 620);
    app_dialog_set_default_response(APP_DIALOG(p->dialog), RESPONSE_GO);

    GtkWidget* area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(area, 14);
    gtk_widget_set_margin_bottom(area, 14);
    gtk_widget_set_margin_start(area, 16);
    gtk_widget_set_margin_end(area, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(p->dialog))), area);

    p->entry = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(p->entry), _("Search project symbols by name…"));
    g_signal_connect(p->entry, "search-changed", G_CALLBACK(on_search_changed), p);
    g_signal_connect(p->entry, "activate", G_CALLBACK(on_entry_activate), p);
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_entry_key), p);
    gtk_widget_add_controller(p->entry, keys);
    gtk_widget_set_hexpand(p->entry, TRUE);

    p->kind_list = gtk_string_list_new(NULL);
    for (guint i = 0; i < N_KIND_CHOICES + 1; i++) {
        gchar* label = kind_row_label(p, i, FALSE);
        gtk_string_list_append(p->kind_list, label);
        g_free(label);
    }
    p->kind_drop = gtk_drop_down_new(G_LIST_MODEL(p->kind_list), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(p->kind_drop), last_kind_row <= N_KIND_CHOICES ? last_kind_row : 0);
    gtk_widget_set_tooltip_text(p->kind_drop, _("Kind of symbol (Alt+K / Alt+Shift+K)"));
    gtk_widget_set_focus_on_click(p->kind_drop, FALSE);
    g_signal_connect(p->kind_drop, "notify::selected", G_CALLBACK(on_kind_changed), p);

    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(row), p->entry);
    gtk_box_append(GTK_BOX(row), p->kind_drop);
    gtk_box_append(GTK_BOX(area), row);

    p->selection = gtk_single_selection_new(G_LIST_MODEL(g_object_ref(p->store)));
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_setup), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(on_bind), NULL);
    p->list = gtk_list_view_new(GTK_SELECTION_MODEL(p->selection), factory);
    g_signal_connect(p->list, "activate", G_CALLBACK(on_activate_row), p);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 340);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), p->list);
    gtk_widget_add_css_class(scroll, "dialog-list");
    gtk_widget_set_margin_top(scroll, 6);
    gtk_box_append(GTK_BOX(area), scroll);

    p->note = gtk_label_new(_("Searching…"));
    gtk_widget_add_css_class(p->note, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(p->note), 0.0f);
    gtk_widget_set_hexpand(p->note, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(p->note), PANGO_ELLIPSIZE_END);
    GtkWidget* hint = gtk_label_new(_("Alt+K / Alt+Shift+K: next / previous kind"));
    gtk_widget_add_css_class(hint, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(hint), 1.0f);
    GtkWidget* foot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_top(foot, 4);
    gtk_box_append(GTK_BOX(foot), p->note);
    gtk_box_append(GTK_BOX(foot), hint);
    gtk_box_append(GTK_BOX(area), foot);

    g_signal_connect(p->dialog, "response", G_CALLBACK(on_response), p);
    g_object_set_data_full(G_OBJECT(p->dialog), "ws-picker", p, (GDestroyNotify)picker_free);
    app_dialog_present(APP_DIALOG(p->dialog));
    gtk_widget_grab_focus(p->entry);
    run_search(p);
}
