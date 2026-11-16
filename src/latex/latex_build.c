/* LaTeX documents compiled when they are saved (see latex_build.h) */
#include "latex/latex_build.h"
#include "latex/latex_errors.h"
#include "latex/latex_log.h"
#include "latex/latex_root.h"
#include "latex/latex_sync.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "explorer/fs_watch.h"
#include "preview/preview_state.h"
#include "preview/viewer.h"
#include <string.h>

/* Candidate main documents read at most, and their size */
#define ROOT_CANDIDATES 64
#define ROOT_MAX_BYTES  (1024 * 1024)
/* Log read at most */
#define LOG_MAX_BYTES (16 * 1024 * 1024)

struct LatexBuild {
    GCancellable* running; /* the compilation in progress, or NULL */
    gchar* pending;        /* file saved again meanwhile, or NULL */
    GCancellable* finding; /* the search for a selected tab's PDF, or NULL */
    gboolean told_missing; /* "no compiler" said once */
};

typedef struct {
    AppData* app;
    GCancellable* cancellable;
    gchar* saved;     /* the saved file */
    gchar* root;      /* its main document, or NULL */
    gchar* program;   /* the command run, or NULL when none is installed */
    gchar* pdf;       /* the PDF made, or NULL */
    gboolean pdf_new; /* written by this compilation */
    gboolean failed;  /* the command failed */
    GPtrArray* msgs;  /* LatexMessage* of the log */
} BuildJob;

static void job_free(gpointer p) {
    BuildJob* job = p;
    g_clear_object(&job->cancellable);
    g_free(job->saved);
    g_free(job->root);
    g_free(job->program);
    g_free(job->pdf);
    if (job->msgs != NULL) g_ptr_array_unref(job->msgs);
    g_free(job);
}

static struct LatexBuild* build_state(AppData* app) {
    if (app->latex_build == NULL) app->latex_build = g_new0(struct LatexBuild, 1);
    return app->latex_build;
}

/* ------------------------------------------------------------------------- */
/* Worker                                                                    */
/* ------------------------------------------------------------------------- */

static gchar* read_text(const gchar* path) {
    gchar* text = NULL;
    gsize len = 0;
    if (!g_file_get_contents(path, &text, &len, NULL)) return NULL;
    if (len > ROOT_MAX_BYTES) text[ROOT_MAX_BYTES] = '\0';
    return text;
}

/* The main documents of dir (prefix: how dir is written from the saved
 * file's directory, "" or "../"), excluding skip; stem: the saved file seen
 * from dir */
static void add_candidates(const gchar* dir, const gchar* prefix, const gchar* skip, const gchar* stem,
                           GPtrArray* names, GPtrArray* texts, GPtrArray* stems) {
    GDir* d = g_dir_open(dir, 0, NULL);
    if (d == NULL) return;
    GPtrArray* entries = g_ptr_array_new_with_free_func(g_free);
    const gchar* e;
    while ((e = g_dir_read_name(d)) != NULL) {
        if (g_str_has_suffix(e, ".tex")) g_ptr_array_add(entries, g_strdup(e));
    }
    g_dir_close(d);
    g_ptr_array_sort_values(entries, (GCompareFunc)g_strcmp0);
    for (guint i = 0; i < entries->len && names->len < ROOT_CANDIDATES; i++) {
        gchar* path = g_build_filename(dir, g_ptr_array_index(entries, i), NULL);
        gchar* text = g_strcmp0(path, skip) != 0 ? read_text(path) : NULL;
        if (text != NULL && latex_root_is_main(text)) {
            g_ptr_array_add(names, g_strconcat(prefix, g_ptr_array_index(entries, i), NULL));
            g_ptr_array_add(texts, text);
            g_ptr_array_add(stems, g_strdup(stem));
        } else {
            g_free(text);
        }
        g_free(path);
    }
    g_ptr_array_unref(entries);
}

