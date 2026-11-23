/* Build and test tasks run from the keyboard (see task_run.h) */
#include "tasks/task_run.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "core/vfs.h"
#include "tasks/task_detect.h"
#include "tasks/task_page.h"
#include "tasks/task_parse.h"
#include "theme/palette.h"
#include "widgets/dialog.h"
#include "widgets/focus_route.h"
#include <string.h>
#ifdef G_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

/* Output read at a time */
#define READ_BYTES 65536
/* A line longer than this is cut (minified output) */
#define MAX_LINE_BYTES 65536
/* A stopped task that is still there after this is killed */
#define STOP_GRACE_MS 3000

typedef struct Run {
    gint refs;
    AppData* app;
    TaskDef* def;
    GSubprocess* proc;
    GInputStream* in;
    GCancellable* cancellable;
    TaskParser* parser;
    GtkWidget* page; /* the Build page written to (owned ref), or NULL */
    GString* partial; /* the line being read */
    gchar* last_line; /* the last line of output that was not empty */
    gint64 started;
    guint kill_id;
    gboolean output_done;
    gboolean exited;
    gboolean stopped; /* by the user */
    gboolean dead;    /* replaced by another run, or the application quits */
} Run;

struct TaskRun {
    GHashTable* chosen;    /* project folder -> TaskDef* chosen last there */
    TaskDef* last;         /* the task run last (Run Again) */
    Run* current;          /* the task running, or NULL */
    GCancellable* finding; /* the search for the project's tasks, or NULL */
};

static struct TaskRun* run_state(AppData* app) {
    if (app->task_run == NULL) {
        app->task_run = g_new0(struct TaskRun, 1);
        app->task_run->chosen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)task_def_free);
    }
    return app->task_run;
}

static void notice(AppData* app, const gchar* text) {
    app_notice(app, text, FALSE);
}

/* ------------------------------------------------------------------------- */
/* A run                                                                     */
/* ------------------------------------------------------------------------- */

static Run* run_ref(Run* r) {
    r->refs++;
    return r;
}

static void run_unref(Run* r) {
    if (--r->refs > 0) return;
    if (r->kill_id != 0) g_source_remove(r->kill_id);
    task_def_free(r->def);
    g_clear_object(&r->proc);
    g_clear_object(&r->in);
    g_clear_object(&r->cancellable);
    task_parser_free(r->parser);
    g_clear_object(&r->page);
    g_string_free(r->partial, TRUE);
    g_free(r->last_line);
    g_free(r);
}

/* Send sig to the task and what it started (its process group) */
static void signal_run(Run* r, gboolean kill_now) {
    if (r->proc == NULL || r->exited) return;
#ifdef G_OS_UNIX
    const gchar* id = g_subprocess_get_identifier(r->proc);
    pid_t pid = id != NULL ? (pid_t)g_ascii_strtoll(id, NULL, 10) : 0;
    if (pid > 0 && kill(-pid, kill_now ? SIGKILL : SIGTERM) == 0) return;
    if (kill_now) g_subprocess_force_exit(r->proc);
    else g_subprocess_send_signal(r->proc, SIGTERM);
#else
    (void)kill_now;
    g_subprocess_force_exit(r->proc);
#endif
}

static gboolean on_kill_timeout(gpointer data) {
    Run* r = data;
    r->kill_id = 0;
    signal_run(r, TRUE);
    return G_SOURCE_REMOVE;
}

/* The page shows the messages the parser added since it had shown
 * `before` of them */
static void show_new_messages(Run* r, guint before) {
    const GPtrArray* msgs = task_parser_messages(r->parser);
    for (guint i = before; i < msgs->len; i++) task_page_add_message(r->page, g_ptr_array_index(msgs, i));
}

/* A complete line of output (without its newline) into the parser and the
 * page's text */
static void take_line(Run* r, const gchar* line, gsize len, GString* shown) {
    gchar* raw = g_strndup(line, len);
    guint before = task_parser_messages(r->parser)->len;
    task_parser_feed(r->parser, raw);
    show_new_messages(r, before);
    gchar* clean = task_parse_clean_line(raw);
    g_string_append(shown, clean);
    g_string_append_c(shown, '\n');
    if (clean[0] != '\0') {
        g_free(r->last_line);
        r->last_line = g_strstrip(g_strdup(clean));
    }
    g_free(clean);
    g_free(raw);
}

