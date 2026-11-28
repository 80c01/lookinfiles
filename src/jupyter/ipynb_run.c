/* Running the cells of a Jupyter notebook tab (see ipynb_tab.h and
 * ipynb_tab_private.h) */
#include "jupyter/ipynb_tab_private.h"
#include "core/app_paths.h"
#include "core/i18n.h"
#include "core/remote_path.h"
#include "core/ssh_session.h"
#include "jupyter/ipynb_doc.h"
#include "jupyter/ipynb_kernel.h"
#include "jupyter/ipynb_python.h"
#include "core/run_time.h"
#include "theme/icons.h"
#include "widgets/dialog.h"
#include "widgets/path_suggest.h"

#include <string.h>

#define BRIDGE_RESOURCE "/org/lookin/jupyter/ipynb_bridge.py"
#define SPEC_KEY "lookin-ipynb-kernel-spec"
#define TIMING_TICK_MS 100 /* the time of a cell running: tenths of a second */

/* The last run of a cell (t->timings) */
typedef struct {
    gint64 start; /* monotonic, microseconds */
    gint64 took;  /* microseconds, or -1 while it runs */
    gboolean failed; /* ended with an error (or the kernel stopped) */
} IpynbTiming;

/* --- Run times --- */

static IpynbTiming* timing_of(IpynbTab* t, guint key) {
    if (t->timings == NULL) t->timings = g_hash_table_new_full(NULL, NULL, NULL, g_free);
    return g_hash_table_lookup(t->timings, GUINT_TO_POINTER(key));
}

static gboolean on_timing_tick(gpointer data) {
    IpynbTab* t = data;
    gboolean any = FALSE;
    GHashTableIter it;
    gpointer key, value;
    g_hash_table_iter_init(&it, t->timings);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        if (((IpynbTiming*)value)->took >= 0) continue;
        gint index = ipynb_doc_find_cell(t->doc, GPOINTER_TO_UINT(key));
        if (index >= 0) ipynb_tab_refresh_time(t, (guint)index);
        any = TRUE;
    }
    if (!any) t->timing_source = 0;
    return any ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

/* The kernel starts running the cell with key (not when it is queued) */
static void timing_start(IpynbTab* t, guint key) {
    IpynbTiming* timing = timing_of(t, key);
    if (timing == NULL) {
        timing = g_new0(IpynbTiming, 1);
        g_hash_table_insert(t->timings, GUINT_TO_POINTER(key), timing);
    }
    timing->start = g_get_monotonic_time();
    timing->took = -1;
    timing->failed = FALSE;
    if (t->timing_source == 0) t->timing_source = g_timeout_add(TIMING_TICK_MS, on_timing_tick, t);
}

/* The cell with key ended, with status "ok", "error" or "aborted" (skipped
 * after an error: shown as not run), or NULL when the kernel stopped (an
 * error): keeps the time it took */
static void timing_stop(IpynbTab* t, guint key, const gchar* status) {
    IpynbTiming* timing = timing_of(t, key);
    if (timing == NULL || timing->took >= 0) return;
    if (g_strcmp0(status, "aborted") == 0) {
        g_hash_table_remove(t->timings, GUINT_TO_POINTER(key));
        return;
    }
    timing->took = g_get_monotonic_time() - timing->start;
    timing->failed = g_strcmp0(status, "ok") != 0;
}

IpynbRunState ipynb_run_state(IpynbTab* t, guint index, gchar** text) {
    guint key = ipynb_doc_cell_key(t->doc, index);
    IpynbTiming* timing = timing_of(t, key);
    *text = NULL;
    if (timing == NULL) {
        if (!g_hash_table_contains(t->running, GUINT_TO_POINTER(key))) return IPYNB_RUN_NONE;
        *text = g_strdup(_("waiting"));
        return IPYNB_RUN_WAITING;
    }
    gchar* took = run_time_format(timing->took >= 0 ? timing->took : g_get_monotonic_time() - timing->start);
    IpynbRunState state = timing->took < 0 ? IPYNB_RUN_RUNNING : timing->failed ? IPYNB_RUN_ERROR : IPYNB_RUN_OK;
    const gchar* mark = state == IPYNB_RUN_RUNNING ? "●" : state == IPYNB_RUN_OK ? "✓" : "✗";
    *text = g_strdup_printf("%s %s", mark, took);
    g_free(took);
    return state;
}

/* --- Showing what the kernel does --- */

/* Why the kernel is not running, shown until it runs again (error: the
 * text, details: the bridge's error output or NULL) */
