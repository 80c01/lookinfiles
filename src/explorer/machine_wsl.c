#include "explorer/machine_wsl.h"
#include "core/app_events.h"
#include "core/i18n.h"
#include "explorer/explorer_tree.h"
#include "explorer/local_machine.h"
#include "explorer/tree_nav.h"
#include "terminal/terminal.h"
#include "theme/palette.h"

/* The distributions listed (NULL until then), whether a listing runs or
 * is done, whether the WSL row is collapsed (session only) */
static struct {
    AppData* app;
    GPtrArray* distros;
    gboolean listed;
    gboolean loading;
    gboolean collapsed;
} mw;

static void redraw(void) {
    if (mw.app != NULL && mw.app->sidebar_view == SIDEBAR_VIEW_REMOTE) {
        app_emit(mw.app, APP_EVENT_FILES_CHANGED); /* rebuilds the Machines view */
    }
}

/* ------------------------------------------------------------------------- */
/* Listing with wsl.exe on a worker thread                                   */
/* ------------------------------------------------------------------------- */

#ifdef G_OS_WIN32
/* What wsl.exe writes for argv, as UTF-8, NULL when it cannot run */
static gchar* run_wsl(const gchar* const* argv) {
    GSubprocessLauncher* launcher =
        g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
    g_subprocess_launcher_setenv(launcher, "WSL_UTF8", "1", TRUE);
    GSubprocess* proc = g_subprocess_launcher_spawnv(launcher, argv, NULL);
    g_object_unref(launcher);
    if (proc == NULL) {
        return NULL;
    }
    GBytes* out = NULL;
    gchar* text = NULL;
    if (g_subprocess_communicate(proc, NULL, NULL, &out, NULL, NULL) && out != NULL) {
        gsize len = 0;
        const gchar* data = g_bytes_get_data(out, &len);
        text = wsl_output_to_utf8(data, len);
    }
    g_clear_pointer(&out, g_bytes_unref);
    g_object_unref(proc);
    return text;
}

static void list_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancel) {
    (void)source;
    (void)data;
    (void)cancel;
    static const gchar* const verbose_argv[] = {"wsl.exe", "-l", "-v", NULL};
    static const gchar* const running_argv[] = {"wsl.exe", "-l", "--running", "-q", NULL};
    gchar* verbose = run_wsl(verbose_argv);
    GPtrArray* distros = wsl_distros_parse(verbose);
    g_free(verbose);
    if (distros->len > 0) {
        gchar* running = run_wsl(running_argv);
        wsl_distros_mark_running(distros, running);
        g_free(running);
    }
    g_task_return_pointer(task, distros, (GDestroyNotify)g_ptr_array_unref);
}

static void on_listed(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    (void)data;
    mw.loading = FALSE;
    mw.listed = TRUE;
    gboolean had = (mw.distros != NULL && mw.distros->len > 0);
    g_clear_pointer(&mw.distros, g_ptr_array_unref);
    mw.distros = g_task_propagate_pointer(G_TASK(res), NULL);
    if (had || (mw.distros != NULL && mw.distros->len > 0)) {
        redraw();
    }
}
#endif

static void maybe_list(void) {
#ifdef G_OS_WIN32
    if (mw.listed || mw.loading) {
        return;
    }
    mw.loading = TRUE;
    GTask* task = g_task_new(NULL, NULL, on_listed, NULL);
    g_task_run_in_thread(task, list_thread);
    g_object_unref(task);
#endif
}

void machine_wsl_forget(void) {
    mw.listed = FALSE;
}

/* ------------------------------------------------------------------------- */
/* Rows                                                                      */
/* ------------------------------------------------------------------------- */

static void append_distro_row(ExplorerRow* parent, const WslDistro* d) {
    ExplorerRow* row = explorer_row_append(parent, WSL_DISTRO_ROW, d->name, d->name, d->name);
    explorer_row_set_glyph(row, d->running ? "computer-running" : "computer");
    const gchar* state = d->running ? _("running") : _("stopped");
    gchar* text = (d->version > 0) ? g_strdup_printf("WSL %d · %s", d->version, state) : g_strdup(state);
    if (d->is_default) {
        gchar* with = g_strdup_printf(_("%s · default"), text);
        g_free(text);
        text = with;
    }
    gchar* badge = g_markup_printf_escaped("<span foreground=\"%s\">%s</span>",
                                           palette_color(d->running ? PAL_SUCCESS : PAL_FG_SUBTLE), text);
    explorer_row_set_badge(row, badge);
    g_free(badge);
    g_free(text);
}