static void finish_run(Run* r) {
    AppData* app = r->app;
    struct TaskRun* st = run_state(app);
    guint before = task_parser_messages(r->parser)->len;
    task_parser_finish(r->parser);
    show_new_messages(r, before);

    gboolean success = !r->stopped && g_subprocess_get_successful(r->proc);
    gint status = !r->stopped && g_subprocess_get_if_exited(r->proc) ? g_subprocess_get_exit_status(r->proc) : -1;
    guint errors = task_parser_count(r->parser, TASK_MSG_ERROR);
    guint warnings = task_parser_count(r->parser, TASK_MSG_WARNING);
    /* A failure with no message: its last line says why */
    if (!success && !r->stopped && errors == 0 && r->last_line != NULL) {
        TaskMessage m = {TASK_MSG_ERROR, NULL, 0, 0, r->last_line, 0};
        task_page_add_message(r->page, &m);
        errors = 1;
    }
    gint64 usec = g_get_monotonic_time() - r->started;
    task_page_finish(r->page, success, status, r->stopped, usec, errors, warnings);

    gchar* msg = NULL;
    if (r->stopped) {
        msg = g_strdup_printf(_("%s stopped"), r->def->label);
    } else if (errors > 0 && !success) {
        msg = g_strdup_printf(ngettext("%1$s: %2$u error (Build tab)", "%1$s: %2$u errors (Build tab)", errors),
                              r->def->label, errors);
    } else if (!success) {
        msg = g_strdup_printf(_("%s failed (Build tab)"), r->def->label);
    } else if (warnings > 0) {
        msg = g_strdup_printf(ngettext("%1$s done: %2$u warning (Build tab)", "%1$s done: %2$u warnings (Build tab)",
                                       warnings),
                              r->def->label, warnings);
    } else {
        msg = g_strdup_printf(_("%s done"), r->def->label);
    }
    notice(app, msg);
    g_free(msg);
    if (st->current == r) {
        st->current = NULL;
        run_unref(r);
    }
}

static void maybe_finish(Run* r) {
    if (r->dead || !r->output_done || !r->exited) return;
    finish_run(r);
}

static void read_next(Run* r);

static void on_read(GObject* source, GAsyncResult* result, gpointer data) {
    Run* r = data;
    GBytes* bytes = g_input_stream_read_bytes_finish(G_INPUT_STREAM(source), result, NULL);
    if (r->dead) {
        if (bytes != NULL) g_bytes_unref(bytes);
        run_unref(r);
        return;
    }
    gsize len = 0;
    const gchar* data_bytes = bytes != NULL ? g_bytes_get_data(bytes, &len) : NULL;
    GString* shown = g_string_new(NULL);
    if (len == 0) {
        /* The end (or a read error): the last line without a newline */
        if (r->partial->len > 0) take_line(r, r->partial->str, r->partial->len, shown);
        g_string_truncate(r->partial, 0);
        task_page_append_output(r->page, shown->str);
        g_string_free(shown, TRUE);
        if (bytes != NULL) g_bytes_unref(bytes);
        r->output_done = TRUE;
        maybe_finish(r);
        run_unref(r);
        return;
    }
    const gchar* start = data_bytes;
    const gchar* end = data_bytes + len;
    while (start < end) {
        const gchar* nl = memchr(start, '\n', (gsize)(end - start));
        if (nl == NULL) {
            g_string_append_len(r->partial, start, end - start);
            if (r->partial->len > MAX_LINE_BYTES) {
                take_line(r, r->partial->str, r->partial->len, shown);
                g_string_truncate(r->partial, 0);
            }
            break;
        }
        if (r->partial->len > 0) {
            g_string_append_len(r->partial, start, nl - start);
            take_line(r, r->partial->str, r->partial->len, shown);
            g_string_truncate(r->partial, 0);
        } else {
            take_line(r, start, (gsize)(nl - start), shown);
        }
        start = nl + 1;
    }
    task_page_append_output(r->page, shown->str);
    g_string_free(shown, TRUE);
    g_bytes_unref(bytes);
    read_next(r);
}