static void set_kernel_error(IpynbTab* t, const gchar* error, const gchar* details) {
    g_free(t->kernel_error);
    t->kernel_error = g_strdup(error);
    g_free(t->kernel_details);
    t->kernel_details = details != NULL && details[0] != '\0' ? g_strdup(details) : NULL;
}

static void update_kernel_label(IpynbTab* t) {
    IpynbKernelState state = t->kernel != NULL ? ipynb_kernel_get_state(t->kernel) : IPYNB_KERNEL_OFF;
    if (state == IPYNB_KERNEL_OFF && t->kernel_error != NULL) {
        gtk_label_set_text(GTK_LABEL(t->kernel_state), t->kernel_error);
        gtk_widget_add_css_class(t->kernel_state, "ipynb-error");
        gtk_widget_set_tooltip_text(t->kernel_state,
                                    t->kernel_details != NULL ? t->kernel_details : t->kernel_error);
        gtk_widget_set_sensitive(t->interrupt_button, FALSE);
        gtk_widget_set_sensitive(t->restart_button, FALSE);
        return;
    }
    gtk_widget_remove_css_class(t->kernel_state, "ipynb-error");
    const gchar* text = state == IPYNB_KERNEL_STARTING ? _("Kernel starting…")
                      : state == IPYNB_KERNEL_IDLE     ? _("Kernel idle")
                      : state == IPYNB_KERNEL_BUSY     ? _("Kernel busy")
                      : state == IPYNB_KERNEL_DEAD     ? _("Kernel died")
                      : ipynb_doc_kernel(t->doc) != NULL ? _("Kernel not started")
                                                         : _("No kernel");
    gboolean on = state != IPYNB_KERNEL_OFF;
    gchar* where = t->kernel_host != NULL && on ? g_strdup_printf(_("%1$s on %2$s"), text, t->kernel_host)
                                                : g_strdup(text);
    /* The Python that runs it: its environment, else its path */
    const gchar* python = on && t->kernel_python != NULL ? t->kernel_python : "";
    const gchar* which = on && t->kernel_env != NULL && t->kernel_env[0] != '\0' ? t->kernel_env : python;
    if (which[0] != '\0') {
        /* TRANSLATORS: the kernel state and its Python, e.g. "Kernel idle (project/.venv)" */
        gchar* shown = g_strdup_printf(_("%1$s (%2$s)"), where, which);
        gtk_label_set_text(GTK_LABEL(t->kernel_state), shown);
        g_free(shown);
    } else {
        gtk_label_set_text(GTK_LABEL(t->kernel_state), where);
    }
    gtk_widget_set_tooltip_text(t->kernel_state, python[0] != '\0' ? python : NULL);
    g_free(where);
    gtk_widget_set_sensitive(t->interrupt_button, on && state != IPYNB_KERNEL_DEAD);
    gtk_widget_set_sensitive(t->restart_button, on);
}

static gboolean on_refresh_idle(gpointer data) {
    IpynbTab* t = data;
    t->refresh_source = 0;
    GHashTableIter it;
    gpointer key;
    g_hash_table_iter_init(&it, t->stale);
    while (g_hash_table_iter_next(&it, &key, NULL)) {
        gint index = ipynb_doc_find_cell(t->doc, GPOINTER_TO_UINT(key));
        if (index >= 0) ipynb_tab_refresh_cell(t, (guint)index);
    }
    g_hash_table_remove_all(t->stale);
    return G_SOURCE_REMOVE;
}

/* Show the cell with key again soon: outputs written in many pieces are
 * drawn once per main loop turn */
static void queue_refresh(IpynbTab* t, guint key) {
    g_hash_table_add(t->stale, GUINT_TO_POINTER(key));
    if (t->refresh_source == 0) t->refresh_source = g_idle_add(on_refresh_idle, t);
}

/* No cell runs any more (the kernel stopped or restarted) */
static void forget_running(IpynbTab* t) {
    GHashTableIter it;
    gpointer key;
    g_hash_table_iter_init(&it, t->running);
    while (g_hash_table_iter_next(&it, &key, NULL)) {
        timing_stop(t, GPOINTER_TO_UINT(key), NULL);
        queue_refresh(t, GPOINTER_TO_UINT(key));
    }
    g_hash_table_remove_all(t->running);
    t->clear_wait = 0;
    gtk_widget_set_visible(t->input_row, FALSE);
}

/* --- Kernel events --- */

static void on_state(IpynbKernelState state, gpointer data) {
    IpynbTab* t = data;
    update_kernel_label(t);
    if (state == IPYNB_KERNEL_DEAD || state == IPYNB_KERNEL_OFF) forget_running(t);
}

