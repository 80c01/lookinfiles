/* A Jupyter kernel through the bridge script (see ipynb_kernel.h). */
#include "jupyter/ipynb_kernel.h"

#include <gio/gio.h>
#include <stdarg.h>
#include <string.h>

#include "formats/json_view.h"

struct IpynbKernel {
    gint ref;                 /* the owner, and each operation in flight */
    gboolean freed;           /* the owner let go: no more callbacks */
    IpynbKernelCallbacks cb;
    gpointer data;
    GSubprocess* proc;
    GDataInputStream* out;    /* the bridge's events */
    GOutputStream* in;        /* its commands */
    GCancellable* cancel;
    GQueue writes;            /* gchar*: command lines waiting */
    gchar* writing;           /* the line being written, or NULL */
    IpynbKernelState state;
};

static IpynbKernel* kernel_ref(IpynbKernel* k) {
    k->ref++;
    return k;
}

static void kernel_unref(IpynbKernel* k) {
    if (--k->ref > 0) return;
    g_queue_clear_full(&k->writes, g_free);
    g_free(k->writing);
    /* The pipes close with their streams: the bridge sees the end of its
     * stdin and stops the kernel */
    g_clear_object(&k->out);
    g_clear_object(&k->in);
    g_clear_object(&k->proc);
    g_clear_object(&k->cancel);
    g_free(k);
}

IpynbKernel* ipynb_kernel_new(const IpynbKernelCallbacks* callbacks, gpointer data) {
    IpynbKernel* k = g_new0(IpynbKernel, 1);
    k->ref = 1;
    if (callbacks != NULL) k->cb = *callbacks;
    k->data = data;
    k->cancel = g_cancellable_new();
    g_queue_init(&k->writes);
    return k;
}

void ipynb_kernel_free(IpynbKernel* k) {
    if (k == NULL) return;
    k->freed = TRUE;
    g_cancellable_cancel(k->cancel);
    kernel_unref(k);
}

IpynbKernelState ipynb_kernel_get_state(const IpynbKernel* k) {
    return k->state;
}

static void set_state(IpynbKernel* k, IpynbKernelState state) {
    if (k->state == state) return;
    k->state = state;
    if (!k->freed && k->cb.state_changed != NULL) k->cb.state_changed(state, k->data);
}

static void failed(IpynbKernel* k, const gchar* code, const gchar* message) {
    if (!k->freed && k->cb.failed != NULL) k->cb.failed(code, message != NULL ? message : "", k->data);
}

/* --- Commands --- */

static void append_string(GString* out, const gchar* s) {
    g_string_append_c(out, '"');
    for (const guchar* p = (const guchar*)(s != NULL ? s : ""); *p != '\0'; p++) {
        switch (*p) {
        case '"': g_string_append(out, "\\\""); break;
        case '\\': g_string_append(out, "\\\\"); break;
        case '\n': g_string_append(out, "\\n"); break;
        case '\r': g_string_append(out, "\\r"); break;
        case '\t': g_string_append(out, "\\t"); break;
        default:
            if (*p < 0x20) {
                g_string_append_printf(out, "\\u%04x", *p);
            } else {
                g_string_append_c(out, (gchar)*p);
            }
        }
    }
    g_string_append_c(out, '"');
}

static void write_next(IpynbKernel* k);

static void on_written(GObject* source, GAsyncResult* res, gpointer data) {
    IpynbKernel* k = data;
    GError* error = NULL;
    gboolean ok = g_output_stream_write_all_finish(G_OUTPUT_STREAM(source), res, NULL, &error);
    g_clear_pointer(&k->writing, g_free);
    if (!ok) {
        /* The bridge is gone (its end of stdout says so) or we are freed */
        g_queue_clear_full(&k->writes, g_free);
        g_error_free(error);
    } else if (!k->freed) {
        write_next(k);
    }
    kernel_unref(k);
}

static void write_next(IpynbKernel* k) {
    if (k->writing != NULL || k->in == NULL || g_queue_is_empty(&k->writes)) return;
    k->writing = g_queue_pop_head(&k->writes);
    g_output_stream_write_all_async(k->in, k->writing, strlen(k->writing), G_PRIORITY_DEFAULT, k->cancel,
                                    on_written, kernel_ref(k));
}

