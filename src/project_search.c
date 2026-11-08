/* Search in files (see project_search.h) */
#include "project_search.h"
#include "i18n.h"
#include "code_index.h"
#include "code_refs.h"
#include "dialog.h"
#include "editor.h"
#include "path_globs.h"
#include "text_search.h"

#include <string.h>

#define RESPONSE_SEARCH 1

/* Which files are searched */
typedef struct {
    gchar* include; /* path_globs lists ("" for every file) */
    gchar* exclude;
    gboolean ignored; /* also the files git ignores */
} Scope;

static void scope_clear(Scope* sc) {
    g_free(sc->include);
    g_free(sc->exclude);
}

static void scope_copy(Scope* to, const Scope* from) {
    to->include = g_strdup(from->include != NULL ? from->include : "");
    to->exclude = g_strdup(from->exclude != NULL ? from->exclude : "");
    to->ignored = from->ignored;
}

/* A workspace's search (VirtualLayout.search_state), kept for its next
 * search: any case, every file by default */
typedef struct {
    gchar* query;
    TextSearchOptions opts;
    Scope scope;
    GCancellable* running; /* the search in progress, if any */
    GtkWidget* page;       /* its Search page while the workspace is hidden (owned ref) */
} WsSearch;

static WsSearch* ws_search(VirtualLayout* ws) {
    if (ws->search_state == NULL) ws->search_state = g_new0(WsSearch, 1);
    return ws->search_state;
}

static VirtualLayout* current_ws(AppData* app) {
    return app->current_ws >= 0 && app->current_ws < app->num_workspaces ? &app->workspaces[app->current_ws] : NULL;
}

typedef struct {
    AppData* app;
    WsSearch* ws; /* the workspace that started it (valid while not cancelled) */
    GCancellable* cancellable;
    gchar* query;
    TextSearchOptions opts;
    Scope scope;
    gchar* root;
    GPtrArray* refs;
    gboolean cut_files; /* TEXT_SEARCH_MAX_FILES listed */
    TextSearchStats stats;
} Search;

static void search_free(gpointer data) {
    Search* s = data;
    g_object_unref(s->cancellable);
    g_free(s->query);
    scope_clear(&s->scope);
    g_free(s->root);
    if (s->refs != NULL) g_ptr_array_unref(s->refs);
    g_free(s);
}

typedef struct {
    PathGlobs* include;
    PathGlobs* exclude;
} Filter;

static gboolean keep_file(const gchar* rel, gpointer data) {
    Filter* f = data;
    return (f->include == NULL || path_globs_match(f->include, rel)) &&
           (f->exclude == NULL || !path_globs_match(f->exclude, rel));
}

static void search_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)task;
    (void)source;
    Search* s = data;
    Filter f = {path_globs_new(s->scope.include), path_globs_new(s->scope.exclude)};
    GPtrArray* files = code_index_some_files(s->root, s->scope.ignored, keep_file, &f, TEXT_SEARCH_MAX_FILES,
                                             cancellable, &s->cut_files);
    path_globs_free(f.include);
    path_globs_free(f.exclude);
    s->refs = text_search_files(s->query, &s->opts, files, cancellable, &s->stats);
    g_ptr_array_unref(files);
}

static void add_note(GString* s, const gchar* note) {
    if (s->len > 0) g_string_append(s, ", ");
    g_string_append(s, note);
}