static void on_started(const gchar* kernelspec, const gchar* language_info, const gchar* display,
                       const gchar* python, const gchar* env, gpointer data) {
    (void)display;
    IpynbTab* t = data;
    set_kernel_error(t, NULL, NULL);
    g_free(t->kernel_python);
    t->kernel_python = g_strdup(python);
    g_free(t->kernel_env);
    t->kernel_env = g_strdup(env);
    update_kernel_label(t);
    if (ipynb_doc_set_kernel_info(t->doc, kernelspec, language_info)) {
        ipynb_tab_mark_changed(t);
        ipynb_tab_update_info(t);
    }
}

/* Jupyter missing: where to install it (venv: the virtual environment
 * found without it, or NULL) */
static gchar* no_jupyter_text(IpynbTab* t, const gchar* venv) {
    if (venv != NULL && venv[0] != '\0') {
        return t->kernel_host != NULL
                   ? g_strdup_printf(_("Running cells needs Jupyter in the virtual environment %1$s on %2$s: "
                                       "pip install ipykernel"),
                                     venv, t->kernel_host)
                   : g_strdup_printf(_("Running cells needs Jupyter in the virtual environment %s: "
                                       "pip install ipykernel"),
                                     venv);
    }
    return t->kernel_host != NULL
               ? g_strdup_printf(_("Running cells needs Jupyter on %s: pip install jupyter_client ipykernel"),
                                 t->kernel_host)
               : g_strdup(_("Running cells needs Jupyter: pip install jupyter_client ipykernel"));
}

/* The last line of text (a traceback ends with the error) */
static const gchar* last_line(const gchar* text) {
    const gchar* nl = strrchr(text, '\n');
    return nl != NULL ? nl + 1 : text;
}

/* What a failure of the bridge (ipynb_kernel.h) means. Caller frees. */
static gchar* failure_text(IpynbTab* t, const gchar* code, const gchar* message) {
    if (g_strcmp0(code, "no-jupyter") == 0) return no_jupyter_text(t, NULL);
    if (g_strcmp0(code, "no-jupyter-venv") == 0) return no_jupyter_text(t, message);
    if (g_strcmp0(code, "no-python") == 0) {
        return t->kernel_host != NULL ? g_strdup_printf(_("Running cells needs Python 3 on %s"), t->kernel_host)
                                      : g_strdup(_("Running cells needs Python 3"));
    }
    if (g_strcmp0(code, "no-kernel") == 0) return g_strdup_printf(_("Kernel not installed: %s"), message);
    if (g_strcmp0(code, "exited") == 0) {
        /* message: what the bridge (Python, ssh) wrote last on its error output */
        return message[0] != '\0' ? g_strdup_printf(_("The kernel stopped: %s"), last_line(message))
                                  : g_strdup(_("The kernel stopped (no error message)"));
    }
    return g_strdup_printf(_("Kernel error: %s"), message);
}

static void on_failed(const gchar* code, const gchar* message, gpointer data) {
    IpynbTab* t = data;
    gchar* text = failure_text(t, code, message);
    if (g_strcmp0(code, "command") == 0) {
        ipynb_tab_set_status(t, text, TRUE); /* the kernel still runs */
    } else {
        /* Kept in the kernel's label: a later "Saved" does not hide it */
        set_kernel_error(t, text, g_strcmp0(code, "exited") == 0 ? message : NULL);
        forget_running(t);
    }
    g_free(text);
    update_kernel_label(t);
}

static void on_execute_input(guint key, gint count, gpointer data) {
    IpynbTab* t = data;
    gint index = ipynb_doc_find_cell(t->doc, key);
    if (index < 0) return;
    ipynb_doc_set_execution_count(t->doc, (guint)index, count);
    ipynb_tab_mark_changed(t);
    timing_start(t, key);
    queue_refresh(t, key);
}

/* Remove the outputs of a cell, keeping its execution count */
static void clear_outputs(IpynbTab* t, guint index) {
    gint count = ipynb_doc_cell_execution_count(t->doc, index);
    ipynb_doc_clear_outputs(t->doc, index);
    ipynb_doc_set_execution_count(t->doc, index, count);
}

static void on_output(guint key, const gchar* json, gpointer data) {
    IpynbTab* t = data;
    gint index = ipynb_doc_find_cell(t->doc, key);
    if (index < 0) return; /* deleted while it runs */
    if (t->clear_wait == key) {
        t->clear_wait = 0;
        clear_outputs(t, (guint)index);
    }
    if (!ipynb_doc_append_output(t->doc, (guint)index, json, strlen(json), NULL)) return;
    ipynb_tab_mark_changed(t);
    queue_refresh(t, key);
}