static void read_next(Run* r) {
    g_input_stream_read_bytes_async(r->in, READ_BYTES, G_PRIORITY_DEFAULT, r->cancellable, on_read, r);
}

static void on_exited(GObject* source, GAsyncResult* result, gpointer data) {
    Run* r = data;
    g_subprocess_wait_finish(G_SUBPROCESS(source), result, NULL);
    r->exited = TRUE;
    if (r->kill_id != 0) {
        g_source_remove(r->kill_id);
        r->kill_id = 0;
    }
    maybe_finish(r);
    run_unref(r);
}

#ifdef G_OS_UNIX
/* The task leads a process group of its own, so that Stop reaches the
 * compilers and tests it started */
static void child_setup(gpointer data) {
    (void)data;
    setpgid(0, 0);
}
#endif

/* Unsaved editor tabs of files under dir */
static guint unsaved_under(AppData* app, const gchar* dir) {
    guint n = 0;
    gsize len = strlen(dir);
    for (GList* l = app->editors; l != NULL; l = l->next) {
        EditorTab* tab = l->data;
        if (tab == NULL || tab->path == NULL || tab->buffer == NULL || tab->is_buffer) continue;
        if (!gtk_text_buffer_get_modified(tab->buffer)) continue;
        if (strncmp(tab->path, dir, len) == 0 && tab->path[len] == G_DIR_SEPARATOR) n++;
    }
    return n;
}

/* Stop the running task and leave it: its page says it was stopped */
static void drop_current(AppData* app, gboolean quiet) {
    struct TaskRun* st = run_state(app);
    Run* r = st->current;
    if (r == NULL) return;
    st->current = NULL;
    r->dead = TRUE;
    signal_run(r, TRUE);
    g_cancellable_cancel(r->cancellable);
    if (!quiet) task_page_finish(r->page, FALSE, -1, TRUE, g_get_monotonic_time() - r->started, 0, 0);
    run_unref(r);
}

static void start_run(AppData* app, const TaskDef* def) {
    struct TaskRun* st = run_state(app);
    drop_current(app, FALSE);
    task_def_free(st->last);
    st->last = task_def_copy(def);

    GtkWidget* page = task_page_begin(app, def->label, def->dir);
    gchar* program = g_find_program_in_path(def->argv[0]);
    if (program == NULL) {
        gchar* msg = g_strdup_printf(_("%s: program not found (install it or add it to PATH)"), def->argv[0]);
        if (page != NULL) {
            TaskMessage m = {TASK_MSG_ERROR, NULL, 0, 0, msg, 0};
            task_page_add_message(page, &m);
            task_page_finish(page, FALSE, -1, FALSE, 0, 1, 0);
            g_object_unref(page);
        }
        notice(app, msg);
        g_free(msg);
        return;
    }

    GPtrArray* argv = g_ptr_array_new();
    g_ptr_array_add(argv, program);
    for (gchar** a = def->argv + 1; *a != NULL; a++) g_ptr_array_add(argv, *a);
    g_ptr_array_add(argv, NULL);
    GSubprocessLauncher* launcher =
        g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                  G_SUBPROCESS_FLAGS_STDERR_MERGE);
    g_subprocess_launcher_set_cwd(launcher, def->dir);
#ifdef G_OS_UNIX
    g_subprocess_launcher_set_child_setup(launcher, child_setup, NULL, NULL);
