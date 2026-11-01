/* Search in files (see project_search.h) */
#include "project_search.h"
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

/* Kept for the next search: any case, every file by default */
static gchar* last_query;
static TextSearchOptions last_opts;
static Scope last_scope;
static GCancellable* running; /* the search in progress, if any */

typedef struct {
    AppData* app;
    GCancellable* cancellable;
    gchar* query;
    TextSearchOptions opts;
    Scope scope;
    gchar* root;
    GPtrArray* refs;
    gboolean truncated;
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
    gboolean cut_files = FALSE;
    GPtrArray* files = code_index_some_files(s->root, s->scope.ignored, keep_file, &f, cancellable, &cut_files);
    path_globs_free(f.include);
    path_globs_free(f.exclude);
    s->refs = text_search_files(s->query, &s->opts, files, cancellable, &s->truncated);
    s->truncated = s->truncated || cut_files;
    g_ptr_array_unref(files);
}

/* The options and the files searched, for the header of the Search tab */
static gchar* describe(const TextSearchOptions* opts, const Scope* sc) {
    gchar* how = text_search_describe(opts);
    GString* s = g_string_new(how);
    g_free(how);
    if (sc->include[0] != '\0') g_string_append_printf(s, ", in %s", sc->include);
    if (sc->exclude[0] != '\0') g_string_append_printf(s, ", not in %s", sc->exclude);
    if (sc->ignored) g_string_append(s, ", ignored files too");
    return g_string_free(s, FALSE);
}

static void search_done(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    (void)data;
    Search* s = g_task_get_task_data(G_TASK(res));
    if (g_cancellable_is_cancelled(s->cancellable)) return; /* a newer search replaced it */
    if (running == s->cancellable) g_clear_object(&running);
    gchar* how = describe(&s->opts, &s->scope);
    code_refs_show_search(s->app, s->query, s->refs, s->root, how, s->truncated);
    g_free(how);
}

static void start_search(AppData* app, const gchar* query, const TextSearchOptions* opts, const Scope* scope) {
    if (running != NULL) {
        g_cancellable_cancel(running);
        g_clear_object(&running);
    }
    g_free(last_query);
    last_query = g_strdup(query);
    last_opts = *opts;
    scope_clear(&last_scope);
    scope_copy(&last_scope, scope);

    Search* s = g_new0(Search, 1);
    s->app = app;
    s->cancellable = g_cancellable_new();
    s->query = g_strdup(query);
    s->opts = *opts;
    scope_copy(&s->scope, scope);
    s->root = code_index_project_root(app->base_dir);
    running = g_object_ref(s->cancellable);
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
    "Patterns separated by commas: *.c (a name at any depth), src/ui (from the project root), build/ (folders " \
    "only)"

void project_search_show(AppData* app) {
    if (app == NULL || app->window == NULL || app->base_dir == NULL || app_dialog_is_open(app->window)) {
        return;
    }
    gchar* root = code_index_project_root(app->base_dir);
    Prompt* p = g_new0(Prompt, 1);
    p->app = app;
    GtkWidget* dlg = app_dialog_new("Search in Files", app->window, "_Cancel", APP_RESPONSE_CANCEL, NULL);
    p->search = app_dialog_add_button(APP_DIALOG(dlg), "_Search", RESPONSE_SEARCH);
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
    gtk_entry_set_placeholder_text(GTK_ENTRY(p->entry), "Text to find");
    gtk_entry_set_activates_default(GTK_ENTRY(p->entry), TRUE);
    gchar* initial = selected_text(app);
    gtk_editable_set_text(GTK_EDITABLE(p->entry), initial != NULL ? initial : last_query != NULL ? last_query : "");
    g_free(initial);
    gtk_box_append(GTK_BOX(row), p->entry);
    GtkWidget* toggles = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(toggles, "linked");
    gtk_box_append(GTK_BOX(toggles), option_new(p, OPT_CASE, "Aa", "Match Case (Alt+M)", last_opts.match_case));
    gtk_box_append(GTK_BOX(toggles), option_new(p, OPT_WORD, "ab", "Whole Word (Alt+W)", last_opts.whole_word));
    gtk_box_append(GTK_BOX(toggles), option_new(p, OPT_REGEX, ".*", "Regular Expression, PCRE Syntax (Alt+R)",
                                                last_opts.regex));
    gtk_box_append(GTK_BOX(toggles),
                   option_new(p, OPT_FUZZY, "a…z", "Letters in Order, Not Next to Each Other: edvim Finds editor_vim (Alt+L)",
                              last_opts.fuzzy));
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
    p->include = filter_row(GTK_GRID(grid), 0, "Files to i_nclude", "*.c, src/ (empty: every file)", GLOBS_TOOLTIP,
                            last_scope.include);
    p->exclude = filter_row(GTK_GRID(grid), 1, "Files to e_xclude", "tests/, *.min.js", GLOBS_TOOLTIP,
                            last_scope.exclude);
    gtk_box_append(GTK_BOX(box), grid);
    p->ignored = gtk_check_button_new_with_mnemonic("Include _ignored files");
    gtk_widget_set_tooltip_text(p->ignored, "Also the files git ignores (.gitignore); outside git, hidden and build folders");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(p->ignored), last_scope.ignored);
    gtk_box_append(GTK_BOX(box), p->ignored);

    gchar* where = g_strdup_printf("In the files of %s", root);
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