static void on_clear_output(guint key, gboolean wait, gpointer data) {
    IpynbTab* t = data;
    gint index = ipynb_doc_find_cell(t->doc, key);
    if (index < 0) return;
    if (wait) {
        t->clear_wait = key; /* at its next output: no flicker */
        return;
    }
    clear_outputs(t, (guint)index);
    ipynb_tab_mark_changed(t);
    queue_refresh(t, key);
}

static void on_done(guint key, const gchar* status, gpointer data) {
    IpynbTab* t = data;
    g_hash_table_remove(t->running, GUINT_TO_POINTER(key));
    timing_stop(t, key, status);
    if (t->clear_wait == key) t->clear_wait = 0;
    queue_refresh(t, key);
}

static void on_input_request(const gchar* prompt, gboolean password, gpointer data) {
    IpynbTab* t = data;
    gtk_label_set_text(GTK_LABEL(t->input_prompt), prompt[0] != '\0' ? prompt : _("Input:"));
    gtk_widget_set_visible(t->input_entry, !password);
    gtk_widget_set_visible(t->input_password, password);
    gtk_editable_set_text(GTK_EDITABLE(t->input_entry), "");
    gtk_editable_set_text(GTK_EDITABLE(t->input_password), "");
    gtk_widget_set_visible(t->input_row, TRUE);
    gtk_widget_grab_focus(password ? t->input_password : t->input_entry);
}

static const IpynbKernelCallbacks CALLBACKS = { on_state, on_started, on_failed, on_execute_input,
                                                on_output, on_clear_output, on_done, on_input_request };

/* --- Starting the kernel --- */

/* The file of the Pythons chosen for folders (ipynb_python.h). Caller frees. */
static gchar* choices_file(void) {
    gchar* config = app_config_dir();
    gchar* file = g_build_filename(config, "jupyter.ini", NULL);
    g_free(config);
    return file;
}

/* The Python chosen for the notebook's folder, or NULL. Caller frees. */
static gchar* chosen_python(IpynbTab* t) {
    gchar* file = choices_file();
    gchar* python = ipynb_python_chosen(file, t->dir);
    g_free(file);
    return python;
}

/* The bridge on the machine of a notebook in a mounted remote folder: ssh
 * running it there with the Python of the notebook's virtual environment
 * or python3 (ipynb_python_remote_command()), *cwd the notebook's folder on
 * the machine and t->kernel_host its name. NULL for a local notebook. */
static gchar** remote_bridge_argv(IpynbTab* t, GBytes* script, gchar** cwd) {
    SshTarget target;
    gchar* remote = NULL;
    gchar* machine = NULL;
    if (!remote_path_locate(t->app, t->dir, &target, &remote, &machine)) return NULL;
    /* Through the master connection of the machine: pipes only, no tty */
    GPtrArray* args = ssh_session_argv(&target, 0);
    gchar* text = g_strndup(g_bytes_get_data(script, NULL), g_bytes_get_size(script));
    gchar* chosen = chosen_python(t);
    g_ptr_array_add(args, ipynb_python_remote_command(text, remote, chosen));
    g_free(chosen);
    g_free(text);
    g_ptr_array_add(args, NULL);
    g_free(t->kernel_host);
    t->kernel_host = machine;
    if (cwd != NULL) {
        *cwd = remote;
    } else {
        g_free(remote);
    }
    return (gchar**)g_ptr_array_free(args, FALSE);
}

/* python -c <the bridge script> here (the Python of the notebook's virtual
 * environment, *envp then its environment, else python3), or ssh running
 * it on the remote machine of the notebook (*cwd then set to its folder
 * there); NULL with error set. Free *envp with g_strfreev(). */
static gchar** bridge_argv(IpynbTab* t, gchar** cwd, gchar*** envp, GError** error) {
    if (cwd != NULL) *cwd = NULL;
    *envp = NULL;
    GBytes* bridge = g_resources_lookup_data(BRIDGE_RESOURCE, G_RESOURCE_LOOKUP_FLAGS_NONE, error);
    if (bridge == NULL) return NULL;
    gchar** remote = remote_bridge_argv(t, bridge, cwd);
    if (remote != NULL) {
        g_bytes_unref(bridge);
        return remote;
    }
    g_clear_pointer(&t->kernel_host, g_free);
    gchar* chosen = chosen_python(t);
    gchar* python = ipynb_python_local(t->dir, chosen, envp);
    g_free(chosen);
    if (python == NULL) {
        g_clear_pointer(envp, g_strfreev);
        g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT, "%s", _("Running cells needs Python 3"));
        g_bytes_unref(bridge);
        return NULL;
    }
    gchar** argv = g_new0(gchar*, 4);
    argv[0] = python;
    argv[1] = g_strdup("-c");
    argv[2] = g_strndup(g_bytes_get_data(bridge, NULL), g_bytes_get_size(bridge));
    g_bytes_unref(bridge);
    return argv;
}