/* Why the search stopped early and what it skipped, or NULL */
static gchar* stop_note(const Search* s) {
    GString* n = g_string_new(NULL);
    gchar* t = NULL;
    if (s->cut_files) {
        add_note(n, t = g_strdup_printf(_("only the first %u files listed"), (guint)TEXT_SEARCH_MAX_FILES));
        g_free(t);
    }
    if (s->stats.stop == TEXT_SEARCH_MAX_RESULTS_HIT) {
        add_note(n, t = g_strdup_printf(_("stopped at %u results"), (guint)TEXT_SEARCH_MAX_RESULTS));
        g_free(t);
    } else if (s->stats.stop == TEXT_SEARCH_MAX_BYTES_HIT) {
        add_note(n, t = g_strdup_printf(ngettext("stopped after %1$u MB read, %2$u file searched",
                                                 "stopped after %1$u MB read, %2$u files searched",
                                                 s->stats.searched),
                                        (guint)(TEXT_SEARCH_MAX_BYTES / (1024 * 1024)), s->stats.searched));
        g_free(t);
    }
    if (s->stats.skipped_large > 0) {
        add_note(n, t = g_strdup_printf(ngettext("%1$u file over %2$u MB skipped", "%1$u files over %2$u MB skipped",
                                                 s->stats.skipped_large),
                                        s->stats.skipped_large,
                                        (guint)(TEXT_SEARCH_MAX_FILE_BYTES / (1024 * 1024))));
        g_free(t);
    }
    if (s->stats.too_complex > 0) {
        add_note(n, t = g_strdup_printf(ngettext("expression too complex in %u file, matches may be missing",
                                                 "expression too complex in %u files, matches may be missing",
                                                 s->stats.too_complex),
                                        s->stats.too_complex));
        g_free(t);
    }
    if (n->len == 0) {
        g_string_free(n, TRUE);
        return NULL;
    }
    return g_string_free(n, FALSE);
}

/* The options and the files searched, for the header of the Search tab */
static gchar* describe(const TextSearchOptions* opts, const Scope* sc) {
    gchar* how = text_search_describe(opts);
    GString* s = g_string_new(how);
    g_free(how);
    /* TRANSLATORS: items of the search description list, after the options */
    if (sc->include[0] != '\0') {
        g_string_append(s, ", ");
        g_string_append_printf(s, _("in %s"), sc->include);
    }
    if (sc->exclude[0] != '\0') {
        g_string_append(s, ", ");
        g_string_append_printf(s, _("not in %s"), sc->exclude);
    }
    if (sc->ignored) g_string_append_printf(s, ", %s", _("ignored files too"));
    return g_string_free(s, FALSE);
}

static void search_done(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    (void)data;
    Search* s = g_task_get_task_data(G_TASK(res));
    /* Cancelled: a newer search replaced it, or its workspace was removed */
    if (g_cancellable_is_cancelled(s->cancellable)) return;
    WsSearch* st = s->ws;
    if (st->running == s->cancellable) g_clear_object(&st->running);
    gchar* how = describe(&s->opts, &s->scope);
    gchar* note = stop_note(s);
    VirtualLayout* cur = current_ws(s->app);
    if (cur != NULL && cur->search_state == st) {
        code_refs_show_search(s->app, s->query, s->refs, s->root, how, note);
    } else if (st->page != NULL) {
        code_refs_set_search(st->page, s->query, s->refs, s->root, how, note); /* seen on return */
    }
    g_free(note);
    g_free(how);
}

static void start_search(AppData* app, const gchar* query, const TextSearchOptions* opts, const Scope* scope) {
    VirtualLayout* ws = current_ws(app);
    if (ws == NULL) return;
    WsSearch* st = ws_search(ws);
    if (st->running != NULL) {
        g_cancellable_cancel(st->running);
        g_clear_object(&st->running);
    }
    g_free(st->query);
    st->query = g_strdup(query);
    st->opts = *opts;
    scope_clear(&st->scope);
    scope_copy(&st->scope, scope);

    Search* s = g_new0(Search, 1);
    s->app = app;
    s->ws = st;
    s->cancellable = g_cancellable_new();
    s->query = g_strdup(query);
    s->opts = *opts;
    scope_copy(&s->scope, scope);
    s->root = code_index_project_root(app->base_dir);
    st->running = g_object_ref(s->cancellable);
    code_refs_show_searching(app, s->query, s->root);

    GTask* task = g_task_new(NULL, s->cancellable, search_done, NULL);
    g_task_set_task_data(task, s, search_free);
    g_task_run_in_thread(task, search_thread);
    g_object_unref(task);
}