/* The main document of saved (absolute), or NULL; *text: its content */
static gchar* find_root(const gchar* saved, gchar** text) {
    gchar* own = read_text(saved);
    if (own == NULL) return NULL;
    gchar* dir = g_path_get_dirname(saved);
    gchar* root = NULL;
    gchar* magic = latex_root_magic(own);
    if (magic != NULL) {
        root = g_canonicalize_filename(magic, dir);
        if (!g_file_test(root, G_FILE_TEST_IS_REGULAR) && !g_str_has_suffix(root, ".tex")) {
            gchar* with_ext = g_strconcat(root, ".tex", NULL);
            g_free(root);
            root = with_ext;
        }
        g_free(magic);
        *text = read_text(root);
        if (*text == NULL) g_clear_pointer(&root, g_free);
    } else if (latex_root_is_main(own)) {
        root = g_strdup(saved);
        *text = own;
        own = NULL;
    } else {
        /* A chapter: the document of its folder or of the one above that
         * includes it */
        gchar* base = g_path_get_basename(saved);
        base[strlen(base) - strlen(".tex")] = '\0';
        gchar* sub = g_path_get_basename(dir);
        gchar* stem_up = g_build_filename(sub, base, NULL);
        gchar* parent = g_path_get_dirname(dir);
        GPtrArray* names = g_ptr_array_new_with_free_func(g_free);
        GPtrArray* texts = g_ptr_array_new_with_free_func(g_free);
        GPtrArray* stems = g_ptr_array_new_with_free_func(g_free);
        add_candidates(dir, "", saved, base, names, texts, stems);
        if (names->len == 0 && g_strcmp0(parent, dir) != 0) {
            add_candidates(parent, "../", saved, stem_up, names, texts, stems);
        }
        gint i = latex_root_choose((const gchar* const*)names->pdata, (const gchar* const*)texts->pdata,
                                   (const gchar* const*)stems->pdata, names->len);
        if (i >= 0) {
            root = g_canonicalize_filename(g_ptr_array_index(names, i), dir);
            *text = g_strdup(g_ptr_array_index(texts, i));
        }
        g_ptr_array_unref(stems);
        g_ptr_array_unref(texts);
        g_ptr_array_unref(names);
        g_free(parent);
        g_free(stem_up);
        g_free(sub);
        g_free(base);
    }
    g_free(dir);
    g_free(own);
    return root;
}

/* The command compiling root (its file name) in its directory dir, or NULL
 * when none is installed */
static GPtrArray* build_argv(const gchar* dir, const gchar* name, const gchar* text) {
    LatexEngine engine = latex_root_engine(text);
    GPtrArray* argv = g_ptr_array_new_with_free_func(g_free);
    gchar* latexmk = g_find_program_in_path("latexmk");
    gchar* tectonic = latexmk == NULL ? g_find_program_in_path("tectonic") : NULL;
    gchar* tex = latexmk == NULL && tectonic == NULL ? g_find_program_in_path(latex_root_engine_name(engine)) : NULL;
    if (latexmk != NULL) {
        g_ptr_array_add(argv, latexmk);
        /* A latexmkrc of the project chooses the engine itself */
        gchar* rc = g_build_filename(dir, "latexmkrc", NULL);
        gchar* dot_rc = g_build_filename(dir, ".latexmkrc", NULL);
        if (!g_file_test(rc, G_FILE_TEST_EXISTS) && !g_file_test(dot_rc, G_FILE_TEST_EXISTS)) {
            g_ptr_array_add(argv, g_strdup(engine == LATEX_ENGINE_XELATEX    ? "-pdfxe"
                                           : engine == LATEX_ENGINE_LUALATEX ? "-pdflua"
                                                                             : "-pdf"));
        }
        g_free(dot_rc);
        g_free(rc);
        g_ptr_array_add(argv, g_strdup("-interaction=nonstopmode"));
        g_ptr_array_add(argv, g_strdup("-file-line-error"));
        g_ptr_array_add(argv, g_strdup("-synctex=1"));
    } else if (tectonic != NULL) {
        g_ptr_array_add(argv, tectonic);
        g_ptr_array_add(argv, g_strdup("--keep-logs"));
        g_ptr_array_add(argv, g_strdup("--synctex"));
    } else if (tex != NULL) {
        g_ptr_array_add(argv, tex);
        g_ptr_array_add(argv, g_strdup("-interaction=nonstopmode"));
        g_ptr_array_add(argv, g_strdup("-file-line-error"));
        g_ptr_array_add(argv, g_strdup("-synctex=1"));
    } else {
        g_ptr_array_unref(argv);
        return NULL;
    }
    g_ptr_array_add(argv, g_strdup(name));
    g_ptr_array_add(argv, NULL);
    return argv;
}

