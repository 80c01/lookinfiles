#ifndef IPYNB_KERNEL_H
#define IPYNB_KERNEL_H

#include <gio/gio.h>

/* A Jupyter kernel run for a notebook through the bridge script
 * (data/jupyter/ipynb_bridge.py: jupyter_client in a Python process),
 * spoken to in JSON lines over its stdin / stdout. Everything is
 * asynchronous: commands are queued, events come back as callbacks in the
 * main loop. The kernel stops with ipynb_kernel_free(), or when the
 * application goes (the bridge sees the end of its stdin). GIO only, no
 * widgets; unit-tested with a fake bridge. */

typedef enum {
    IPYNB_KERNEL_OFF,      /* not started, failed, or the bridge exited */
    IPYNB_KERNEL_STARTING,
    IPYNB_KERNEL_IDLE,
    IPYNB_KERNEL_BUSY,
    IPYNB_KERNEL_DEAD      /* the kernel process died: restart it */
} IpynbKernelState;

typedef struct {
    void (*state_changed)(IpynbKernelState state, gpointer data);
    /* Running: its kernelspec and language_info (JSON objects) and name */
    void (*started)(const gchar* kernelspec, const gchar* language_info, const gchar* display, gpointer data);
    /* Could not start or went away; code: "no-python", "no-jupyter",
     * "no-kernel", "start", "exited", "command" */
    void (*failed)(const gchar* code, const gchar* message, gpointer data);
    void (*execute_input)(guint cell, gint count, gpointer data);
    /* An nbformat output object (JSON) of the cell */
    void (*output)(guint cell, const gchar* output_json, gpointer data);
    void (*clear_output)(guint cell, gboolean wait, gpointer data);
    /* The cell finished: status "ok", "error" or "aborted" (not run) */
    void (*done)(guint cell, const gchar* status, gpointer data);
    /* The code asks for a line (input()): answer with ipynb_kernel_input() */
    void (*input_request)(const gchar* prompt, gboolean password, gpointer data);
} IpynbKernelCallbacks;

typedef struct IpynbKernel IpynbKernel;

/* A kernel not started yet; callbacks (any may be NULL) get data */
IpynbKernel* ipynb_kernel_new(const IpynbKernelCallbacks* callbacks, gpointer data);
/* Stop the kernel (if running) and free; no callback is called after */
void ipynb_kernel_free(IpynbKernel* kernel);

/* Run the bridge (argv: the program and its arguments, e.g. python3 -c
 * <script>) and start the kernel named kernel_name ("" or NULL: the
 * default one) in directory cwd. FALSE with error set when the bridge
 * cannot be run; a kernel that cannot start is reported to failed(). */
gboolean ipynb_kernel_start(IpynbKernel* kernel, const gchar* const* argv, const gchar* kernel_name,
                            const gchar* cwd, GError** error);
IpynbKernelState ipynb_kernel_get_state(const IpynbKernel* kernel);

/* Queue the code of cell (a key the callbacks give back) to run */
void ipynb_kernel_execute(IpynbKernel* kernel, guint cell, const gchar* code);
void ipynb_kernel_input(IpynbKernel* kernel, const gchar* value);
void ipynb_kernel_interrupt(IpynbKernel* kernel);
/* Restart the kernel: the cells waiting are done() "aborted" */
void ipynb_kernel_restart(IpynbKernel* kernel);

/* --- The installed kernels --- */

typedef struct {
    gchar* name;          /* e.g. "python3": the kernelspec name */
    gchar* display_name;  /* e.g. "Python 3 (ipykernel)" */
    gchar* language;      /* e.g. "python", "" if unknown */
} IpynbKernelSpec;

void ipynb_kernel_spec_free(IpynbKernelSpec* spec);
/* The kernelspec metadata of a notebook for spec (a JSON object). Caller frees. */
gchar* ipynb_kernel_spec_to_json(const IpynbKernelSpec* spec);
/* Its language_info metadata ({"name": language}, until the kernel runs and
 * tells the rest), or NULL when the language is unknown. Caller frees. */
gchar* ipynb_kernel_spec_language_info(const IpynbKernelSpec* spec);

/* Done listing: specs (IpynbKernelSpec*, sorted by display name; owned by
 * the callee, free with g_ptr_array_unref()) or NULL, with code as in
 * failed() ("no-jupyter", "command", "start") and message */
typedef void (*IpynbKernelListFunc)(GPtrArray* specs, const gchar* code, const gchar* message, gpointer data);
/* Run the bridge (argv as for ipynb_kernel_start(), "--list" added) to
 * list the installed kernels, then call done in the main loop; not called
 * once cancel is cancelled */
void ipynb_kernel_list(const gchar* const* argv, GCancellable* cancel, IpynbKernelListFunc done, gpointer data);

#endif /* IPYNB_KERNEL_H */