/* The selection of the current editor tab when it is on one line */
static gchar* selected_text(AppData* app) {
    EditorTab* tab = editor_current_tab(app);
    GtkTextIter a, b;
    if (tab == NULL || tab->buffer == NULL || !gtk_text_buffer_get_selection_bounds(tab->buffer, &a, &b)) {
        return NULL;
    }
    gchar* text = gtk_text_buffer_get_text(tab->buffer, &a, &b, FALSE);
    if (text[0] == '\0' || strchr(text, '\n') != NULL) {
        g_free(text);
        return NULL;
    }
    return text;
}

/* The option toggle buttons, in this order, and their keys (with Alt) */
enum { OPT_CASE, OPT_WORD, OPT_REGEX, OPT_FUZZY, OPT_COUNT };
static const guint OPTION_KEYS[OPT_COUNT] = {GDK_KEY_m, GDK_KEY_w, GDK_KEY_r, GDK_KEY_l};

typedef struct {
    AppData* app;
    GtkWidget* entry;
    GtkWidget* options[OPT_COUNT];
    GtkWidget* error; /* why the regular expression is invalid */
    GtkWidget* include;
    GtkWidget* exclude;
    GtkWidget* ignored;
    GtkWidget* search;
} Prompt;

static gboolean is_on(Prompt* p, gint opt) {
    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->options[opt]));
}

static TextSearchOptions prompt_options(Prompt* p) {
    TextSearchOptions o = {is_on(p, OPT_CASE), is_on(p, OPT_WORD), is_on(p, OPT_REGEX), is_on(p, OPT_FUZZY)};
    return o;
}

/* Why the text cannot be searched (an invalid regular expression), or NULL */
static gchar* prompt_error(Prompt* p) {
    TextSearchOptions o = prompt_options(p);
    return text_search_check(gtk_editable_get_text(GTK_EDITABLE(p->entry)), &o);
}

static void update_prompt(Prompt* p) {
    gtk_widget_set_sensitive(p->options[OPT_WORD], !is_on(p, OPT_FUZZY));
    gchar* why = prompt_error(p);
    gtk_label_set_text(GTK_LABEL(p->error), why != NULL ? why : "");
    gtk_widget_set_visible(p->error, why != NULL);
    if (why != NULL) {
        gtk_widget_add_css_class(p->entry, "error");
    } else {
        gtk_widget_remove_css_class(p->entry, "error");
    }
    gtk_widget_set_sensitive(p->search, why == NULL);
    g_free(why);
}

static void on_entry_changed(GtkEditable* editable, gpointer data) {
    (void)editable;
    update_prompt(data);
}

/* Regex and letters in order exclude each other */
static void on_option_toggled(GtkToggleButton* button, gpointer data) {
    Prompt* p = data;
    if (gtk_toggle_button_get_active(button)) {
        if (GTK_WIDGET(button) == p->options[OPT_REGEX]) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->options[OPT_FUZZY]), FALSE);
        } else if (GTK_WIDGET(button) == p->options[OPT_FUZZY]) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->options[OPT_REGEX]), FALSE);
        }
    }
    update_prompt(p);
}

/* Alt+M / W / R / L: the option buttons (they show no mnemonic) */
static gboolean on_key_pressed(GtkEventControllerKey* controller, guint keyval, guint keycode,
                               GdkModifierType state, gpointer data) {
    (void)controller;
    (void)keycode;
    Prompt* p = data;
    if ((state & (GDK_ALT_MASK | GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_SUPER_MASK)) != GDK_ALT_MASK) return FALSE;
    guint key = gdk_keyval_to_lower(keyval);
    for (gint i = 0; i < OPT_COUNT; i++) {
        if (key == OPTION_KEYS[i]) {
            if (gtk_widget_get_sensitive(p->options[i])) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->options[i]), !is_on(p, i));
            }
            return TRUE;
        }
    }
    return FALSE;
}