/* Modification time in microseconds, -1 when path is missing */
static gint64 file_mtime(const gchar* path) {
    GFile* file = g_file_new_for_path(path);
    GFileInfo* info = g_file_query_info(file, G_FILE_ATTRIBUTE_TIME_MODIFIED "," G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC,
                                        G_FILE_QUERY_INFO_NONE, NULL, NULL);
    gint64 t = -1;
    if (info != NULL) {
        t = (gint64)g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED) * G_USEC_PER_SEC +
            g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC);
        g_object_unref(info);
    }
    g_object_unref(file);
    return t;
}

/* The last line written by a failed command, as the message of the failure */
static void add_output_message(BuildJob* job, GBytes* out) {
    gsize len = 0;
    const gchar* data = out != NULL ? g_bytes_get_data(out, &len) : NULL;
    gchar* text = data != NULL ? g_utf8_make_valid(data, (gssize)len) : g_strdup("");
    gchar** lines = g_strsplit(text, "\n", -1);
    const gchar* last = NULL;
    for (gchar** l = lines; *l != NULL; l++) {
        g_strstrip(*l);
        if ((*l)[0] != '\0') last = *l;
    }
    LatexMessage* m = g_new0(LatexMessage, 1);
    m->kind = LATEX_MSG_ERROR;
    m->text = g_strdup(last != NULL ? last : _("The compilation failed"));
    g_ptr_array_add(job->msgs, m);
    g_strfreev(lines);
    g_free(text);
}

static void build_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    BuildJob* job = data;
    gchar* text = NULL;
    job->root = find_root(job->saved, &text);
    if (job->root == NULL) {
        g_task_return_boolean(task, TRUE);
        return;
    }
    gchar* dir = g_path_get_dirname(job->root);
    gchar* name = g_path_get_basename(job->root);
    gchar* stem = g_strndup(name, g_str_has_suffix(name, ".tex") ? strlen(name) - 4 : strlen(name));
    gchar* pdf = g_strdup_printf("%s/%s.pdf", dir, stem);
    GPtrArray* argv = build_argv(dir, name, text);
    g_free(text);
    if (argv != NULL) {
        job->program = g_path_get_basename(g_ptr_array_index(argv, 0));
        gint64 before = file_mtime(pdf);
        GSubprocessLauncher* launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDIN_PIPE |
                                                                  G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                                                  G_SUBPROCESS_FLAGS_STDERR_MERGE);
        g_subprocess_launcher_set_cwd(launcher, dir);
        GError* error = NULL;
        GSubprocess* proc = g_subprocess_launcher_spawnv(launcher, (const gchar* const*)argv->pdata, &error);
        g_object_unref(launcher);
        GBytes* out = NULL;
        gboolean spawned = proc != NULL;
        if (spawned) {
            /* stdin closed at once: TeX never waits for an answer (a pipe
             * needs input to communicate, even empty) */
            GBytes* none = g_bytes_new_static("", 0);
            if (!g_subprocess_communicate(proc, none, cancellable, &out, NULL, &error)) {
                g_subprocess_force_exit(proc);
            }
            g_bytes_unref(none);
            job->failed = error != NULL || !g_subprocess_get_successful(proc);
            g_object_unref(proc);
        } else {
            job->failed = TRUE;
        }
        gchar* log_path = g_strdup_printf("%s/%s.log", dir, stem);
        gchar* log = NULL;
        gsize log_len = 0;
        /* The log of this run, or of the last one when latexmk found the
         * PDF up to date */
        if (spawned && !g_cancellable_is_cancelled(cancellable) &&
            g_file_get_contents(log_path, &log, &log_len, NULL)) {
            /* Logs of 8-bit input files are not UTF-8 */
            gchar* valid = g_utf8_make_valid(log, (gssize)MIN(log_len, LOG_MAX_BYTES));
            job->msgs = latex_log_parse(valid, -1);
            g_free(valid);
        } else {
            job->msgs = latex_log_parse(NULL, 0);
        }
        if (job->failed && latex_log_count(job->msgs, LATEX_MSG_ERROR) == 0) {
            if (!spawned) {
                LatexMessage* m = g_new0(LatexMessage, 1);
                m->kind = LATEX_MSG_ERROR;
                m->text = g_strdup(error->message);
                g_ptr_array_add(job->msgs, m);
            } else {
                add_output_message(job, out);
            }
        }
        g_free(log);
        g_free(log_path);
        if (out != NULL) g_bytes_unref(out);
        g_clear_error(&error);
        gint64 after = file_mtime(pdf);
        job->pdf_new = after >= 0 && after != before;
        if (after >= 0) job->pdf = g_steal_pointer(&pdf);
        g_ptr_array_unref(argv);
    }
    g_free(pdf);
    g_free(stem);
    g_free(name);
    g_free(dir);
    g_task_return_boolean(task, TRUE);
}