/* A running (or starting) kernel; FALSE when it cannot start */
static gboolean ensure_kernel(IpynbTab* t) {
    if (t->kernel != NULL && ipynb_kernel_get_state(t->kernel) != IPYNB_KERNEL_OFF) return TRUE;
    g_clear_pointer(&t->kernel, ipynb_kernel_free);
    /* Its Python: told when it runs */
    g_clear_pointer(&t->kernel_python, g_free);
    g_clear_pointer(&t->kernel_env, g_free);
    GError* error = NULL;
    gchar* cwd = NULL;
    gchar** envp = NULL;
    set_kernel_error(t, NULL, NULL);
    gchar** argv = bridge_argv(t, &cwd, &envp, &error);
    if (argv != NULL) {
        t->kernel = ipynb_kernel_new(&CALLBACKS, t);
        if (cwd == NULL) cwd = g_strdup(t->dir);
        if (!ipynb_kernel_start(t->kernel, (const gchar* const*)argv, (const gchar* const*)envp,
                                ipynb_doc_kernel_name(t->doc), cwd, &error)) {
            g_clear_pointer(&t->kernel, ipynb_kernel_free);
        }
        g_strfreev(argv);
    }
    g_strfreev(envp);
    g_free(cwd);
    if (t->kernel == NULL) {
        set_kernel_error(t, error != NULL ? error->message : _("No kernel"), NULL);
        g_clear_error(&error);
    }
    update_kernel_label(t);
    return t->kernel != NULL;
}

/* --- Choosing the kernel --- */

static void menu_clear(IpynbTab* t) {
    GtkWidget* child;
    while ((child = gtk_widget_get_first_child(t->kernel_menu)) != NULL) gtk_box_remove(GTK_BOX(t->kernel_menu), child);
}

static void menu_message(IpynbTab* t, const gchar* text) {
    menu_clear(t);
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 40);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_add_css_class(label, "dim-label");
    gtk_widget_set_margin_start(label, 6);
    gtk_widget_set_margin_end(label, 6);
    gtk_box_append(GTK_BOX(t->kernel_menu), label);
}

static void on_kernel_chosen(GtkButton* item, gpointer data) {
    IpynbTab* t = data;
    const IpynbKernelSpec* spec = g_object_get_data(G_OBJECT(item), SPEC_KEY);
    gtk_menu_button_popdown(GTK_MENU_BUTTON(t->kernel_button));
    gboolean on = t->kernel != NULL && ipynb_kernel_get_state(t->kernel) != IPYNB_KERNEL_OFF;
    if (on && g_strcmp0(spec->name, ipynb_doc_kernel_name(t->doc)) == 0) return; /* already that one */
    /* The kernel running goes: its variables with it */
    g_clear_pointer(&t->kernel, ipynb_kernel_free);
    forget_running(t);
    gchar* kernelspec = ipynb_kernel_spec_to_json(spec);
    gchar* language_info = ipynb_kernel_spec_language_info(spec);
    if (ipynb_doc_set_kernel_info(t->doc, kernelspec, language_info)) {
        ipynb_tab_mark_changed(t);
        ipynb_tab_update_info(t);
        ipynb_tab_rebuild_cells(t); /* highlighted with its language */
    }
    g_free(kernelspec);
    g_free(language_info);
    ensure_kernel(t);
}

/* --- Choosing the Python --- */

/* The kernel stops and starts again (if it ran) with another Python */
static void python_changed(IpynbTab* t, const gchar* python) {
    gboolean on = t->kernel != NULL && ipynb_kernel_get_state(t->kernel) != IPYNB_KERNEL_OFF;
    g_clear_pointer(&t->kernel, ipynb_kernel_free);
    forget_running(t);
    if (on) ensure_kernel(t);
    update_kernel_label(t);
    gchar* text = python != NULL && python[0] != '\0' ? g_strdup_printf(_("Python: %s"), python)
                                                     : g_strdup(_("Python: automatic"));
    ipynb_tab_set_status(t, text, FALSE);
    g_free(text);
}

/* A problem with the Python typed (local notebooks only: no wait for the
 * network here), or NULL. Caller frees. */
static gchar* python_problem(IpynbTab* t, const gchar* python) {
    if (t->kernel_host != NULL || python[0] == '\0') return NULL;
    gchar* path = g_str_has_prefix(python, "~/") ? g_build_filename(g_get_home_dir(), python + 2, NULL)
                                                 : g_strdup(python);
    gchar* problem = NULL;
    if (!g_path_is_absolute(path)) {
        problem = g_strdup(_("Type the full path of the Python program."));
    } else if (!g_file_test(path, G_FILE_TEST_IS_EXECUTABLE) || g_file_test(path, G_FILE_TEST_IS_DIR)) {
        problem = g_strdup_printf(_("%s is not a program."), python);
    }
    g_free(path);
    return problem;
}