#endif
    GError* error = NULL;
    GSubprocess* proc = g_subprocess_launcher_spawnv(launcher, (const gchar* const*)argv->pdata, &error);
    g_object_unref(launcher);
    g_ptr_array_unref(argv);
    g_free(program);
    if (proc == NULL) {
        if (page != NULL) {
            TaskMessage m = {TASK_MSG_ERROR, NULL, 0, 0, error->message, 0};
            task_page_add_message(page, &m);
            task_page_finish(page, FALSE, -1, FALSE, 0, 1, 0);
            g_object_unref(page);
        }
        gchar* msg = g_strdup_printf(_("%1$s did not start: %2$s"), def->label, error->message);
        notice(app, msg);
        g_free(msg);
        g_error_free(error);
        return;
    }
    /* No input: a question asked would wait forever */
    g_output_stream_close(g_subprocess_get_stdin_pipe(proc), NULL, NULL);

    Run* r = g_new0(Run, 1);
    r->refs = 1; /* st->current */
    r->app = app;
    r->def = task_def_copy(def);
    r->proc = proc;
    r->in = g_object_ref(g_subprocess_get_stdout_pipe(proc));
    r->cancellable = g_cancellable_new();
    r->parser = task_parser_new(def->dir);
    r->page = page;
    r->partial = g_string_new(NULL);
    r->started = g_get_monotonic_time();
    st->current = r;
    g_subprocess_wait_async(proc, NULL, on_exited, run_ref(r));
    run_ref(r);
    read_next(r);

    guint unsaved = unsaved_under(app, def->dir);
    gchar* msg = unsaved > 0 ? g_strdup_printf(ngettext("Running %1$s… (%2$u unsaved file)",
                                                         "Running %1$s… (%2$u unsaved files)", unsaved),
                                               def->label, unsaved)
                             : g_strdup_printf(_("Running %s…"), def->label);
    notice(app, msg);
    g_free(msg);
}