static void on_response(AppDialog* dialog, gint response, gpointer data) {
    Prompt* p = data;
    if (response == RESPONSE_SEARCH) {
        gchar* why = prompt_error(p);
        g_free(why);
        if (why != NULL) return; /* the error stays shown */
    }
    gchar* query = response == RESPONSE_SEARCH ? g_strdup(gtk_editable_get_text(GTK_EDITABLE(p->entry))) : NULL;
    TextSearchOptions opts = prompt_options(p);
    Scope scope = {g_strdup(gtk_editable_get_text(GTK_EDITABLE(p->include))),
                   g_strdup(gtk_editable_get_text(GTK_EDITABLE(p->exclude))),
                   gtk_check_button_get_active(GTK_CHECK_BUTTON(p->ignored))};
    AppData* app = p->app;
    app_dialog_destroy(dialog); /* frees p */
    if (query != NULL && query[0] != '\0') {
        start_search(app, query, &opts, &scope);
    }
    scope_clear(&scope);
    g_free(query);
}

static GtkWidget* option_new(Prompt* p, gint opt, const gchar* label, const gchar* tooltip, gboolean active) {
    GtkWidget* b = gtk_toggle_button_new_with_label(label);
    gtk_widget_set_tooltip_text(b, tooltip);
    gtk_widget_set_focus_on_click(b, FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), active);
    g_signal_connect(b, "toggled", G_CALLBACK(on_option_toggled), p);
    p->options[opt] = b;
    return b;
}

/* "Files to include" / "exclude": a label with a mnemonic and its entry */
static GtkWidget* filter_row(GtkGrid* grid, gint row, const gchar* label, const gchar* placeholder,
                             const gchar* tooltip, const gchar* text) {
    GtkWidget* l = gtk_label_new_with_mnemonic(label);
    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
    GtkWidget* e = gtk_entry_new();
    gtk_widget_set_hexpand(e, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(e), placeholder);
    gtk_entry_set_activates_default(GTK_ENTRY(e), TRUE);
    gtk_widget_set_tooltip_text(e, tooltip);
    gtk_editable_set_text(GTK_EDITABLE(e), text != NULL ? text : "");
    gtk_label_set_mnemonic_widget(GTK_LABEL(l), e);
    gtk_grid_attach(grid, l, 0, row, 1, 1);
    gtk_grid_attach(grid, e, 1, row, 1, 1);
    return e;
}

#define GLOBS_TOOLTIP                                                                                       \
    _("Patterns separated by commas: *.c (a name at any depth), src/ui (from the project root), build/ (folders " \
    "only)")