/* The PDF of the main document of the selected file, when it was built */
static void find_pdf_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    (void)cancellable;
    BuildJob* job = data;
    gchar* text = NULL;
    job->root = find_root(job->saved, &text);
    g_free(text);
    if (job->root != NULL) {
        gchar* pdf = g_strdup(job->root);
        if (g_str_has_suffix(pdf, ".tex")) pdf[strlen(pdf) - strlen(".tex")] = '\0';
        job->pdf = g_strconcat(pdf, ".pdf", NULL);
        g_free(pdf);
        if (!g_file_test(job->pdf, G_FILE_TEST_IS_REGULAR)) g_clear_pointer(&job->pdf, g_free);
    }
    g_task_return_boolean(task, TRUE);
}

/* ------------------------------------------------------------------------- */
/* Main thread                                                               */
/* ------------------------------------------------------------------------- */

static void start_build(AppData* app, const gchar* path);

static void notice(AppData* app, const gchar* format, const gchar* arg) {
    gchar* msg = g_strdup_printf(format, arg);
    app_notice(app, msg, FALSE);
    g_free(msg);
}

/* Show the PDF in the preview: reloaded where it was (page, zoom) when
 * already shown, else shown as a selected file is, so that the reloads of
 * the preview (zoom, m, file changed) are of the PDF */
static void show_pdf(AppData* app, const gchar* pdf) {
    if (g_strcmp0(preview_state_path(app), pdf) == 0) {
        app_emit(app, APP_EVENT_PREVIEW_STALE);
        return;
    }
    gchar* name = g_path_get_basename(pdf);
    preview_state_set_raw(app, FALSE);
    preview_state_set_zoom(app, 1.0);
    app->file_dirty = FALSE;
    load_file_content(app, name, pdf);
    preview_state_set_file(app, pdf, name);
    preview_state_set_from_editor(app); /* the PDF of an editor tab's document */
    monitor_file(app, pdf);
    app_emit(app, APP_EVENT_STATUS_CHANGED);
    g_free(name);
}

