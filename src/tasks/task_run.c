/* Build and test tasks run from the keyboard (see task_run.h) */
#include "tasks/task_run.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "core/vfs.h"
#include "code/code_intel.h"
#include "editor/editor.h"
#include "editor/editor_modes.h"
#include "editor/editor_watch.h"
#include "tasks/task_choice.h"
#include "tasks/task_detect.h"
#include "tasks/task_page.h"
#include "tasks/task_picker.h"
#include "tasks/task_remote.h"
#include "tasks/task_latex.h"
#include "latex/latex_build.h"
#include "latex/latex_log.h"
#include "tasks/task_parse.h"
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
    GSubprocess* proc;  /* here, or NULL */
    TaskRemote* remote; /* on a remote machine, or NULL */
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
    gboolean success;  /* once exited */
    gint exit_status;  /* once exited: -1 when unknown */
    gboolean stopped; /* by the user */
    gboolean quiet;   /* a LaTeX document saved: the Build tab stays behind */
    gboolean log_read;     /* LaTeX: its .log was read */
    GPtrArray* latex_msgs; /* LaTeX: the messages of its .log (TaskMessage*, files here), or NULL */
    gchar* pdf;            /* LaTeX: its PDF, when there is one */
    gboolean dead;    /* replaced by another run, or the application quits */
} Run;

struct TaskRun {
    GHashTable* chosen;    /* project folder -> TaskDef* chosen last there */
    TaskDef* last;         /* the task run last (Run Again) */
    gboolean told_no_tex;  /* "no LaTeX compiler" said once */
    gchar* last_project;   /* its project's folder */
    Run* current;          /* the task running, or NULL */
    GCancellable* finding; /* the search for the project's tasks, or NULL */
};