void project_search_show(AppData* app) {
    if (app == NULL || app->window == NULL || app->base_dir == NULL || app_dialog_is_open(app->window) ||
        current_ws(app) == NULL) {
        return;
    }
    const WsSearch* last = ws_search(current_ws(app));
    gchar* root = code_index_project_root(app->base_dir);
    Prompt* p = g_new0(Prompt, 1);
    p->app = app;
    GtkWidget* dlg = app_dialog_new(_("Search in Files"), app->window, _("_Cancel"), APP_RESPONSE_CANCEL, NULL);
    p->search = app_dialog_add_button(APP_DIALOG(dlg), _("_Search"), RESPONSE_SEARCH);
    gtk_widget_add_css_class(p->search, "suggested-action");
    app_dialog_set_default_response(APP_DIALOG(dlg), RESPONSE_SEARCH);
    app_dialog_set_width(APP_DIALOG(dlg), 560);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 14);
    gtk_widget_set_margin_bottom(box, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(dlg))), box);

    /* The text and the option buttons on its right */
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    p->entry = gtk_entry_new();
    gtk_widget_set_hexpand(p->entry, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(p->entry), _("Text to find"));
    gtk_entry_set_activates_default(GTK_ENTRY(p->entry), TRUE);
    gchar* initial = selected_text(app);
    gtk_editable_set_text(GTK_EDITABLE(p->entry), initial != NULL ? initial : last->query != NULL ? last->query : "");
    g_free(initial);
    gtk_box_append(GTK_BOX(row), p->entry);
    GtkWidget* toggles = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(toggles, "linked");
    gtk_box_append(GTK_BOX(toggles), option_new(p, OPT_CASE, "Aa", _("Match Case (Alt+M)"), last->opts.match_case));
    gtk_box_append(GTK_BOX(toggles), option_new(p, OPT_WORD, "ab", _("Whole Word (Alt+W)"), last->opts.whole_word));
    gtk_box_append(GTK_BOX(toggles), option_new(p, OPT_REGEX, ".*", _("Regular Expression, PCRE Syntax (Alt+R)"),
                                                last->opts.regex));
    gtk_box_append(GTK_BOX(toggles),
                   option_new(p, OPT_FUZZY, "a…z", _("Letters in Order, Not Next to Each Other: edvim Finds editor_vim (Alt+L)"),
                              last->opts.fuzzy));
    gtk_box_append(GTK_BOX(row), toggles);
    gtk_box_append(GTK_BOX(box), row);

    p->error = gtk_label_new(NULL);
    gtk_widget_add_css_class(p->error, "error");
    gtk_label_set_xalign(GTK_LABEL(p->error), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(p->error), TRUE);
    gtk_widget_set_visible(p->error, FALSE);
    gtk_box_append(GTK_BOX(box), p->error);

    /* The files searched */
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_widget_set_margin_top(grid, 4);
    p->include = filter_row(GTK_GRID(grid), 0, _("Files to i_nclude"), _("*.c, src/ (empty: every file)"), GLOBS_TOOLTIP,
                            last->scope.include);
    p->exclude = filter_row(GTK_GRID(grid), 1, _("Files to e_xclude"), _("tests/, *.min.js"), GLOBS_TOOLTIP,
                            last->scope.exclude);
    gtk_box_append(GTK_BOX(box), grid);
    p->ignored = gtk_check_button_new_with_mnemonic(_("Include _ignored files"));
    gtk_widget_set_tooltip_text(p->ignored, _("Also the files git ignores (.gitignore); outside git, hidden and build folders"));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(p->ignored), last->scope.ignored);
    gtk_box_append(GTK_BOX(box), p->ignored);

    gchar* where = g_strdup_printf(_("In the files of %s"), root);
    GtkWidget* note = gtk_label_new(where);
    g_free(where);
    gtk_widget_add_css_class(note, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(note), TRUE);
    gtk_box_append(GTK_BOX(box), note);
    g_free(root);

    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), p);
    gtk_widget_add_controller(dlg, keys);

    g_signal_connect(p->entry, "changed", G_CALLBACK(on_entry_changed), p);
    g_signal_connect(dlg, "response", G_CALLBACK(on_response), p);
    g_object_set_data_full(G_OBJECT(dlg), "prompt", p, g_free);
    update_prompt(p);
    app_dialog_present(APP_DIALOG(dlg));
    gtk_widget_grab_focus(p->entry);
    gtk_editable_select_region(GTK_EDITABLE(p->entry), 0, -1);
}

void project_search_workspace_hidden(AppData* app, VirtualLayout* ws) {
    GtkWidget* page = code_refs_detach_search(app);
    if (page == NULL) return;
    WsSearch* st = ws_search(ws);
    if (st->page != NULL) g_object_unref(st->page);
    st->page = page;
}

void project_search_workspace_shown(AppData* app, VirtualLayout* ws) {
    WsSearch* st = ws->search_state;
    if (st == NULL || st->page == NULL) return;
    code_refs_attach_search(app, st->page);
    st->page = NULL;
}

void project_search_workspace_removed(AppData* app, VirtualLayout* ws) {
    if (ws == current_ws(app)) {
        GtkWidget* page = code_refs_detach_search(app);
        if (page != NULL) g_object_unref(page);
    }
    WsSearch* st = ws->search_state;
    if (st == NULL) return;
    if (st->running != NULL) {
        g_cancellable_cancel(st->running); /* its end finds it cancelled */
        g_object_unref(st->running);
    }
    if (st->page != NULL) g_object_unref(st->page);
    g_free(st->query);
    scope_clear(&st->scope);
    g_free(st);
    ws->search_state = NULL;
}