/* Ask for the Python of the notebooks of the folder */
static void choose_python(IpynbTab* t) {
    gchar* file = choices_file();
    gchar* current = ipynb_python_chosen(file, t->dir);
    gchar* folder = remote_path_display(t->app, t->dir);
    gchar* text = g_strdup(current != NULL ? current : "");
    gchar* problem = NULL;
    for (;;) {
        GtkWidget* dlg = app_dialog_new(_("Python Interpreter"), t->app->window, _("_Cancel"), APP_RESPONSE_CANCEL,
                                        _("_Use"), APP_RESPONSE_OK, NULL);
        app_dialog_set_default_response(APP_DIALOG(dlg), APP_RESPONSE_OK);
        GtkWidget* box = ipynb_dialog_body(dlg);
        gchar* intro = g_strdup_printf(_("The Python that runs the notebooks of %s and of its subfolders:"), folder);
        ipynb_dialog_label(box, intro);
        g_free(intro);
        GtkWidget* entry = gtk_entry_new();
        gtk_editable_set_text(GTK_EDITABLE(entry), text);
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("Automatic"));
        gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
        gtk_widget_set_size_request(entry, 420, -1);
        gtk_box_append(GTK_BOX(box), entry);
        if (t->kernel_host == NULL) path_suggest_attach(entry, PATH_SUGGEST_PROGRAMS); /* local paths only */
        gchar* hint = t->kernel_host != NULL
                          ? g_strdup_printf(_("A path on %s (~/ for the home folder there). Empty: the virtual "
                                              "environment of the project (.venv, venv or env), else python3."),
                                            t->kernel_host)
                          : g_strdup(_("Empty: the virtual environment of the project (.venv, venv or env), "
                                       "else python3."));
        gtk_widget_add_css_class(ipynb_dialog_label(box, hint), "dim-label");
        g_free(hint);
        if (problem != NULL) gtk_widget_add_css_class(ipynb_dialog_label(box, problem), "error");
        gint response = app_dialog_run(APP_DIALOG(dlg), entry);
        g_free(text);
        text = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry))));
        app_dialog_destroy(APP_DIALOG(dlg));
        g_clear_pointer(&problem, g_free);
        if (response != APP_RESPONSE_OK || t->closed) break;
        if (g_strcmp0(text, current != NULL ? current : "") == 0) break; /* unchanged */
        problem = python_problem(t, text);
        if (problem != NULL) continue;
        GError* error = NULL;
        if (!ipynb_python_choose(file, t->dir, text, &error)) {
            problem = g_strdup_printf(_("Not saved: %s"), error->message);
            g_error_free(error);
            continue;
        }
        python_changed(t, text);
        break;
    }
    g_free(text);
    g_free(folder);
    g_free(current);
    g_free(file);
}

static void on_python_item(GtkButton* item, gpointer data) {
    (void)item;
    IpynbTab* t = data;
    gtk_menu_button_popdown(GTK_MENU_BUTTON(t->kernel_button));
    choose_python(t);
}