static struct TaskRun* run_state(AppData* app) {
    if (app->task_run == NULL) {
        app->task_run = g_new0(struct TaskRun, 1);
        gchar* path = task_choice_path();
        app->task_run->chosen = task_choice_load(path); /* the choices of the last sessions */
        g_free(path);
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
    g_clear_pointer(&r->remote, task_remote_free);
    g_clear_object(&r->in);
    g_clear_object(&r->cancellable);
    task_parser_free(r->parser);
    g_clear_object(&r->page);
    g_string_free(r->partial, TRUE);
    g_free(r->last_line);
    if (r->latex_msgs != NULL) g_ptr_array_unref(r->latex_msgs);
    g_free(r->pdf);
    g_free(r);
}

/* Send sig to the task and what it started (its process group) */
static void signal_run(Run* r, gboolean kill_now) {
    if (r->exited) return;
    if (r->remote != NULL) {
        task_remote_stop(r->remote, kill_now);
        return;
    }
    if (r->proc == NULL) return;
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

/* m as seen here: on a remote machine, its file (a folder of the machine)
 * where the machine is seen, or in the text when it is outside; *out holds
 * the strings to free (task_message_clear()) */
static const TaskMessage* message_here(Run* r, const TaskMessage* m, TaskMessage* out) {
    memset(out, 0, sizeof *out);
    if (r->remote == NULL || m->file == NULL) return m;
    *out = *m;
    out->file = task_remote_local_path(r->remote, m->file);
    out->text = out->file != NULL ? g_strdup(m->text)
                : m->line > 0    ? g_strdup_printf("%s:%d: %s", m->file, m->line, m->text)
                                 : g_strdup_printf("%s: %s", m->file, m->text);
    return out;
}

static void task_message_clear(TaskMessage* m) {
    g_free(m->file);
    g_free(m->text);
}

static void task_message_free(gpointer p) {
    task_message_clear(p);
    g_free(p);
}

/* The page shows the messages the parser added since it had shown
 * `before` of them */
static void show_new_messages(Run* r, guint before) {
    const GPtrArray* msgs = task_parser_messages(r->parser);
    for (guint i = before; i < msgs->len; i++) {
        TaskMessage here;
        task_page_add_message(r->page, message_here(r, g_ptr_array_index(msgs, i), &here));
        task_message_clear(&here);
    }
}

/* A complete line of output (without its newline) into the parser and the
 * page's text */
static void take_line(Run* r, const gchar* line, gsize len, GString* shown) {
    gchar* raw = g_strndup(line, len);
    guint before = task_parser_messages(r->parser)->len;
    task_parser_feed(r->parser, raw);
    /* LaTeX: the messages come from its .log, at the end */
    if (r->def->tex_root == NULL) show_new_messages(r, before);
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

/* The messages of r with a file go to the diagnostics: underlined in the
 * editor tabs, listed in the Problems tab, until the next task */
static void publish_diagnostics(Run* r, const GPtrArray* msgs, gboolean local) {
    GPtrArray* entries = g_ptr_array_new_with_free_func((GDestroyNotify)code_diag_entry_free);
    gchar* source = g_path_get_basename(r->def->shell ? "sh" : r->def->argv[0]);
    for (guint i = 0; i < msgs->len; i++) {
        TaskMessage here = {0};
        const TaskMessage* m = local ? g_ptr_array_index(msgs, i) : message_here(r, g_ptr_array_index(msgs, i), &here);
        if (m->file == NULL || m->line <= 0) {
            task_message_clear(&here);
            continue;
        }
        CodeDiagEntry* e = g_new0(CodeDiagEntry, 1);
        e->path = g_strdup(m->file);
        e->diag.line = m->line - 1;
        e->diag.column = MAX(m->column - 1, 0);
        e->diag.end_line = e->diag.line;
        e->diag.end_column = e->diag.column;
        e->diag.severity = m->kind == TASK_MSG_ERROR     ? LSP_SEVERITY_ERROR
                           : m->kind == TASK_MSG_WARNING ? LSP_SEVERITY_WARNING
                                                         : LSP_SEVERITY_INFO;
        e->diag.message = g_strdup(m->text);
        e->diag.source = g_strdup(source);
        g_ptr_array_add(entries, e);
        task_message_clear(&here);
    }
    code_intel_set_task_diagnostics(r->app, entries);
    g_ptr_array_unref(entries);
    g_free(source);
}

static guint count_kind(const GPtrArray* msgs, TaskMsgKind kind) {
    guint n = 0;
    for (guint i = 0; i < msgs->len; i++) n += ((const TaskMessage*)g_ptr_array_index(msgs, i))->kind == kind;
    return n;
}

static void finish_run(Run* r) {
    AppData* app = r->app;
    struct TaskRun* st = run_state(app);
    guint before = task_parser_messages(r->parser)->len;
    task_parser_finish(r->parser);
    const GPtrArray* msgs = r->latex_msgs;
    if (msgs != NULL) {
        for (guint i = 0; i < msgs->len; i++) task_page_add_message(r->page, g_ptr_array_index(msgs, i));
    } else {
        show_new_messages(r, before);
        msgs = task_parser_messages(r->parser);
    }

    gboolean success = !r->stopped && r->success;
    gint status = !r->stopped ? r->exit_status : -1;
    guint errors = count_kind(msgs, TASK_MSG_ERROR);
    guint warnings = count_kind(msgs, TASK_MSG_WARNING);
    /* A failure with no message: its last line says why */
    if (!success && !r->stopped && errors == 0 && r->last_line != NULL) {
        TaskMessage m = {TASK_MSG_ERROR, NULL, 0, 0, r->last_line, 0};
        task_page_add_message(r->page, &m);
        errors = 1;
    }
    gint64 usec = g_get_monotonic_time() - r->started;
    task_page_finish(r->page, success, status, r->stopped, usec, errors, warnings);
    publish_diagnostics(r, msgs, r->latex_msgs != NULL);
    /* LaTeX: its PDF in the preview, else the Build tab says why */
    if (r->def->tex_root != NULL && !r->stopped) {
        if (r->pdf != NULL) latex_build_show_built(app, r->pdf);
        else task_page_present(r->page);
    }

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

typedef struct {
    Run* r; /* a reference */
    gchar* root;
    GPtrArray* msgs; /* LatexMessage* of the log */
    gchar* pdf;      /* when it exists */
} LogJob;

static void log_job_free(gpointer data) {
    LogJob* job = data;
    g_free(job->root);
    if (job->msgs != NULL) g_ptr_array_unref(job->msgs);
    g_free(job->pdf);
    g_free(job);
}

/* Log of a LaTeX document read at most */
#define LOG_MAX_BYTES (16 * 1024 * 1024)

static void log_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    (void)cancellable;
    LogJob* job = data;
    gchar* log_path = task_latex_output(job->root, ".log");
    gchar* log = NULL;
    gsize len = 0;
    if (vfs_file_get_contents(log_path, LOG_MAX_BYTES, &log, &len, NULL)) {
        /* Logs of 8-bit input files are not UTF-8 */
        gchar* valid = g_utf8_make_valid(log, (gssize)len);
        job->msgs = latex_log_parse(valid, -1);
        g_free(valid);
        g_free(log);
    }
    g_free(log_path);
    job->pdf = task_latex_output(job->root, ".pdf");
    if (!vfs_file_test(job->pdf, G_FILE_TEST_IS_REGULAR)) g_clear_pointer(&job->pdf, g_free);
    g_task_return_boolean(task, TRUE);
}

static void finish_run(Run* r);

static void on_log_read(GObject* source, GAsyncResult* result, gpointer data) {
    (void)source;
    (void)data;
    LogJob* job = g_task_get_task_data(G_TASK(result));
    Run* r = job->r;
    if (!r->dead) {
        gchar* dir = g_path_get_dirname(job->root);
        r->latex_msgs = g_ptr_array_new_with_free_func(task_message_free);
        /* Errors first, then warnings and bad boxes, each in log order */
        for (gint k = LATEX_MSG_ERROR; job->msgs != NULL && k <= LATEX_MSG_BADBOX; k++) {
            for (guint i = 0; i < job->msgs->len; i++) {
                const LatexMessage* lm = g_ptr_array_index(job->msgs, i);
                if ((gint)lm->kind != k) continue;
                TaskMessage* m = g_new0(TaskMessage, 1);
                m->kind = lm->kind == LATEX_MSG_ERROR ? TASK_MSG_ERROR
                          : lm->kind == LATEX_MSG_WARNING ? TASK_MSG_WARNING
                                                         : TASK_MSG_NOTE;
                m->line = lm->line;
                gchar* file = lm->file == NULL           ? NULL
                              : !g_path_is_absolute(lm->file) ? g_canonicalize_filename(lm->file, dir)
                              : r->remote != NULL         ? task_remote_local_path(r->remote, lm->file)
                                                          : g_strdup(lm->file);
                gchar* text = lm->context != NULL && lm->context[0] != '\0'
                                  ? g_strdup_printf("%s — %s", lm->text, lm->context)
                                  : g_strdup(lm->text);
                if (file == NULL && lm->file != NULL) {
                    /* A file of the machine not seen here: named in the text */
                    m->text = g_strdup_printf("%s:%d: %s", lm->file, lm->line, text);
                    g_free(text);
                } else {
                    m->text = text;
                }
                m->file = file;
                g_ptr_array_add(r->latex_msgs, m);
            }
        }
        g_free(dir);
        r->pdf = g_steal_pointer(&job->pdf);
        r->log_read = TRUE;
        finish_run(r);
    }
    run_unref(r);
}

static void maybe_finish(Run* r) {
    if (r->dead || !r->output_done || !r->exited) return;
    if (r->def->tex_root != NULL && !r->stopped && !r->log_read) {
        /* Its messages are in the .log (warnings, bad boxes too), read in a worker */
        LogJob* job = g_new0(LogJob, 1);
        job->r = run_ref(r);
        job->root = g_strdup(r->def->tex_root);
        GTask* task = g_task_new(NULL, NULL, on_log_read, NULL);
        g_task_set_task_data(task, job, log_job_free);
        g_task_run_in_thread(task, log_thread);
        g_object_unref(task);
        return;
    }
    finish_run(r);
}

/* The end of the output: the last line without a newline */
static void end_output(Run* r) {
    GString* shown = g_string_new(NULL);
    if (r->partial->len > 0) take_line(r, r->partial->str, r->partial->len, shown);
    g_string_truncate(r->partial, 0);
    task_page_append_output(r->page, shown->str);
    g_string_free(shown, TRUE);
    r->output_done = TRUE;
}

/* Output read (any cut): its complete lines taken, the rest kept */
static void take_chunk(Run* r, const gchar* data_bytes, gsize len) {
    GString* shown = g_string_new(NULL);
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
    if (len == 0) {
        /* The end, or a read error */
        if (bytes != NULL) g_bytes_unref(bytes);
        end_output(r);
        maybe_finish(r);
        run_unref(r);
        return;
    }
    take_chunk(r, data_bytes, len);
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
    r->success = g_subprocess_get_successful(r->proc);
    r->exit_status = g_subprocess_get_if_exited(r->proc) ? g_subprocess_get_exit_status(r->proc) : -1;
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

/* tab edits a file under dir and is not saved */
static gboolean unsaved_tab_under(const EditorTab* tab, const gchar* dir) {
    if (tab == NULL || tab->path == NULL || tab->buffer == NULL || tab->is_buffer) return FALSE;
    if (!gtk_text_buffer_get_modified(tab->buffer)) return FALSE;
    gsize len = strlen(dir);
    return strncmp(tab->path, dir, len) == 0 && tab->path[len] == G_DIR_SEPARATOR;
}

/* Unsaved editor tabs of files under dir */
static guint unsaved_under(AppData* app, const gchar* dir) {
    guint n = 0;
    for (GList* l = app->editors; l != NULL; l = l->next) {
        if (unsaved_tab_under(l->data, dir)) n++;
    }
    return n;
}

/* Save the edited tabs of files under dir, as Ctrl+S does (a file changed
 * on disk meanwhile asks first) without moving the focus; returns how many
 * were saved */
static guint save_under(AppData* app, const gchar* dir) {
    guint saved = 0;
    GList* tabs = g_list_copy(app->editors); /* a reload may change the list */
    for (GList* l = tabs; l != NULL; l = l->next) {
        EditorTab* tab = l->data;
        /* The views of a file share its buffer: saved once */
        if (g_list_find(app->editors, tab) == NULL || !unsaved_tab_under(tab, dir)) continue;
        if (editor_watch_before_save(app, tab) != EDITOR_WATCH_SAVE_OK) continue;
        GError* error = NULL;
        if (editor_tab_write(tab, tab->path, &error)) {
            gtk_text_buffer_set_modified(tab->buffer, FALSE);
            editor_tab_label_update(tab);
            saved++;
        } else {
            gchar* msg = g_strdup_printf(_("Cannot save %s: %s"), tab->name, error->message);
            notice(app, msg);
            g_free(msg);
            g_error_free(error);
        }
    }
    g_list_free(tabs);
    if (saved > 0) app_emit(app, APP_EVENT_FILES_CHANGED);
    return saved;
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
    if (r->remote != NULL) {
        /* Its calls stop: the reference they held goes */
        g_clear_pointer(&r->remote, task_remote_free);
        run_unref(r);
    }
    if (!quiet) task_page_finish(r->page, FALSE, -1, TRUE, g_get_monotonic_time() - r->started, 0, 0);
    run_unref(r);
}

/* The parser of def's output, the tools in dir */
static TaskParser* new_parser(const TaskDef* def, const gchar* dir) {
    TaskParser* parser = task_parser_new(dir);
    if (def->matcher != NULL) task_parser_add_matcher(parser, def->matcher); /* checked when read */
    return parser;
}

/* def run here (page: its Build page, taken), or NULL when it did not start */
static Run* start_local(AppData* app, const TaskDef* def, GtkWidget* page) {
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
        return NULL;
    }

    GPtrArray* argv = g_ptr_array_new();
    g_ptr_array_add(argv, program);
    for (gchar** a = def->argv + 1; *a != NULL; a++) g_ptr_array_add(argv, *a);
    g_ptr_array_add(argv, NULL);
    GSubprocessLauncher* launcher =
        g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                  G_SUBPROCESS_FLAGS_STDERR_MERGE);
    g_subprocess_launcher_set_cwd(launcher, def->dir);
    for (gchar** e = def->env; e != NULL && *e != NULL; e++) {
        gchar** kv = g_strsplit(*e, "=", 2);
        if (kv[0] != NULL && kv[1] != NULL) g_subprocess_launcher_setenv(launcher, kv[0], kv[1], TRUE);
        g_strfreev(kv);
    }
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
        return NULL;
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
    r->parser = new_parser(def, def->dir);
    r->page = page;
    r->partial = g_string_new(NULL);
    r->started = g_get_monotonic_time();
    g_subprocess_wait_async(proc, NULL, on_exited, run_ref(r));
    run_ref(r);
    read_next(r);
    return r;

}

static void on_remote_ready(gpointer data, const gchar* remote_dir) {
    Run* r = data;
    /* The tools write the folders of the machine */
    task_parser_free(r->parser);
    r->parser = new_parser(r->def, remote_dir);
}

static void on_remote_output(gpointer data, const gchar* bytes, gsize len) {
    take_chunk(data, bytes, len);
}

static void on_remote_done(gpointer data, gint status, const gchar* error) {
    Run* r = data;
    end_output(r);
    if (error != NULL) {
        /* Why it did not run, as the failure's message */
        g_free(r->last_line);
        r->last_line = g_strdup(error);
    }
    r->exited = TRUE;
    r->success = error == NULL && status == 0;
    r->exit_status = error == NULL ? status : -1;
    if (r->kill_id != 0) {
        g_source_remove(r->kill_id);
        r->kill_id = 0;
    }
    maybe_finish(r);
    run_unref(r); /* its calls' reference */
}

/* def run on the remote machine its folder is on (page: taken) */
static Run* start_remote(AppData* app, const TaskDef* def, GtkWidget* page) {
    static const TaskRemoteFuncs funcs = {on_remote_ready, on_remote_output, on_remote_done};
    Run* r = g_new0(Run, 1);
    r->refs = 2; /* st->current and the calls of r->remote */
    r->app = app;
    r->def = task_def_copy(def);
    r->cancellable = g_cancellable_new();
    r->parser = new_parser(def, def->dir);
    r->page = page;
    r->partial = g_string_new(NULL);
    r->started = g_get_monotonic_time();
    r->remote = task_remote_start(def, &funcs, r);
    return r;
}

/* Run def, a task of the project in folder project; quiet: a LaTeX
 * document saved (nothing else saved first, the Build tab stays behind) */
static void start_run(AppData* app, const TaskDef* def, const gchar* project, gboolean quiet) {
    struct TaskRun* st = run_state(app);
    drop_current(app, FALSE);
    task_def_free(st->last);
    st->last = task_def_copy(def);
    if (st->last_project != project) {
        g_free(st->last_project);
        st->last_project = g_strdup(project);
    }
    guint saved = !quiet && app->settings.task_save_first ? save_under(app, project) : 0;
    code_intel_set_task_diagnostics(app, NULL); /* the last task's messages are old now */

    /* A LaTeX document: its PDF in the preview comes first */
    GtkWidget* page = task_page_begin(app, def->label, def->dir, !quiet && def->tex_root == NULL);
    Run* r = vfs_is_remote(def->dir) ? start_remote(app, def, page) : start_local(app, def, page);
    if (r == NULL) return;
    r->quiet = quiet;
    st->current = r;

    guint unsaved = unsaved_under(app, project);
    gchar* msg = unsaved > 0 ? g_strdup_printf(ngettext("Running %1$s… (%2$u unsaved file)",
                                                         "Running %1$s… (%2$u unsaved files)", unsaved),
                                               def->label, unsaved)
                 : saved > 0 ? g_strdup_printf(ngettext("Running %1$s… (%2$u file saved)",
                                                        "Running %1$s… (%2$u files saved)", saved),
                                               def->label, saved)
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

/* Shift+F7 chose def among the tasks of project: kept as its choice (also
 * for the next sessions), then run */
static void run_chosen(AppData* app, const TaskDef* def, const gchar* project) {
    struct TaskRun* st = run_state(app);
    g_hash_table_replace(st->chosen, g_strdup(project), task_def_copy(def));
    gchar* path = task_choice_path();
    GError* error = NULL;
    if (!task_choice_save(path, st->chosen, &error)) {
        g_printerr("[ Task choice not saved: %s ]\n", error->message);
        g_error_free(error);
    }
    g_free(path);
    start_run(app, def, project, FALSE);
}

/* ------------------------------------------------------------------------- */
/* Finding the project's tasks                                               */
/* ------------------------------------------------------------------------- */

typedef struct {
    AppData* app;
    GCancellable* cancellable;
    gchar* start;    /* the folder searched from */
    gchar* start_file; /* the .tex file of the focused tab, or NULL */
    gboolean choose; /* Shift+F7: ask which task */
    gboolean quiet;  /* start_file was saved: compile it, quietly */
    gchar* tex_root; /* found: the main document of start_file, or NULL */
    guint n_latex;   /* the LaTeX tasks among tasks (first) */
    gchar* project;  /* found: the project's folder, or NULL */
    GPtrArray* tasks;
    gchar* problem;  /* what is wrong with its tasks file, or NULL */
} FindJob;

static void find_job_free(gpointer data) {
    FindJob* job = data;
    g_clear_object(&job->cancellable);
    g_free(job->start);
    g_free(job->start_file);
    g_free(job->tex_root);
    g_free(job->project);
    g_free(job->problem);
    if (job->tasks != NULL) g_ptr_array_unref(job->tasks);
    g_free(job);
}

/* The processors of the remote machine of dir, 0 for this computer's (or
 * when the machine does not say) */
static guint remote_processors(const gchar* dir) {
    if (!vfs_is_remote(dir)) return 0;
    gint status = 0;
    GBytes* out = NULL;
    guint n = 0;
    if (vfs_exec(dir, "nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null", NULL, NULL, &status, &out, NULL) &&
        status == 0) {
        gsize len = 0;
        const gchar* data = g_bytes_get_data(out, &len);
        gchar* text = g_strndup(data, len);
        gint64 v = g_ascii_strtoll(text, NULL, 10);
        n = v > 0 && v < 4096 ? (guint)v : 0;
        g_free(text);
    }
    if (out != NULL) g_bytes_unref(out);
    return n;
}

static void find_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    (void)cancellable;
    FindJob* job = data;
    /* A .tex file: the tasks of its document first */
    job->tex_root = job->start_file != NULL ? task_latex_root(job->start_file) : NULL;
    GPtrArray* latex = job->tex_root != NULL ? task_latex_tasks(job->tex_root) : NULL;
    if (!job->quiet) {
        job->project = task_detect_project_dir(job->start);
        if (job->project != NULL) job->tasks = task_detect(job->project, remote_processors(job->project), &job->problem);
    }
    if (latex != NULL && latex->len > 0) {
        if (job->project == NULL) job->project = g_path_get_dirname(job->tex_root);
        if (job->tasks == NULL) job->tasks = g_ptr_array_new_with_free_func((GDestroyNotify)task_def_free);
        for (guint i = 0; i < latex->len; i++) g_ptr_array_insert(job->tasks, (gint)i, task_def_copy(g_ptr_array_index(latex, i)));
        job->n_latex = latex->len;
    }
    if (latex != NULL) g_ptr_array_unref(latex);
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
    if (job->start_file != NULL && job->n_latex == 0 && (job->quiet || job->tasks == NULL || job->tasks->len == 0)) {
        gchar* name = g_path_get_basename(job->start_file);
        if (job->tex_root == NULL) {
            gchar* msg = g_strdup_printf(_("%s: no main document found (add a “%% !TEX root = main.tex” line)"), name);
            notice(app, msg);
            g_free(msg);
        } else if (!st->told_no_tex || !job->quiet) {
            st->told_no_tex = TRUE;
            notice(app, _("No LaTeX compiler found: install latexmk (TeX Live) or Tectonic (the Diagnostics tab of the + menu gives the command)"));
        }
        g_free(name);
        return;
    }
    if (job->quiet) {
        /* A save compiles the document: the LaTeX task chosen in the project,
         * else its first; never in place of another task running */
        if (st->current != NULL && st->current->def->tex_root == NULL) {
            gchar* msg = g_strdup_printf(_("%s is running: the document is compiled with F7 once it ends"),
                                         st->current->def->label);
            notice(app, msg);
            g_free(msg);
            return;
        }
        const TaskDef* chosen = g_hash_table_lookup(st->chosen, job->project);
        const TaskDef* def = g_ptr_array_index(job->tasks, 0);
        for (guint i = 0; chosen != NULL && i < job->n_latex; i++) {
            const TaskDef* t = g_ptr_array_index(job->tasks, i);
            if (t->tex_root != NULL && task_def_equal(t, chosen)) def = t;
        }
        start_run(app, def, job->project, TRUE);
        return;
    }
    if (job->problem != NULL) {
        /* Said, and the other tasks still offered */
        gchar* msg = g_strdup_printf(_("Tasks file left out: %s"), job->problem);
        notice(app, msg);
        g_free(msg);
        if (job->tasks == NULL || job->tasks->len == 0) return;
    }
    if (job->project == NULL || job->tasks == NULL || job->tasks->len == 0) {
        gchar* msg = g_strdup_printf(_("No build file in %s or above (Makefile, Cargo.toml, package.json, go.mod…)"),
                                     job->start);
        notice(app, msg);
        g_free(msg);
        return;
    }
    if (job->choose) {
        task_picker_show(app, job->project, job->tasks, g_hash_table_lookup(st->chosen, job->project), run_chosen);
        return;
    }
    /* The task chosen last in this project when it is still there, else the
     * default one */
    const TaskDef* chosen = g_hash_table_lookup(st->chosen, job->project);
    const TaskDef* def = g_ptr_array_index(job->tasks, 0);
    for (guint i = 0; chosen != NULL && i < job->tasks->len; i++) {
        /* The task as found now: its matcher too, which is not kept */
        if (task_def_equal(g_ptr_array_index(job->tasks, i), chosen)) def = g_ptr_array_index(job->tasks, i);
    }
    start_run(app, def, job->project, FALSE);
}

/* The folder whose project F7 builds: the focused editor tab's file's, else
 * the explorer's; *tex: the tab's file when it is a .tex file */
static gchar* start_dir(AppData* app, gchar** tex) {
    *tex = NULL;
    FocusInfo info;
    focus_route_get(app, &info);
    if (info.area == FOCUS_AREA_EDITOR && info.tab != NULL && info.tab->path != NULL && !info.tab->is_buffer) {
        if (g_str_has_suffix(info.tab->path, ".tex")) *tex = g_strdup(info.tab->path);
        return g_path_get_dirname(info.tab->path);
    }
    return g_strdup(app->base_dir);
}

/* Find the tasks from the folder start (taken) and the .tex file tex
 * (taken, may be NULL), then run one or (choose) ask which */
static void find_tasks_from(AppData* app, gchar* start, gchar* tex, gboolean choose, gboolean quiet) {
    struct TaskRun* st = run_state(app);
    if (start == NULL) {
        g_free(tex);
        return;
    }
    if (st->finding != NULL) {
        g_cancellable_cancel(st->finding);
        g_clear_object(&st->finding);
    }
    FindJob* job = g_new0(FindJob, 1);
    job->app = app;
    job->start = start;
    job->start_file = tex;
    job->choose = choose;
    job->quiet = quiet;
    job->cancellable = g_cancellable_new();
    st->finding = g_object_ref(job->cancellable);
    GTask* task = g_task_new(NULL, job->cancellable, on_found, NULL);
    g_task_set_task_data(task, job, find_job_free);
    g_task_run_in_thread(task, find_thread);
    g_object_unref(task);
}

static void find_tasks(AppData* app, gboolean choose) {
    gchar* tex = NULL;
    gchar* start = start_dir(app, &tex);
    find_tasks_from(app, start, tex, choose, FALSE);
}

/* The header's Run Again button: the task run last, wherever it was */
static void run_last(AppData* app) {
    struct TaskRun* st = run_state(app);
    if (st->last == NULL) {
        find_tasks(app, FALSE);
        return;
    }
    TaskDef* def = task_def_copy(st->last);
    gchar* project = g_strdup(st->last_project);
    start_run(app, def, project, FALSE);
    g_free(project);
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

void task_run_latex_saved(AppData* app) {
    if (app == NULL || app->latex_saved_path == NULL) return;
    ensure_actions();
    gchar* path = g_steal_pointer(&app->latex_saved_path);
    find_tasks_from(app, g_path_get_dirname(path), path, FALSE, TRUE);
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
    g_free(st->last_project);
    g_hash_table_unref(st->chosen);
    g_free(st);
    app->task_run = NULL;
}