gboolean task_run_stop(AppData* app) {
    if (app == NULL || app->task_run == NULL || app->task_run->current == NULL) return FALSE;
    Run* r = app->task_run->current;
    if (r->stopped) {
        signal_run(r, TRUE);
        return TRUE;
    }
    r->stopped = TRUE;
    signal_run(r, FALSE);
    if (r->kill_id == 0) r->kill_id = g_timeout_add(STOP_GRACE_MS, on_kill_timeout, r);
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Choosing a task                                                           */
/* ------------------------------------------------------------------------- */

#define TASK_TYPE_CHOICE (task_choice_get_type())
G_DECLARE_FINAL_TYPE(TaskChoice, task_choice, TASK, CHOICE, GObject)

struct _TaskChoice {
    GObject parent;
    TaskDef* def;
};

G_DEFINE_FINAL_TYPE(TaskChoice, task_choice, G_TYPE_OBJECT)

static void task_choice_finalize(GObject* obj) {
    task_def_free(TASK_CHOICE(obj)->def);
    G_OBJECT_CLASS(task_choice_parent_class)->finalize(obj);
}

static void task_choice_class_init(TaskChoiceClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = task_choice_finalize;
}

static void task_choice_init(TaskChoice* c) {
    (void)c;
}

enum { RESPONSE_RUN = 1 };

typedef struct {
    AppData* app;
    gchar* project;
    GtkWidget* dialog;
    GtkWidget* entry;
    GtkWidget* list;
    GListStore* store;
    GtkCustomFilter* filter;
    GtkFilterListModel* filtered;
    GtkSingleSelection* selection;
    gchar* needle; /* filter text, lower case */
} Picker;

static void picker_free(gpointer data) {
    Picker* p = data;
    g_clear_object(&p->store);
    g_free(p->project);
    g_free(p->needle);
    g_free(p);
}

static gboolean filter_func(gpointer item, gpointer data) {
    Picker* p = data;
    if (p->needle == NULL || p->needle[0] == '\0') return TRUE;
    gchar* low = g_utf8_strdown(TASK_CHOICE(item)->def->label, -1);
    gboolean match = strstr(low, p->needle) != NULL;
    g_free(low);
    return match;
}

static void on_search_changed(GtkSearchEntry* entry, gpointer data) {
    Picker* p = data;
    g_free(p->needle);
    p->needle = g_utf8_strdown(gtk_editable_get_text(GTK_EDITABLE(entry)), -1);
    gtk_filter_changed(GTK_FILTER(p->filter), GTK_FILTER_CHANGE_DIFFERENT);
    if (g_list_model_get_n_items(G_LIST_MODEL(p->filtered)) > 0) {
        gtk_single_selection_set_selected(p->selection, 0);
        gtk_list_view_scroll_to(GTK_LIST_VIEW(p->list), 0, GTK_LIST_SCROLL_NONE, NULL);
    }
}

static void run_selected(Picker* p) {
    TaskChoice* c = gtk_single_selection_get_selected_item(p->selection);
    if (c == NULL) return;
    AppData* app = p->app;
    TaskDef* def = task_def_copy(c->def);
    g_hash_table_replace(run_state(app)->chosen, g_strdup(p->project), task_def_copy(def));
    app_dialog_destroy(APP_DIALOG(p->dialog)); /* frees p */
    start_run(app, def);
    task_def_free(def);
}

static void on_response(AppDialog* dialog, gint response, gpointer data) {
    if (response == RESPONSE_RUN) {
        run_selected(data);
        return;
    }
    app_dialog_destroy(dialog);
}

static void on_activate_row(GtkListView* view, guint position, gpointer data) {
    (void)view;
    Picker* p = data;
    gtk_single_selection_set_selected(p->selection, position);
    run_selected(p);
}

static void on_entry_activate(GtkSearchEntry* entry, gpointer data) {
    (void)entry;
    run_selected(data);
}

static gboolean on_entry_key(GtkEventControllerKey* ctrl, guint keyval, guint keycode, GdkModifierType state,
                             gpointer data) {
    (void)ctrl;
    (void)keycode;
    (void)state;
    Picker* p = data;
    /* Escape closes the picker (the search entry would only clear its text) */
    if (keyval == GDK_KEY_Escape) {
        app_dialog_response(APP_DIALOG(p->dialog), APP_RESPONSE_CANCEL);
        return TRUE;
    }
    gint delta = 0;
    switch (keyval) {
    case GDK_KEY_Down: case GDK_KEY_KP_Down: delta = 1; break;
    case GDK_KEY_Up: case GDK_KEY_KP_Up: delta = -1; break;
    case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down: delta = 10; break;
    case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up: delta = -10; break;
    default: return FALSE;
    }
    guint n = g_list_model_get_n_items(G_LIST_MODEL(p->filtered));
    if (n == 0) return TRUE;
    guint cur = gtk_single_selection_get_selected(p->selection);
    gint next = cur == GTK_INVALID_LIST_POSITION ? 0 : CLAMP((gint)cur + delta, 0, (gint)n - 1);
    gtk_single_selection_set_selected(p->selection, (guint)next);
    gtk_list_view_scroll_to(GTK_LIST_VIEW(p->list), (guint)next, GTK_LIST_SCROLL_NONE, NULL);
    return TRUE;
}

static void on_choice_setup(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    (void)data;
    GtkWidget* label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_margin_start(label, 6);
    gtk_widget_set_margin_end(label, 6);
    gtk_list_item_set_child(item, label);
}

static void on_choice_bind(GtkSignalListItemFactory* f, GtkListItem* item, gpointer data) {
    (void)f;
    const gchar* project = data;
    TaskChoice* c = gtk_list_item_get_item(item);
    /* The folder only when it is not the project's (a Cargo workspace) */
    const gchar* where = g_strcmp0(c->def->dir, project) != 0 ? c->def->dir : "";
    gchar* markup = g_markup_printf_escaped("<b>%s</b>  <span foreground=\"%s\">%s</span>", c->def->label,
                                            palette_color(PAL_FG_SUBTLE), where);
    gtk_label_set_markup(GTK_LABEL(gtk_list_item_get_child(item)), markup);
    g_free(markup);
}

/* The dialog over the tasks of project (TaskDef*), the one chosen last
 * there selected */
static void show_picker(AppData* app, const gchar* project, GPtrArray* tasks) {
    if (app->window == NULL || app_dialog_is_open(app->window)) return;
    Picker* p = g_new0(Picker, 1);
    p->app = app;
    p->project = g_strdup(project);
    p->store = g_list_store_new(TASK_TYPE_CHOICE);
    const TaskDef* chosen = g_hash_table_lookup(run_state(app)->chosen, project);
    guint selected = 0;
    for (guint i = 0; i < tasks->len; i++) {
        TaskChoice* c = g_object_new(TASK_TYPE_CHOICE, NULL);
        c->def = task_def_copy(g_ptr_array_index(tasks, i));
        if (task_def_equal(c->def, chosen)) selected = i;
        g_list_store_append(p->store, c);
        g_object_unref(c);
    }

    gchar* base = g_path_get_basename(project);
    gchar* title = g_strdup_printf(_("Run a Task of %s"), base);
    g_free(base);
    p->dialog = app_dialog_new(title, app->window, _("_Cancel"), APP_RESPONSE_CANCEL, _("_Run"), RESPONSE_RUN, NULL);
    g_free(title);
    app_dialog_set_width(APP_DIALOG(p->dialog), 520);
    app_dialog_set_default_response(APP_DIALOG(p->dialog), RESPONSE_RUN);
    GtkWidget* area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(area, 14);
    gtk_widget_set_margin_bottom(area, 14);
    gtk_widget_set_margin_start(area, 16);
    gtk_widget_set_margin_end(area, 16);
    gtk_box_append(GTK_BOX(app_dialog_get_content_area(APP_DIALOG(p->dialog))), area);

    p->entry = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(p->entry), _("Filter the tasks"));
    g_signal_connect(p->entry, "search-changed", G_CALLBACK(on_search_changed), p);
    g_signal_connect(p->entry, "activate", G_CALLBACK(on_entry_activate), p);
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_entry_key), p);
    gtk_widget_add_controller(p->entry, keys);
    gtk_box_append(GTK_BOX(area), p->entry);

    p->filter = gtk_custom_filter_new(filter_func, p, NULL);
    p->filtered = gtk_filter_list_model_new(G_LIST_MODEL(g_object_ref(p->store)), GTK_FILTER(p->filter));
    p->selection = gtk_single_selection_new(G_LIST_MODEL(p->filtered));
    GtkListItemFactory* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_choice_setup), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(on_choice_bind), p->project);
    p->list = gtk_list_view_new(GTK_SELECTION_MODEL(p->selection), factory);
    g_signal_connect(p->list, "activate", G_CALLBACK(on_activate_row), p);
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 280);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), p->list);
    gtk_widget_add_css_class(scroll, "dialog-list");
    gtk_widget_set_margin_top(scroll, 6);
    gtk_box_append(GTK_BOX(area), scroll);

    gchar* where = g_strdup_printf(_("In %s"), project);
    GtkWidget* note = gtk_label_new(where);
    g_free(where);
    gtk_widget_add_css_class(note, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(note), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_margin_top(note, 4);
    gtk_box_append(GTK_BOX(area), note);

    gtk_single_selection_set_selected(p->selection, selected);
    g_signal_connect(p->dialog, "response", G_CALLBACK(on_response), p);
    g_object_set_data_full(G_OBJECT(p->dialog), "task-picker", p, picker_free);
    app_dialog_present(APP_DIALOG(p->dialog));
    gtk_widget_grab_focus(p->entry);
    gtk_list_view_scroll_to(GTK_LIST_VIEW(p->list), selected, GTK_LIST_SCROLL_NONE, NULL);
}