void machine_wsl_append(ExplorerRow* local_row, AppData* app) {
    mw.app = app;
    maybe_list();
    if (local_row == NULL || mw.distros == NULL || mw.distros->len == 0) {
        return;
    }
    ExplorerRow* group = explorer_row_append_container(local_row, MACHINE_WSL_ROW, "WSL", "WSL", "");
    explorer_row_set_glyph(group, "computer");
    gchar* count = g_strdup_printf("<span foreground=\"%s\">%u</span>", palette_color(PAL_FG_FAINT),
                                   mw.distros->len);
    explorer_row_set_badge(group, count);
    g_free(count);
    for (guint i = 0; i < mw.distros->len; i++) {
        append_distro_row(group, g_ptr_array_index(mw.distros, i));
    }
}

void machine_wsl_restore_expanded(AppData* app, ExplorerRow* root) {
    if (app == NULL || app->tree == NULL || root == NULL || mw.collapsed) {
        return;
    }
    for (guint i = 0; i < explorer_row_n_children(root); i++) {
        ExplorerRow* machine = explorer_row_child(root, i);
        if (!explorer_row_is(machine, LOCAL_MACHINE_ROW) || !explorer_tree_is_expanded(app->tree, machine)) {
            continue;
        }
        for (guint j = 0; j < explorer_row_n_children(machine); j++) {
            ExplorerRow* group = explorer_row_child(machine, j);
            if (explorer_row_is(group, MACHINE_WSL_ROW)) {
                explorer_tree_set_expanded(app->tree, group, TRUE);
            }
        }
    }
}

gboolean machine_wsl_row_toggled(ExplorerRow* row, gboolean expanded) {
    if (!explorer_row_is(row, MACHINE_WSL_ROW)) {
        return FALSE;
    }
    mw.collapsed = !expanded;
    return TRUE;
}

void machine_wsl_set_expanded(gboolean expanded) {
    mw.collapsed = !expanded;
}

/* ------------------------------------------------------------------------- */
/* Selection and keys                                                        */
/* ------------------------------------------------------------------------- */

gboolean machine_wsl_owns_selected(AppData* app) {
    ExplorerRow* row = (app != NULL) ? tree_nav_selected(app) : NULL;
    return explorer_row_is(row, MACHINE_WSL_ROW) || explorer_row_is(row, WSL_DISTRO_ROW);
}

const WslDistro* machine_wsl_find(const gchar* name) {
    return wsl_distros_find(mw.distros, name);
}

const WslDistro* machine_wsl_selected(AppData* app) {
    ExplorerRow* row = (app != NULL) ? tree_nav_selected(app) : NULL;
    return explorer_row_is(row, WSL_DISTRO_ROW) ? machine_wsl_find(row->path) : NULL;
}

void machine_wsl_activate_selected(AppData* app) {
    ExplorerRow* row = (app != NULL) ? tree_nav_selected(app) : NULL;
    if (explorer_row_is(row, MACHINE_WSL_ROW)) {
        explorer_tree_toggle_expanded(app->tree, row);
        return;
    }
    const WslDistro* d = machine_wsl_selected(app);
    if (d == NULL) {
        return;
    }
    /* Reading its folder starts the distribution */
    gchar* path = wsl_distro_path(d->name);
    ((WslDistro*)d)->running = TRUE;
    local_machine_open_dir(app, path);
    g_free(path);
}

void machine_wsl_terminal_selected(AppData* app) {
    const WslDistro* d = machine_wsl_selected(app);
    if (d == NULL) {
        return;
    }
    const gchar* argv[] = {"wsl.exe", "-d", d->name, "--cd", "~", NULL};
    ((WslDistro*)d)->running = TRUE;
    local_machine_make_current(app);
    app_emit(app, APP_EVENT_CHROME_CHANGED);
    terminal_open_command(app, argv, NULL);
}

static void on_stopped(GObject* source, GAsyncResult* res, gpointer data) {
    gchar* name = data;
    GError* error = NULL;
    if (g_subprocess_wait_check_finish(G_SUBPROCESS(source), res, &error)) {
        gchar* msg = g_strdup_printf(_("%s stopped"), name);
        app_notice(mw.app, msg, FALSE);
        g_free(msg);
    } else {
        /* TRANSLATORS: %1$s is a WSL distribution, %2$s the reason */
        gchar* msg = g_strdup_printf(_("Could not stop %1$s: %2$s"), name, error->message);
        app_notice(mw.app, msg, FALSE);
        g_free(msg);
        g_clear_error(&error);
    }
    g_free(name);
    machine_wsl_forget();
    redraw();
}

void machine_wsl_stop_selected(AppData* app) {
    const WslDistro* d = machine_wsl_selected(app);
    if (d == NULL || !d->running) {
        return;
    }
    const gchar* argv[] = {"wsl.exe", "--terminate", d->name, NULL};
    GError* error = NULL;
    GSubprocess* proc = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
                                          &error);
    if (proc == NULL) {
        gchar* msg = g_strdup_printf(_("Could not stop %1$s: %2$s"), d->name, error->message);
        app_notice(app, msg, FALSE);
        g_free(msg);
        g_clear_error(&error);
        return;
    }
    g_subprocess_wait_check_async(proc, NULL, on_stopped, g_strdup(d->name));
    g_object_unref(proc);
}