/* Send {"op": op, then the given key / value pairs (strings, or a number
 * for "id")}; NULL ends the list */
static void send(IpynbKernel* k, const gchar* op, ...) {
    if (k->in == NULL) return;
    GString* line = g_string_new("{\"op\": ");
    append_string(line, op);
    va_list ap;
    va_start(ap, op);
    const gchar* key;
    while ((key = va_arg(ap, const gchar*)) != NULL) {
        g_string_append(line, ", ");
        append_string(line, key);
        g_string_append(line, ": ");
        if (strcmp(key, "id") == 0) {
            g_string_append_printf(line, "%u", va_arg(ap, guint));
        } else {
            append_string(line, va_arg(ap, const gchar*));
        }
    }
    va_end(ap);
    g_string_append(line, "}\n");
    g_queue_push_tail(&k->writes, g_string_free(line, FALSE));
    write_next(k);
}

void ipynb_kernel_execute(IpynbKernel* k, guint cell, const gchar* code) {
    send(k, "execute", "id", cell, "code", code, NULL);
}

void ipynb_kernel_input(IpynbKernel* k, const gchar* value) {
    send(k, "input", "value", value, NULL);
}

void ipynb_kernel_interrupt(IpynbKernel* k) {
    send(k, "interrupt", NULL);
}

void ipynb_kernel_restart(IpynbKernel* k) {
    send(k, "restart", NULL);
}

/* --- Events --- */

static gchar* member_string(const JsonValue* v, const gchar* key) {
    return json_value_dup_string(json_value_get(v, key));
}

static guint member_uint(const JsonValue* v, const gchar* key) {
    gdouble n = json_value_get_number(json_value_get(v, key));
    return n > 0 && n < G_MAXUINT ? (guint)n : 0;
}

static gboolean member_true(const JsonValue* v, const gchar* key) {
    gchar* s = member_string(v, key);
    gboolean yes = g_strcmp0(s, "true") == 0;
    g_free(s);
    return yes;
}

static void handle_event(IpynbKernel* k, const gchar* line, gsize len) {
    g_autoptr(JsonValue) v = json_value_parse(line, len, NULL);
    if (v == NULL || json_value_get_type(v) != JSON_VALUE_OBJECT) return;
    gchar* ev = member_string(v, "ev");
    guint cell = member_uint(v, "id");
    if (g_strcmp0(ev, "status") == 0) {
        gchar* s = member_string(v, "state");
        if (g_strcmp0(s, "starting") == 0) set_state(k, IPYNB_KERNEL_STARTING);
        if (g_strcmp0(s, "idle") == 0) set_state(k, IPYNB_KERNEL_IDLE);
        if (g_strcmp0(s, "busy") == 0) set_state(k, IPYNB_KERNEL_BUSY);
        if (g_strcmp0(s, "dead") == 0) set_state(k, IPYNB_KERNEL_DEAD);
        g_free(s);
    } else if (g_strcmp0(ev, "started") == 0) {
        gchar* spec = member_string(v, "kernelspec");
        gchar* info = member_string(v, "language_info");
        gchar* display = member_string(v, "display");
        if (!k->freed && k->cb.started != NULL) k->cb.started(spec, info, display, k->data);
        g_free(spec);
        g_free(info);
        g_free(display);
    } else if (g_strcmp0(ev, "error") == 0) {
        gchar* code = member_string(v, "code");
        gchar* message = member_string(v, "message");
        if (g_strcmp0(code, "command") != 0) set_state(k, IPYNB_KERNEL_OFF);
        failed(k, code != NULL ? code : "start", message);
        g_free(code);
        g_free(message);
    } else if (g_strcmp0(ev, "execute_input") == 0) {
        const JsonValue* count = json_value_get(v, "count");
        gint n = json_value_get_type(count) == JSON_VALUE_NUMBER ? (gint)json_value_get_number(count) : -1;
        if (!k->freed && k->cb.execute_input != NULL) k->cb.execute_input(cell, n, k->data);
    } else if (g_strcmp0(ev, "output") == 0) {
        gchar* json = member_string(v, "json");
        if (json != NULL && !k->freed && k->cb.output != NULL) k->cb.output(cell, json, k->data);
        g_free(json);
    } else if (g_strcmp0(ev, "clear_output") == 0) {
        if (!k->freed && k->cb.clear_output != NULL) k->cb.clear_output(cell, member_true(v, "wait"), k->data);
    } else if (g_strcmp0(ev, "done") == 0) {
        gchar* status = member_string(v, "status");
        if (!k->freed && k->cb.done != NULL) k->cb.done(cell, status != NULL ? status : "ok", k->data);
        g_free(status);
    } else if (g_strcmp0(ev, "input_request") == 0) {
        gchar* prompt = member_string(v, "prompt");
        if (!k->freed && k->cb.input_request != NULL) {
            k->cb.input_request(prompt != NULL ? prompt : "", member_true(v, "password"), k->data);
        }
        g_free(prompt);
    }
    g_free(ev);
}