/* ------------------------------------------------------------------------- */
/* Finding the project's tasks                                               */
/* ------------------------------------------------------------------------- */

typedef struct {
    AppData* app;
    GCancellable* cancellable;
    gchar* start;    /* the folder searched from */
    gboolean choose; /* Shift+F7: ask which task */
    gchar* project;  /* found: the project's folder, or NULL */
    GPtrArray* tasks;
} FindJob;

static void find_job_free(gpointer data) {
    FindJob* job = data;
    g_clear_object(&job->cancellable);
    g_free(job->start);
    g_free(job->project);
    if (job->tasks != NULL) g_ptr_array_unref(job->tasks);
    g_free(job);
}

static void find_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    (void)cancellable;
    FindJob* job = data;
    job->project = task_detect_project_dir(job->start);
    if (job->project != NULL) job->tasks = task_detect(job->project);
    g_task_return_boolean(task, TRUE);
}

static void on_found(GObject* source, GAsyncResult* result, gpointer data) {
    (void)source;
    (void)data;
    FindJob* job = g_task_get_task_data(G_TASK(result));
    if (g_cancellable_is_cancelled(job->cancellable)) return; /* another search, or at exit */
    AppData* app = job->app;
    struct TaskRun* st = run_state(app);
    g_clear_object(&st->finding);
    if (job->project == NULL || job->tasks == NULL || job->tasks->len == 0) {
        gchar* msg = g_strdup_printf(_("No build file in %s or above (Makefile, Cargo.toml, package.json, go.mod…)"),
                                     job->start);
        notice(app, msg);
        g_free(msg);
        return;
    }
    if (job->choose) {
        show_picker(app, job->project, job->tasks);
        return;
    }
    /* The task chosen last in this project when it is still there, else the
     * default one */
    const TaskDef* chosen = g_hash_table_lookup(st->chosen, job->project);
    const TaskDef* def = g_ptr_array_index(job->tasks, 0);
    for (guint i = 0; chosen != NULL && i < job->tasks->len; i++) {
        if (task_def_equal(g_ptr_array_index(job->tasks, i), chosen)) def = chosen;
    }
    start_run(app, def);
}

