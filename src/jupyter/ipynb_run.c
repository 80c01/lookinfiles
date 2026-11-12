/* Running the cells of a Jupyter notebook tab (see ipynb_tab.h and
 * ipynb_tab_private.h) */
#include "jupyter/ipynb_tab_private.h"
#include "core/i18n.h"
#include "jupyter/ipynb_doc.h"
#include "jupyter/ipynb_kernel.h"
#include "theme/icons.h"

#include <string.h>

#define BRIDGE_RESOURCE "/org/lookin/jupyter/ipynb_bridge.py"
#define SPEC_KEY "lookin-ipynb-kernel-spec"

/* --- Showing what the kernel does --- */

static void update_kernel_label(IpynbTab* t) {
    IpynbKernelState state = t->kernel != NULL ? ipynb_kernel_get_state(t->kernel) : IPYNB_KERNEL_OFF;
    const gchar* text = state == IPYNB_KERNEL_STARTING ? _("Kernel starting…")
                      : state == IPYNB_KERNEL_IDLE     ? _("Kernel idle")
                      : state == IPYNB_KERNEL_BUSY     ? _("Kernel busy")
                      : state == IPYNB_KERNEL_DEAD     ? _("Kernel died")
                                                       : _("No kernel");
    gtk_menu_button_set_label(GTK_MENU_BUTTON(t->kernel_button), text);
    gboolean on = state != IPYNB_KERNEL_OFF;
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
    while (g_hash_table_iter_next(&it, &key, NULL)) queue_refresh(t, GPOINTER_TO_UINT(key));
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

static void on_started(const gchar* kernelspec, const gchar* language_info, const gchar* display, gpointer data) {
    (void)display;
    IpynbTab* t = data;
    if (ipynb_doc_set_kernel_info(t->doc, kernelspec, language_info)) {
        ipynb_tab_mark_changed(t);
        ipynb_tab_update_info(t);
    }
}

static void on_failed(const gchar* code, const gchar* message, gpointer data) {
    IpynbTab* t = data;
    gchar* text;
    if (g_strcmp0(code, "no-jupyter") == 0) {
        text = g_strdup(_("Running cells needs Jupyter: pip install jupyter_client ipykernel"));
    } else if (g_strcmp0(code, "no-kernel") == 0) {
        text = g_strdup_printf(_("Kernel not installed: %s"), message);
    } else if (g_strcmp0(code, "exited") == 0) {
        text = g_strdup(_("The kernel stopped"));
    } else {
        text = g_strdup_printf(_("Kernel error: %s"), message);
    }
    ipynb_tab_set_status(t, text, TRUE);
    g_free(text);
    if (g_strcmp0(code, "command") != 0) forget_running(t);
    update_kernel_label(t);
}

static void on_execute_input(guint key, gint count, gpointer data) {
    IpynbTab* t = data;
    gint index = ipynb_doc_find_cell(t->doc, key);
    if (index < 0) return;
    ipynb_doc_set_execution_count(t->doc, (guint)index, count);
    ipynb_tab_mark_changed(t);
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
    (void)status;
    IpynbTab* t = data;
    g_hash_table_remove(t->running, GUINT_TO_POINTER(key));
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

/* python3 -c <the bridge script>, or NULL with error set */
static gchar** bridge_argv(GError** error) {
    gchar* python = g_find_program_in_path("python3");
    if (python == NULL) python = g_find_program_in_path("python");
    if (python == NULL) {
        g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT, "%s", _("Running cells needs Python 3"));
        return NULL;
    }
    GBytes* script = g_resources_lookup_data(BRIDGE_RESOURCE, G_RESOURCE_LOOKUP_FLAGS_NONE, error);
    if (script == NULL) {
        g_free(python);
        return NULL;
    }
    gchar** argv = g_new0(gchar*, 4);
    argv[0] = python;
    argv[1] = g_strdup("-c");
    argv[2] = g_strndup(g_bytes_get_data(script, NULL), g_bytes_get_size(script));
    g_bytes_unref(script);
    return argv;
}

/* A running (or starting) kernel; FALSE when it cannot start */
static gboolean ensure_kernel(IpynbTab* t) {
    if (t->kernel != NULL && ipynb_kernel_get_state(t->kernel) != IPYNB_KERNEL_OFF) return TRUE;
    g_clear_pointer(&t->kernel, ipynb_kernel_free);
    GError* error = NULL;
    gchar** argv = bridge_argv(&error);
    if (argv != NULL) {
        t->kernel = ipynb_kernel_new(&CALLBACKS, t);
        gchar* cwd = g_path_get_dirname(t->path);
        if (!ipynb_kernel_start(t->kernel, (const gchar* const*)argv, ipynb_doc_kernel_name(t->doc), cwd, &error)) {
            g_clear_pointer(&t->kernel, ipynb_kernel_free);
        }
        g_free(cwd);
        g_strfreev(argv);
    }
    if (t->kernel == NULL) {
        ipynb_tab_set_status(t, error != NULL ? error->message : _("No kernel"), TRUE);
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

static void on_kernels_listed(GPtrArray* specs, const gchar* code, const gchar* message, gpointer data) {
    IpynbTab* t = data;
    g_clear_object(&t->list_cancel);
    if (specs == NULL) {
        gchar* text = g_strcmp0(code, "no-jupyter") == 0
                          ? g_strdup(_("Running cells needs Jupyter: pip install jupyter_client ipykernel"))
                          : g_strdup_printf(_("Kernel error: %s"), message);
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

/* The menu opens: list the kernels installed now */
static void on_kernel_menu_show(GtkWidget* popover, gpointer data) {
    (void)popover;
    IpynbTab* t = data;
    if (t->list_cancel != NULL) return; /* listing */
    GError* error = NULL;
    gchar** argv = bridge_argv(&error);
    if (argv == NULL) {
        menu_message(t, error->message);
        g_error_free(error);
        return;
    }
    menu_message(t, _("Looking for kernels…"));
    t->list_cancel = g_cancellable_new();
    ipynb_kernel_list((const gchar* const*)argv, t->list_cancel, on_kernels_listed, t);
    g_strfreev(argv);
}

/* --- Running --- */

void ipynb_run_cell(IpynbTab* t, guint index) {
    if (index >= ipynb_doc_n_cells(t->doc) || ipynb_doc_cell_type(t->doc, index) != IPYNB_CELL_CODE) return;
    guint key = ipynb_doc_cell_key(t->doc, index);
    if (g_hash_table_contains(t->running, GUINT_TO_POINTER(key)) || !ensure_kernel(t)) return;
    ipynb_doc_clear_outputs(t->doc, index);
    g_hash_table_add(t->running, GUINT_TO_POINTER(key));
    ipynb_tab_refresh_cell(t, index);
    ipynb_tab_mark_changed(t);
    gchar* source = ipynb_doc_cell_source(t->doc, index);
    ipynb_kernel_execute(t->kernel, key, source);
    g_free(source);
}

void ipynb_run_advance(IpynbTab* t) {
    guint index = t->current;
    ipynb_run_cell(t, index);
    if (index + 1 >= ipynb_doc_n_cells(t->doc)) {
        ipynb_tab_insert_cell(t, index + 1, IPYNB_CELL_CODE);
    } else {
        ipynb_tab_focus_cell(t, index + 1);
    }
}

static void on_run_all(GtkButton* button, gpointer data) {
    (void)button;
    IpynbTab* t = data;
    for (guint i = 0; i < ipynb_doc_n_cells(t->doc); i++) ipynb_run_cell(t, i);
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
    g_hash_table_remove_all(t->running);
    if (t->stale != NULL) g_hash_table_remove_all(t->stale);
}