/* The last entry of the kernel menu: the Python that runs the kernels */
static void menu_add_python(IpynbTab* t) {
    gtk_box_append(GTK_BOX(t->kernel_menu), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    GtkWidget* item = gtk_button_new_with_label(_("Python Interpreter…"));
    gtk_widget_add_css_class(item, "flat");
    GtkWidget* text = gtk_button_get_child(GTK_BUTTON(item));
    if (GTK_IS_LABEL(text)) gtk_label_set_xalign(GTK_LABEL(text), 0.0f);
    gchar* python = chosen_python(t);
    gtk_widget_set_tooltip_text(item, python != NULL ? python : _("Automatic"));
    g_free(python);
    g_signal_connect(item, "clicked", G_CALLBACK(on_python_item), t);
    gtk_box_append(GTK_BOX(t->kernel_menu), item);
}

/* The kernels listed (specs NULL: why not) fill the menu */
static void fill_kernel_menu(IpynbTab* t, GPtrArray* specs, const gchar* code, const gchar* message) {
    if (specs == NULL) {
        gchar* text = failure_text(t, code, message);
        /* The whole error output: the menu has room for it */
        if (g_strcmp0(code, "exited") == 0 && strchr(message, '\n') != NULL) {
            gchar* full = g_strdup_printf("%s\n\n%s", text, message);
            g_free(text);
            text = full;
        }
        menu_message(t, text);
        g_free(text);
        return;
    }
    if (specs->len == 0) {
        menu_message(t, _("No kernel installed (e.g. pip install ipykernel)"));
        g_ptr_array_unref(specs);
        return;
    }
    menu_clear(t);
    const gchar* current = ipynb_doc_kernel_name(t->doc);
    for (guint i = 0; i < specs->len; i++) {
        IpynbKernelSpec* spec = g_ptr_array_index(specs, i);
        GtkWidget* item = gtk_button_new_with_label(spec->display_name);
        gtk_widget_add_css_class(item, "flat");
        if (g_strcmp0(spec->name, current) == 0) gtk_widget_add_css_class(item, "menu-item-current");
        GtkWidget* text = gtk_button_get_child(GTK_BUTTON(item));
        if (GTK_IS_LABEL(text)) gtk_label_set_xalign(GTK_LABEL(text), 0.0f);
        gtk_widget_set_tooltip_text(item, spec->name);
        /* The item keeps its spec */
        g_object_set_data_full(G_OBJECT(item), SPEC_KEY, g_steal_pointer(&specs->pdata[i]),
                               (GDestroyNotify)ipynb_kernel_spec_free);
        g_signal_connect(item, "clicked", G_CALLBACK(on_kernel_chosen), t);
        gtk_box_append(GTK_BOX(t->kernel_menu), item);
    }
    g_ptr_array_unref(specs);
}

static void on_kernels_listed(GPtrArray* specs, const gchar* code, const gchar* message, gpointer data) {
    IpynbTab* t = data;
    g_clear_object(&t->list_cancel);
    fill_kernel_menu(t, specs, code, message);
    menu_add_python(t);
}

/* The menu opens: list the kernels installed now */
static void on_kernel_menu_show(GtkWidget* popover, gpointer data) {
    (void)popover;
    IpynbTab* t = data;
    if (t->list_cancel != NULL) return; /* listing */
    GError* error = NULL;
    gchar** envp = NULL;
    gchar** argv = bridge_argv(t, NULL, &envp, &error);
    if (argv == NULL) {
        menu_message(t, error->message);
        menu_add_python(t);
        g_error_free(error);
        return;
    }
    menu_message(t, _("Looking for kernels…"));
    t->list_cancel = g_cancellable_new();
    ipynb_kernel_list((const gchar* const*)argv, (const gchar* const*)envp, t->list_cancel, on_kernels_listed, t);
    g_strfreev(argv);
    g_strfreev(envp);
}

/* --- Running --- */

void ipynb_run_cell(IpynbTab* t, guint index) {
    if (index >= ipynb_doc_n_cells(t->doc)) return;
    if (ipynb_doc_cell_type(t->doc, index) == IPYNB_CELL_MARKDOWN) {
        ipynb_tab_render_cell(t, index); /* running a Markdown cell renders it */
        return;
    }
    if (ipynb_doc_cell_type(t->doc, index) != IPYNB_CELL_CODE) return;
    guint key = ipynb_doc_cell_key(t->doc, index);
    if (g_hash_table_contains(t->running, GUINT_TO_POINTER(key)) || !ensure_kernel(t)) return;
    ipynb_doc_clear_outputs(t->doc, index);
    if (timing_of(t, key) != NULL) g_hash_table_remove(t->timings, GUINT_TO_POINTER(key)); /* until it starts */
    g_hash_table_add(t->running, GUINT_TO_POINTER(key));
    ipynb_tab_refresh_cell(t, index);
    ipynb_tab_mark_changed(t);
    gchar* source = ipynb_doc_cell_source(t->doc, index);
    ipynb_kernel_execute(t->kernel, key, source);
    g_free(source);
}

void ipynb_run_range(IpynbTab* t, guint index) {
    guint first, last;
    ipynb_select_range(t, index, &first, &last);
    for (guint i = first; i <= last; i++) ipynb_run_cell(t, i);
}

void ipynb_run_advance(IpynbTab* t) {
    guint first, last;
    ipynb_select_range(t, t->current, &first, &last);
    for (guint i = first; i <= last; i++) ipynb_run_cell(t, i);
    ipynb_select_clear(t);
    /* The next cell shown: past a folded section */
    guint next = ipynb_fold_step(t, last, 1);
    if (next == last) {
        ipynb_tab_insert_cell(t, ipynb_doc_n_cells(t->doc), IPYNB_CELL_CODE);
    } else {
        ipynb_tab_focus_cell(t, next);
    }
}

void ipynb_run_from(IpynbTab* t, guint index) {
    for (guint i = index; i < ipynb_doc_n_cells(t->doc); i++) ipynb_run_cell(t, i);
}

static void on_run_all(GtkButton* button, gpointer data) {
    (void)button;
    ipynb_run_from(data, 0);
}

static void on_interrupt(GtkButton* button, gpointer data) {
    (void)button;
    IpynbTab* t = data;
    if (t->kernel != NULL) ipynb_kernel_interrupt(t->kernel);
}

static void on_restart(GtkButton* button, gpointer data) {
    (void)button;
    IpynbTab* t = data;
    if (t->kernel == NULL) return;
    forget_running(t);
    ipynb_kernel_restart(t->kernel);
    ipynb_tab_set_status(t, _("Kernel restarted"), FALSE);
}

static void on_input_activate(GtkWidget* entry, gpointer data) {
    IpynbTab* t = data;
    if (t->kernel != NULL) ipynb_kernel_input(t->kernel, gtk_editable_get_text(GTK_EDITABLE(entry)));
    gtk_widget_set_visible(t->input_row, FALSE);
    ipynb_tab_focus_cell(t, t->current);
}

static GtkWidget* run_button(IpynbTab* t, const gchar* icon, const gchar* text, const gchar* tooltip, GCallback cb) {
    GtkWidget* b = icon_button_new(icon, tooltip);
    icon_button_set_content(b, icon, text);
    g_signal_connect(b, "clicked", cb, t);
    return b;
}

GtkWidget* ipynb_run_bar(IpynbTab* t, GtkWidget* bar) {
    t->stale = g_hash_table_new(NULL, NULL);
    gtk_box_append(GTK_BOX(bar), run_button(t, "go-next", _("Run All"), _("Run Every Code Cell"),
                                            G_CALLBACK(on_run_all)));
    t->interrupt_button = run_button(t, "process-stop", NULL, _("Interrupt the Kernel"), G_CALLBACK(on_interrupt));
    gtk_box_append(GTK_BOX(bar), t->interrupt_button);
    t->restart_button = run_button(t, "view-refresh", NULL, _("Restart the Kernel (Outputs Kept)"),
                                   G_CALLBACK(on_restart));
    gtk_box_append(GTK_BOX(bar), t->restart_button);
    t->kernel_button = gtk_menu_button_new();
    gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(t->kernel_button), FALSE);
    gtk_menu_button_set_always_show_arrow(GTK_MENU_BUTTON(t->kernel_button), TRUE);
    gtk_widget_set_tooltip_text(t->kernel_button, _("Choose the Kernel"));
    gtk_widget_set_margin_start(t->kernel_button, 8);
    GtkWidget* popover = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    t->kernel_menu = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 320);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), t->kernel_menu);
    gtk_popover_set_child(GTK_POPOVER(popover), scroll);
    g_signal_connect(popover, "show", G_CALLBACK(on_kernel_menu_show), t);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(t->kernel_button), popover);
    t->kernel_state = gtk_label_new(NULL);
    gtk_label_set_ellipsize(GTK_LABEL(t->kernel_state), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(t->kernel_state), 80);
    update_kernel_label(t);
    return t->kernel_button;
}