/* The folder whose project F7 builds: the focused editor tab's file's, else
 * the explorer's */
static gchar* start_dir(AppData* app) {
    FocusInfo info;
    focus_route_get(app, &info);
    if (info.area == FOCUS_AREA_EDITOR && info.tab != NULL && info.tab->path != NULL && !info.tab->is_buffer) {
        return g_path_get_dirname(info.tab->path);
    }
    return g_strdup(app->base_dir);
}

static void find_tasks(AppData* app, gboolean choose) {
    struct TaskRun* st = run_state(app);
    gchar* start = start_dir(app);
    if (start == NULL) return;
    if (vfs_is_remote(start)) {
        notice(app, _("Tasks do not run on remote machines yet"));
        g_free(start);
        return;
    }
    if (st->finding != NULL) {
        g_cancellable_cancel(st->finding);
        g_clear_object(&st->finding);
    }
    FindJob* job = g_new0(FindJob, 1);
    job->app = app;
    job->start = start;
    job->choose = choose;
    job->cancellable = g_cancellable_new();
    st->finding = g_object_ref(job->cancellable);
    GTask* task = g_task_new(NULL, job->cancellable, on_found, NULL);
    g_task_set_task_data(task, job, find_job_free);
    g_task_run_in_thread(task, find_thread);
    g_object_unref(task);
}

/* The header's Run Again button: the task run last, wherever it was */
static void run_last(AppData* app) {
    struct TaskRun* st = run_state(app);
    if (st->last == NULL) {
        find_tasks(app, FALSE);
        return;
    }
    TaskDef* def = task_def_copy(st->last);
    start_run(app, def);
    task_def_free(def);
}

static void stop_action(AppData* app) {
    task_run_stop(app);
}

static void ensure_actions(void) {
    task_page_set_actions(run_last, stop_action);
}

void task_run_again(AppData* app) {
    if (app == NULL) return;
    ensure_actions();
    /* From the Build page: the task it shows */
    GtkWidget* focus = app->window != NULL ? gtk_window_get_focus(app->window) : NULL;
    if (app->build_page != NULL && focus != NULL && gtk_widget_is_ancestor(focus, app->build_page)) {
        run_last(app);
        return;
    }
    find_tasks(app, FALSE);
}

void task_run_choose(AppData* app) {
    if (app == NULL) return;
    ensure_actions();
    find_tasks(app, TRUE);
}

void task_run_free(AppData* app) {
    struct TaskRun* st = app != NULL ? app->task_run : NULL;
    if (st == NULL) return;
    if (st->finding != NULL) {
        g_cancellable_cancel(st->finding);
        g_object_unref(st->finding);
    }
    drop_current(app, TRUE);
    task_def_free(st->last);
    g_hash_table_unref(st->chosen);
    g_free(st);
    app->task_run = NULL;
}
