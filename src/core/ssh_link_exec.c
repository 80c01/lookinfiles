/* Remote commands on libssh exec channels (see ssh_link_exec.h). */
#include "core/ssh_link_exec.h"
#include "core/i18n.h"

#define CHUNK 16384

/* Reads stdout and stderr and writes stdin together, in a context of its
 * own, until all three are done */
typedef struct {
    GMainContext* ctx;
    GInputStream* src[2]; /* 0 = stdout, 1 = stderr */
    GByteArray* buf[2];
    guint8 chunk[2][CHUNK];
    GOutputStream* sink;
    GBytes* input;
    GCancellable* cancel;
    gint pending; /* reads, write and close in flight */
    GError* error; /* the first one */
} Run;

static void keep_error(Run* r, GError* e) {
    if (r->error == NULL) r->error = e;
    else g_error_free(e);
}

static void on_read(GObject* source, GAsyncResult* res, gpointer data);

static void read_next(Run* r, gint i) {
    r->pending++;
    g_input_stream_read_async(r->src[i], r->chunk[i], CHUNK, G_PRIORITY_DEFAULT, r->cancel, on_read, r);
}

static void on_read(GObject* source, GAsyncResult* res, gpointer data) {
    Run* r = data;
    gint i = G_INPUT_STREAM(source) == r->src[0] ? 0 : 1;
    GError* e = NULL;
    gssize n = g_input_stream_read_finish(G_INPUT_STREAM(source), res, &e);
    r->pending--;
    if (n < 0) {
        keep_error(r, e);
    } else if (n > 0) {
        g_byte_array_append(r->buf[i], r->chunk[i], (guint)n);
        if (r->error == NULL) read_next(r, i);
    }
}

static void on_closed(GObject* source, GAsyncResult* res, gpointer data) {
    Run* r = data;
    /* Not an error of the command: it may end without reading its input */
    g_output_stream_close_finish(G_OUTPUT_STREAM(source), res, NULL);
    r->pending--;
}

static void close_sink(Run* r) {
    r->pending++;
    g_output_stream_close_async(r->sink, G_PRIORITY_DEFAULT, r->cancel, on_closed, r);
}

static void on_written(GObject* source, GAsyncResult* res, gpointer data) {
    Run* r = data;
    r->pending--;
    /* A failed write is not an error of the command either (as above) */
    if (!g_output_stream_write_all_finish(G_OUTPUT_STREAM(source), res, NULL, NULL)) return;
    close_sink(r); /* EOF: the command reads no more */
}

static gboolean run_here(SshLink* link, const gchar* command, GBytes* input, GCancellable* cancel, gint* status,
                         GBytes** out, GBytes** err, GError** error) {
    SshChannel* ch = ssh_link_open_channel_sync(link, SSH_CHANNEL_KIND_EXEC, command, 0, 0, cancel, error);
    if (ch == NULL) return FALSE;
    Run r = {0};
    r.ctx = g_main_context_new();
    r.src[0] = g_io_stream_get_input_stream(G_IO_STREAM(ch));
    r.src[1] = ssh_channel_get_stderr(ch);
    r.buf[0] = g_byte_array_new();
    r.buf[1] = g_byte_array_new();
    r.sink = g_io_stream_get_output_stream(G_IO_STREAM(ch));
    r.cancel = cancel;
    g_main_context_push_thread_default(r.ctx); /* the channel completes there */
    read_next(&r, 0);
    read_next(&r, 1);
    gsize len = 0;
    const gchar* data = input != NULL ? g_bytes_get_data(input, &len) : NULL;
    if (len > 0) {
        r.pending++;
        g_output_stream_write_all_async(r.sink, data, len, G_PRIORITY_DEFAULT, cancel, on_written, &r);
    } else {
        close_sink(&r);
    }
    while (r.pending > 0) g_main_context_iteration(r.ctx, TRUE);
    g_main_context_pop_thread_default(r.ctx);

    gint code = -1;
    if (r.error == NULL) ssh_channel_wait_exit_sync(ch, cancel, &code, &r.error);
    if (r.error == NULL && code < 0) {
        r.error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED,
                                      _("the remote command ended without an exit status"));
    }
    ssh_channel_force_close(ch);
    g_object_unref(ch);
    g_main_context_unref(r.ctx);
    if (r.error != NULL) {
        g_propagate_error(error, r.error);
        g_byte_array_unref(r.buf[0]);
        g_byte_array_unref(r.buf[1]);
        return FALSE;
    }
    *status = code;
    *out = g_byte_array_free_to_bytes(r.buf[0]);
    if (err != NULL) *err = g_byte_array_free_to_bytes(r.buf[1]);
    else g_byte_array_unref(r.buf[1]);
    return TRUE;
}

/* The main thread waits for a worker: the channel's sync calls refuse it */
typedef struct {
    SshLink* link;
    const gchar* command;
    GBytes* input;
    GCancellable* cancel;
    gint* status;
    GBytes** out;
    GBytes** err;
    GError** error;
    gboolean ok;
} Call;

static gpointer call_main(gpointer data) {
    Call* c = data;
    c->ok = run_here(c->link, c->command, c->input, c->cancel, c->status, c->out, c->err, c->error);
    return NULL;
}

gboolean ssh_link_exec(SshLink* link, const gchar* command, GBytes* input, GCancellable* cancel, gint* status,
                       GBytes** out, GBytes** err, GError** error) {
    *out = NULL;
    if (err != NULL) *err = NULL;
    if (!g_main_context_is_owner(g_main_context_default())) {
        return run_here(link, command, input, cancel, status, out, err, error);
    }
    Call c = {link, command, input, cancel, status, out, err, error, FALSE};
    g_thread_join(g_thread_new("ssh-exec", call_main, &c));
    return c.ok;
}