GtkWidget* ipynb_run_input_row_new(IpynbTab* t) {
    t->input_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(t->input_row, "ipynb-input");
    t->input_prompt = gtk_label_new(NULL);
    gtk_box_append(GTK_BOX(t->input_row), t->input_prompt);
    t->input_entry = gtk_entry_new();
    gtk_widget_set_hexpand(t->input_entry, TRUE);
    g_signal_connect(t->input_entry, "activate", G_CALLBACK(on_input_activate), t);
    gtk_box_append(GTK_BOX(t->input_row), t->input_entry);
    t->input_password = gtk_password_entry_new();
    gtk_widget_set_hexpand(t->input_password, TRUE);
    g_signal_connect(t->input_password, "activate", G_CALLBACK(on_input_activate), t);
    gtk_box_append(GTK_BOX(t->input_row), t->input_password);
    gtk_widget_set_visible(t->input_row, FALSE);
    return t->input_row;
}

void ipynb_run_stop(IpynbTab* t) {
    g_clear_pointer(&t->kernel, ipynb_kernel_free);
    if (t->list_cancel != NULL) {
        g_cancellable_cancel(t->list_cancel); /* no callback after */
        g_clear_object(&t->list_cancel);
    }
    if (t->refresh_source != 0) {
        g_source_remove(t->refresh_source);
        t->refresh_source = 0;
    }
    if (t->timing_source != 0) {
        g_source_remove(t->timing_source);
        t->timing_source = 0;
    }
    g_hash_table_remove_all(t->running);
    if (t->stale != NULL) g_hash_table_remove_all(t->stale);
    g_clear_pointer(&t->timings, g_hash_table_unref);
}
