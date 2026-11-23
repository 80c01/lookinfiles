/* The PDF of LaTeX documents in the preview (see latex_build.h) */
#include "latex/latex_build.h"
#include "core/app_events.h"
#include "core/vfs.h"
#include "explorer/fs_watch.h"
#include "latex/latex_sync.h"
#include "preview/preview_state.h"
#include "preview/viewer.h"
#include "tasks/task_latex.h"
#include <string.h>

struct LatexBuild {
    GCancellable* finding; /* the search for a selected tab's PDF, or NULL */
};

typedef struct {
    AppData* app;
    GCancellable* cancellable;
    gchar* saved; /* the .tex file of the tab */
    gchar* pdf;   /* the PDF of its main document, or NULL */
} FindJob;

static void job_free(gpointer p) {
    FindJob* job = p;
    g_clear_object(&job->cancellable);
    g_free(job->saved);
    g_free(job->pdf);
    g_free(job);
}

static struct LatexBuild* build_state(AppData* app) {
    if (app->latex_build == NULL) app->latex_build = g_new0(struct LatexBuild, 1);
    return app->latex_build;
}

void latex_build_saved(AppData* app, const gchar* path) {
    if (app == NULL || path == NULL || !g_str_has_suffix(path, ".tex") || !app->settings.latex_on_save) return;
    g_free(app->latex_saved_path);
    app->latex_saved_path = g_strdup(path);
    app_emit(app, APP_EVENT_LATEX_SAVED);
}

void latex_build_show_built(AppData* app, const gchar* pdf) {
    if (app == NULL || pdf == NULL) return;
    if (g_strcmp0(preview_state_path(app), pdf) == 0) {
        app_emit(app, APP_EVENT_PREVIEW_STALE);
    } else {
        /* Shown as a selected file is, so that the reloads of the preview
         * (zoom, m, file changed) are of the PDF */
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
    latex_sync_after_build(app);
}

/* The PDF of the main document of the selected file, when it was built */
static void find_pdf_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancellable) {
    (void)source;
    (void)cancellable;
    FindJob* job = data;
    gchar* root = task_latex_root(job->saved);
    if (root != NULL) {
        job->pdf = task_latex_output(root, ".pdf");
        if (!vfs_file_test(job->pdf, G_FILE_TEST_IS_REGULAR)) g_clear_pointer(&job->pdf, g_free);
    }
    g_free(root);
    g_task_return_boolean(task, TRUE);
}

static void on_pdf_found(GObject* source, GAsyncResult* result, gpointer data) {
    (void)source;
    (void)data;
    FindJob* job = g_task_get_task_data(G_TASK(result));
    if (g_cancellable_is_cancelled(job->cancellable)) return; /* another tab, or at exit */
    AppData* app = job->app;
    g_clear_object(&build_state(app)->finding);
    /* Still the selected tab */
    EditorTab* tab = app->last_focused_editor;
    if (job->pdf == NULL || tab == NULL || g_strcmp0(tab->path, job->saved) != 0) return;
    if (g_strcmp0(preview_state_path(app), job->pdf) == 0) return;
    latex_build_show_built(app, job->pdf);
}

void latex_build_show_pdf(AppData* app, const gchar* path) {
    if (app == NULL || path == NULL || !g_str_has_suffix(path, ".tex")) return;
    struct LatexBuild* st = build_state(app);
    if (st->finding != NULL) {
        g_cancellable_cancel(st->finding);
        g_clear_object(&st->finding);
    }
    FindJob* job = g_new0(FindJob, 1);
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
    if (st != NULL) {
        if (st->finding != NULL) {
            g_cancellable_cancel(st->finding);
            g_object_unref(st->finding);
        }
        g_free(st);
        app->latex_build = NULL;
    }
    g_clear_pointer(&app->latex_saved_path, g_free);
}