static void on_line(GObject* source, GAsyncResult* res, gpointer data) {
    IpynbKernel* k = data;
    gsize len = 0;
    GError* error = NULL;
    gchar* line = g_data_input_stream_read_line_finish_utf8(G_DATA_INPUT_STREAM(source), res, &len, &error);
    if (k->freed) {
        g_free(line);
        g_clear_error(&error);
        kernel_unref(k);
        return;
    }
    if (line == NULL) {
        /* End of its stdout: the bridge exited (or crashed) */
        gboolean was_on = k->state != IPYNB_KERNEL_OFF;
        set_state(k, IPYNB_KERNEL_OFF);
        g_clear_object(&k->in);
        if (was_on) failed(k, "exited", error != NULL ? error->message : NULL);
        g_clear_error(&error);
        kernel_unref(k);
        return;
    }
    handle_event(k, line, len);
    g_free(line);
    if (k->freed || k->out == NULL) {
        kernel_unref(k);
        return;
    }
    /* The next line, with the same reference */
    g_data_input_stream_read_line_async(k->out, G_PRIORITY_DEFAULT, k->cancel, on_line, k);
}

gboolean ipynb_kernel_start(IpynbKernel* k, const gchar* const* argv, const gchar* kernel_name,
                            const gchar* cwd, GError** error) {
    if (k->proc != NULL) return TRUE;
    k->proc = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE, error);
    if (k->proc == NULL) return FALSE;
    k->in = g_object_ref(g_subprocess_get_stdin_pipe(k->proc));
    k->out = g_data_input_stream_new(g_subprocess_get_stdout_pipe(k->proc));
    g_data_input_stream_set_newline_type(k->out, G_DATA_STREAM_NEWLINE_TYPE_LF);
    set_state(k, IPYNB_KERNEL_STARTING);
    send(k, "start", "kernel", kernel_name != NULL ? kernel_name : "", "cwd", cwd != NULL ? cwd : "", NULL);
    g_data_input_stream_read_line_async(k->out, G_PRIORITY_DEFAULT, k->cancel, on_line, kernel_ref(k));
    return TRUE;
}

/* --- The installed kernels --- */

void ipynb_kernel_spec_free(IpynbKernelSpec* spec) {
    if (spec == NULL) return;
    g_free(spec->name);
    g_free(spec->display_name);
    g_free(spec->language);
    g_free(spec);
}

gchar* ipynb_kernel_spec_to_json(const IpynbKernelSpec* spec) {
    GString* out = g_string_new("{\"display_name\": ");
    append_string(out, spec->display_name);
    g_string_append(out, ", \"language\": ");
    append_string(out, spec->language);
    g_string_append(out, ", \"name\": ");
    append_string(out, spec->name);
    g_string_append_c(out, '}');
    return g_string_free(out, FALSE);
}

gchar* ipynb_kernel_spec_language_info(const IpynbKernelSpec* spec) {
    if (spec->language == NULL || spec->language[0] == '\0') return NULL;
    GString* out = g_string_new("{\"name\": ");
    append_string(out, spec->language);
    g_string_append_c(out, '}');
    return g_string_free(out, FALSE);
}

typedef struct {
    IpynbKernelListFunc done;
    gpointer data;
    GCancellable* cancel; /* owned ref, or NULL */
    GError* error;        /* the bridge could not run */
} ListCall;