static void on_build_done(GObject* source, GAsyncResult* result, gpointer data) {
    (void)source;
    (void)data;
    BuildJob* job = g_task_get_task_data(G_TASK(result));
    if (g_cancellable_is_cancelled(job->cancellable)) return; /* at exit: app is gone */
    AppData* app = job->app;
    struct LatexBuild* st = build_state(app);
    g_clear_object(&st->running);

    gchar* saved_name = g_path_get_basename(job->saved);
    if (job->root == NULL) {
        notice(app, _("%s: no main document found (add a “%% !TEX root = main.tex” line)"), saved_name);
    } else if (job->program == NULL) {
        if (!st->told_missing) {
            st->told_missing = TRUE;
            app_notice(app, _("No LaTeX compiler found: install latexmk (TeX Live) or Tectonic"), FALSE);
        }
    } else {
        latex_errors_show(app, job->root, job->msgs, job->program);
        guint errors = latex_log_count(job->msgs, LATEX_MSG_ERROR);
        gchar* root_name = g_path_get_basename(job->root);
        if (errors > 0) {
            gchar* msg = g_strdup_printf(ngettext("%1$s: %2$u error (LaTeX tab)", "%1$s: %2$u errors (LaTeX tab)", errors),
                                         root_name, errors);
            app_notice(app, msg, FALSE);
            g_free(msg);
        } else if (job->failed) {
            notice(app, _("%s: the compilation failed (LaTeX tab)"), root_name);
        } else {
            gchar* pdf_name = g_path_get_basename(job->pdf != NULL ? job->pdf : job->root);
            notice(app, _("%s compiled"), pdf_name);
            g_free(pdf_name);
        }
        g_free(root_name);
        if (job->pdf != NULL && job->pdf_new) {
            show_pdf(app, job->pdf);
            latex_sync_after_build(app);
        }
    }
    g_free(saved_name);

    if (st->pending != NULL) {
        gchar* next = g_steal_pointer(&st->pending);
        start_build(app, next);
        g_free(next);
    }
}

static void start_build(AppData* app, const gchar* path) {
    struct LatexBuild* st = build_state(app);
    if (st->running != NULL) {
        /* Compiled again when the running compilation ends */
        g_free(st->pending);
        st->pending = g_strdup(path);
        return;
    }
    BuildJob* job = g_new0(BuildJob, 1);
    job->app = app;
    job->saved = g_strdup(path);
    job->cancellable = g_cancellable_new();
    st->running = g_object_ref(job->cancellable);
    gchar* name = g_path_get_basename(path);
    notice(app, _("Compiling %s…"), name);
    g_free(name);
    latex_errors_compiling(app, path);
    GTask* task = g_task_new(NULL, job->cancellable, on_build_done, NULL);
    g_task_set_task_data(task, job, job_free);
    g_task_run_in_thread(task, build_thread);
    g_object_unref(task);
}

void latex_build_saved(AppData* app, const gchar* path) {
    if (app == NULL || path == NULL || !g_str_has_suffix(path, ".tex")) return;
    start_build(app, path);
}

static void on_pdf_found(GObject* source, GAsyncResult* result, gpointer data) {
    (void)source;
    (void)data;
    BuildJob* job = g_task_get_task_data(G_TASK(result));
    if (g_cancellable_is_cancelled(job->cancellable)) return; /* another tab, or at exit */
    AppData* app = job->app;
    struct LatexBuild* st = build_state(app);
    g_clear_object(&st->finding);
    /* Still the selected tab, and not compiled meanwhile */
    EditorTab* tab = app->last_focused_editor;
    if (job->pdf == NULL || tab == NULL || g_strcmp0(tab->path, job->saved) != 0 || st->running != NULL) return;
    if (g_strcmp0(preview_state_path(app), job->pdf) == 0) return;
    show_pdf(app, job->pdf);
    latex_sync_after_build(app);
}

void latex_build_show_pdf(AppData* app, const gchar* path) {
    if (app == NULL || path == NULL || !g_str_has_suffix(path, ".tex")) return;
    struct LatexBuild* st = build_state(app);
    if (st->finding != NULL) {
        g_cancellable_cancel(st->finding);
        g_clear_object(&st->finding);
    }
    BuildJob* job = g_new0(BuildJob, 1);
    job->app = app;
    job->saved = g_strdup(path);
    job->cancellable = g_cancellable_new();
    st->finding = g_object_ref(job->cancellable);
    GTask* task = g_task_new(NULL, job->cancellable, on_pdf_found, NULL);
    g_task_set_task_data(task, job, job_free);
    g_task_run_in_thread(task, find_pdf_thread);
    g_object_unref(task);
}

void latex_build_free(AppData* app) {
    struct LatexBuild* st = app->latex_build;
    if (st == NULL) return;
    if (st->finding != NULL) {
        g_cancellable_cancel(st->finding);
        g_object_unref(st->finding);
    }
    if (st->running != NULL) {
        g_cancellable_cancel(st->running);
        g_object_unref(st->running);
    }
    g_free(st->pending);
    g_free(st);
    app->latex_build = NULL;
}