static void list_call_free(ListCall* call) {
    g_clear_object(&call->cancel);
    g_clear_error(&call->error);
    g_free(call);
}

/* The kernels of the bridge's events (out), or NULL with *code and
 * *message set */
static GPtrArray* parse_list(const gchar* out, gchar** code, gchar** message) {
    gchar** lines = g_strsplit(out != NULL ? out : "", "\n", -1);
    GPtrArray* specs = NULL;
    for (gchar** l = lines; *l != NULL && specs == NULL && *code == NULL; l++) {
        g_autoptr(JsonValue) v = json_value_parse(*l, strlen(*l), NULL);
        if (v == NULL || json_value_get_type(v) != JSON_VALUE_OBJECT) continue;
        gchar* ev = member_string(v, "ev");
        if (g_strcmp0(ev, "error") == 0) {
            *code = member_string(v, "code");
            *message = member_string(v, "message");
        } else if (g_strcmp0(ev, "kernels") == 0) {
            const JsonValue* list = json_value_get(v, "kernels");
            specs = g_ptr_array_new_with_free_func((GDestroyNotify)ipynb_kernel_spec_free);
            for (guint i = 0; json_value_get_type(list) == JSON_VALUE_ARRAY && i < json_value_get_length(list); i++) {
                const JsonValue* item = json_value_get_item(list, i);
                gchar* name = member_string(item, "name");
                if (name == NULL || name[0] == '\0') {
                    g_free(name);
                    continue;
                }
                IpynbKernelSpec* spec = g_new0(IpynbKernelSpec, 1);
                spec->name = name;
                spec->display_name = member_string(item, "display_name");
                if (spec->display_name == NULL) spec->display_name = g_strdup(name);
                spec->language = member_string(item, "language");
                if (spec->language == NULL) spec->language = g_strdup("");
                g_ptr_array_add(specs, spec);
            }
        }
        g_free(ev);
    }
    g_strfreev(lines);
    if (specs == NULL && *code == NULL) *code = g_strdup("exited");
    return specs;
}

static void on_listed(GObject* source, GAsyncResult* res, gpointer data) {
    ListCall* call = data;
    gchar* out = NULL;
    GError* error = NULL;
    gboolean ok = g_subprocess_communicate_utf8_finish(G_SUBPROCESS(source), res, &out, NULL, &error);
    if (!ok && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_error_free(error);
        list_call_free(call);
        return;
    }
    gchar* code = NULL;
    gchar* message = NULL;
    GPtrArray* specs = NULL;
    if (ok) {
        specs = parse_list(out, &code, &message);
    } else {
        code = g_strdup("start");
        message = g_strdup(error->message);
    }
    call->done(specs, code, message != NULL ? message : "", call->data);
    g_clear_error(&error);
    g_free(out);
    g_free(code);
    g_free(message);
    list_call_free(call);
}

static gboolean on_list_failed(gpointer data) {
    ListCall* call = data;
    if (call->cancel == NULL || !g_cancellable_is_cancelled(call->cancel)) {
        call->done(NULL, "start", call->error->message, call->data);
    }
    list_call_free(call);
    return G_SOURCE_REMOVE;
}

void ipynb_kernel_list(const gchar* const* argv, GCancellable* cancel, IpynbKernelListFunc done, gpointer data) {
    ListCall* call = g_new0(ListCall, 1);
    call->done = done;
    call->data = data;
    if (cancel != NULL) call->cancel = g_object_ref(cancel);
    GPtrArray* args = g_ptr_array_new();
    for (const gchar* const* a = argv; *a != NULL; a++) g_ptr_array_add(args, (gpointer)*a);
    g_ptr_array_add(args, (gpointer)"--list");
    g_ptr_array_add(args, NULL);
    GSubprocess* proc = g_subprocess_newv((const gchar* const*)args->pdata,
                                          G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE,
                                          &call->error);
    g_ptr_array_unref(args);
    if (proc == NULL) {
        g_idle_add(on_list_failed, call); /* in the main loop, as when it runs */
        return;
    }
    g_subprocess_communicate_utf8_async(proc, NULL, cancel, on_listed, call);
    g_object_unref(proc);
}
