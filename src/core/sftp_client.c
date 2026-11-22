/* SFTP v3 client over a pair of streams (see sftp_client.h). */
#include "core/sftp_client.h"
#include "core/i18n.h"

#include <string.h>

#define CHUNK 32768u          /* bytes per READ / WRITE: what every server takes */
#define WINDOW 16u            /* READ / WRITE requests in flight per file */
#define MAX_PACKET (4u << 20) /* a longer reply is a broken connection */
#define ERR_TAIL 2048         /* bytes of ssh's error output kept */
#define POSIX_RENAME "posix-rename@openssh.com"

typedef void (*ReplyFunc)(SftpClient* c, guint8 type, SftpReader* r, const GError* error, gpointer data);

typedef struct {
    ReplyFunc func;
    gpointer data;
} Pending;

struct SftpClient {
    gint refs; /* the caller, the reads and writes running, the operations */
    GInputStream* in;
    GOutputStream* out;
    GSubprocess* proc; /* ssh, or NULL */
    GCancellable* io;
    GMainContext* context;  /* where the client was made: its callbacks run there */
    gboolean ready;         /* the server's VERSION came */
    GError* error;          /* set once the connection is gone */
    GHashTable* extensions; /* name -> data */
    guint32 next_id;
    GHashTable* pending; /* id -> Pending */
    GQueue held;         /* packets waiting for VERSION */
    GQueue outq;         /* packets to write */
    GBytes* writing;
    guint8 head[4];
    guint8* body;
    guint32 body_len;
    guint8 err_buf[512];
    GString* err_tail; /* what ssh wrote on its standard error */
};

static SftpClient* client_ref(SftpClient* c) {
    c->refs++;
    return c;
}

static void client_unref(SftpClient* c) {
    if (--c->refs > 0) return;
    g_clear_object(&c->in);
    g_clear_object(&c->out);
    g_clear_object(&c->proc);
    g_clear_object(&c->io);
    g_main_context_unref(c->context);
    g_clear_error(&c->error);
    g_hash_table_unref(c->extensions);
    g_hash_table_unref(c->pending);
    g_queue_clear_full(&c->held, (GDestroyNotify)g_bytes_unref);
    g_queue_clear_full(&c->outq, (GDestroyNotify)g_bytes_unref);
    if (c->writing != NULL) g_bytes_unref(c->writing);
    g_free(c->body);
    g_string_free(c->err_tail, TRUE);
    g_free(c);
}

void sftp_entry_free(gpointer entry) {
    SftpEntry* e = entry;
    if (e == NULL) return;
    g_free(e->name);
    g_free(e);
}

/* ------------------------------------------------------------------------- */
/* The connection                                                            */
/* ------------------------------------------------------------------------- */

/* The connection is gone: every request waiting gets err (taken) */
static void lose(SftpClient* c, GError* err) {
    if (c->error != NULL) {
        g_error_free(err);
        return;
    }
    client_ref(c);
    c->error = err;
    g_cancellable_cancel(c->io);
    g_queue_clear_full(&c->held, (GDestroyNotify)g_bytes_unref);
    g_queue_clear_full(&c->outq, (GDestroyNotify)g_bytes_unref);
    GHashTable* pending = c->pending;
    c->pending = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    GHashTableIter it;
    gpointer value = NULL;
    g_hash_table_iter_init(&it, pending);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
        Pending* p = value;
        p->func(c, 0, NULL, c->error, p->data);
    }
    g_hash_table_unref(pending);
    if (c->proc != NULL) g_subprocess_force_exit(c->proc);
    client_unref(c);
}

/* The error when the server stops answering: what ssh said, if anything */
static GError* closed_error(SftpClient* c, GError* io_error) {
    gchar* said = g_utf8_make_valid(c->err_tail->str, (gssize)c->err_tail->len);
    g_strstrip(said);
    GError* e = NULL;
    if (said[0] != '\0') {
        e = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED, said);
    } else if (io_error != NULL && !g_error_matches(io_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        e = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED, io_error->message);
    } else {
        e = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED,
                                _("the connection to the remote machine was closed"));
    }
    g_free(said);
    if (io_error != NULL) g_error_free(io_error);
    return e;
}

static GError* bad_reply(void) {
    return g_error_new_literal(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                               _("the remote machine sent a reply that makes no sense"));
}

static void write_next(SftpClient* c);

static void on_written(GObject* src, GAsyncResult* res, gpointer data) {
    SftpClient* c = data;
    GError* err = NULL;
    gboolean ok = g_output_stream_write_all_finish(G_OUTPUT_STREAM(src), res, NULL, &err);
    g_clear_pointer(&c->writing, g_bytes_unref);
    if (!ok) {
        lose(c, closed_error(c, err));
    } else {
        write_next(c);
    }
    client_unref(c);
}

static void write_next(SftpClient* c) {
    if (c->writing != NULL || c->error != NULL || g_queue_is_empty(&c->outq)) return;
    c->writing = g_queue_pop_head(&c->outq);
    gsize len = 0;
    const void* data = g_bytes_get_data(c->writing, &len);
    g_output_stream_write_all_async(c->out, data, len, G_PRIORITY_DEFAULT, c->io, on_written, client_ref(c));
}

static void dispatch(SftpClient* c, const guint8* body, guint32 len) {
    SftpReader r;
    sftp_reader_init(&r, body, len);
    guint8 type = 0;
    sftp_get_u8(&r, &type);
    if (type == SFTP_VERSION) {
        guint32 version = 0;
        if (!sftp_get_u32(&r, &version) || version < 3) {
            lose(c, g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                                        _("the remote machine speaks an older SFTP version")));
            return;
        }
        while (r.ok && r.left > 0) {
            gchar* name = sftp_get_str(&r);
            gchar* value = sftp_get_str(&r);
            if (name != NULL) g_hash_table_replace(c->extensions, name, value != NULL ? value : g_strdup(""));
        }
        c->ready = TRUE;
        while (!g_queue_is_empty(&c->held)) g_queue_push_tail(&c->outq, g_queue_pop_head(&c->held));
        write_next(c);
        return;
    }
    guint32 id = 0;
    if (!sftp_get_u32(&r, &id)) {
        lose(c, bad_reply());
        return;
    }
    Pending* p = NULL;
    if (!g_hash_table_steal_extended(c->pending, GUINT_TO_POINTER(id), NULL, (gpointer*)&p)) return;
    p->func(c, type, &r, NULL, p->data);
    g_free(p);
}

static void read_head(SftpClient* c);

static void on_body(GObject* src, GAsyncResult* res, gpointer data) {
    SftpClient* c = data;
    GError* err = NULL;
    gsize n = 0;
    gboolean ok = g_input_stream_read_all_finish(G_INPUT_STREAM(src), res, &n, &err);
    if (!ok || n < c->body_len) {
        lose(c, closed_error(c, err));
    } else if (c->error == NULL) {
        dispatch(c, c->body, c->body_len);
        if (c->error == NULL) read_head(c);
    }
    g_clear_pointer(&c->body, g_free);
    client_unref(c);
}

static void on_head(GObject* src, GAsyncResult* res, gpointer data) {
    SftpClient* c = data;
    GError* err = NULL;
    gsize n = 0;
    gboolean ok = g_input_stream_read_all_finish(G_INPUT_STREAM(src), res, &n, &err);
    if (!ok || n < 4) {
        lose(c, closed_error(c, err));
    } else if (c->error == NULL) {
        guint32 len = ((guint32)c->head[0] << 24) | ((guint32)c->head[1] << 16) | ((guint32)c->head[2] << 8) |
                      c->head[3];
        if (len == 0 || len > MAX_PACKET) {
            lose(c, bad_reply());
        } else {
            c->body_len = len;
            c->body = g_malloc(len);
            g_input_stream_read_all_async(c->in, c->body, len, G_PRIORITY_DEFAULT, c->io, on_body, client_ref(c));
        }
    }
    client_unref(c);
}

static void read_head(SftpClient* c) {
    g_input_stream_read_all_async(c->in, c->head, 4, G_PRIORITY_DEFAULT, c->io, on_head, client_ref(c));
}

/* ssh's standard error, kept for the message when the connection drops */
static void on_err(GObject* src, GAsyncResult* res, gpointer data) {
    SftpClient* c = data;
    gssize n = g_input_stream_read_finish(G_INPUT_STREAM(src), res, NULL);
    if (n > 0) {
        g_string_append_len(c->err_tail, (const gchar*)c->err_buf, n);
        if (c->err_tail->len > ERR_TAIL) g_string_erase(c->err_tail, 0, (gssize)(c->err_tail->len - ERR_TAIL));
        g_input_stream_read_async(G_INPUT_STREAM(src), c->err_buf, sizeof c->err_buf, G_PRIORITY_DEFAULT, c->io,
                                  on_err, client_ref(c));
    }
    client_unref(c);
}

SftpClient* sftp_client_new(GInputStream* in, GOutputStream* out) {
    SftpClient* c = g_new0(SftpClient, 1);
    c->refs = 1;
    c->in = g_object_ref(in);
    c->out = g_object_ref(out);
    c->io = g_cancellable_new();
    c->context = g_main_context_ref_thread_default();
    c->extensions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    c->pending = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    c->next_id = 1;
    c->err_tail = g_string_new(NULL);
    GByteArray* b = sftp_packet_begin(SFTP_INIT);
    sftp_put_u32(b, 3);
    g_queue_push_tail(&c->outq, sftp_packet_finish(b));
    write_next(c);
    read_head(c);
    return c;
}

SftpClient* sftp_client_spawn(const SshTarget* target, GError** error) {
    gchar* sock = ssh_session_control_path(target);
    GPtrArray* argv = ssh_session_argv(target, 0); /* asks for the master when it is not open */
    gboolean no_master = sock != NULL && !g_file_test(sock, G_FILE_TEST_EXISTS);
    g_free(sock);
    if (no_master) {
        g_ptr_array_unref(argv);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED,
                            _("the SSH connection to the machine is not open"));
        return NULL;
    }
    g_ptr_array_insert(argv, (gint)argv->len - 1, g_strdup("-s")); /* before the destination */
    g_ptr_array_add(argv, g_strdup("sftp"));
    g_ptr_array_add(argv, NULL);
    GSubprocessLauncher* l = ssh_session_launcher(G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                                  G_SUBPROCESS_FLAGS_STDERR_PIPE);
    GSubprocess* p = g_subprocess_launcher_spawnv(l, (const gchar* const*)argv->pdata, error);
    g_object_unref(l);
    g_ptr_array_unref(argv);
    if (p == NULL) return NULL;
    SftpClient* c = sftp_client_new(g_subprocess_get_stdout_pipe(p), g_subprocess_get_stdin_pipe(p));
    c->proc = p;
    g_input_stream_read_async(g_subprocess_get_stderr_pipe(p), c->err_buf, sizeof c->err_buf, G_PRIORITY_DEFAULT,
                              c->io, on_err, client_ref(c));
    return c;
}

void sftp_client_free(SftpClient* c) {
    if (c == NULL) return;
    lose(c, g_error_new_literal(G_IO_ERROR, G_IO_ERROR_CLOSED, _("the connection to the remote machine was closed")));
    client_unref(c);
}

gboolean sftp_client_is_open(const SftpClient* c) {
    return c != NULL && c->error == NULL;
}

const GError* sftp_client_error(const SftpClient* c) {
    return c != NULL ? c->error : NULL;
}

gboolean sftp_client_has_extension(const SftpClient* c, const gchar* name) {
    return c != NULL && g_hash_table_contains(c->extensions, name);
}

/* ------------------------------------------------------------------------- */
/* Requests                                                                  */
/* ------------------------------------------------------------------------- */

typedef struct {
    SftpClient* c;
    ReplyFunc func;
    gpointer data;
} FailLater;

static gboolean on_fail_later(gpointer data) {
    FailLater* f = data;
    f->func(f->c, 0, NULL, f->c->error, f->data);
    client_unref(f->c);
    g_free(f);
    return G_SOURCE_REMOVE;
}

/* A request packet with its id: put the fields, then request_send() */
static GByteArray* begin(SftpClient* c, guint8 type, guint32* id) {
    *id = c->next_id++;
    if (c->next_id == 0) c->next_id = 1;
    GByteArray* b = sftp_packet_begin(type);
    sftp_put_u32(b, *id);
    return b;
}

/* Send; func gets the reply (or the error), never before request_send() returns */
static void request_send(SftpClient* c, GByteArray* b, guint32 id, ReplyFunc func, gpointer data) {
    if (c->error != NULL) {
        g_byte_array_unref(b);
        FailLater* f = g_new(FailLater, 1);
        f->c = client_ref(c);
        f->func = func;
        f->data = data;
        GSource* idle = g_idle_source_new();
        g_source_set_callback(idle, on_fail_later, f, NULL);
        g_source_attach(idle, c->context);
        g_source_unref(idle);
        return;
    }
    Pending* p = g_new(Pending, 1);
    p->func = func;
    p->data = data;
    g_hash_table_insert(c->pending, GUINT_TO_POINTER(id), p);
    g_queue_push_tail(c->ready ? &c->outq : &c->held, sftp_packet_finish(b));
    write_next(c);
}

/* A request on a path */
static void send_path(SftpClient* c, guint8 type, const gchar* path, ReplyFunc func, gpointer data) {
    guint32 id = 0;
    GByteArray* b = begin(c, type, &id);
    sftp_put_str(b, path);
    request_send(c, b, id, func, data);
}

/* A request on a handle */
static void send_handle(SftpClient* c, guint8 type, GBytes* handle, ReplyFunc func, gpointer data) {
    guint32 id = 0;
    GByteArray* b = begin(c, type, &id);
    gsize n = 0;
    const void* h = g_bytes_get_data(handle, &n);
    sftp_put_data(b, h, n);
    request_send(c, b, id, func, data);
}

/* NULL for a SFTP_OK status; else the error of the reply (*code gets the
 * status code, or SFTP_FAILURE) */
static GError* status_error(guint8 type, SftpReader* r, const GError* error, guint32* code) {
    if (code != NULL) *code = SFTP_FAILURE;
    if (error != NULL) return g_error_copy(error);
    if (type != SFTP_STATUS) return bad_reply();
    guint32 st = 0;
    if (!sftp_get_u32(r, &st)) return bad_reply();
    if (code != NULL) *code = st;
    if (st == SFTP_OK) return NULL;
    gchar* msg = sftp_get_str(r);
    GError* e = sftp_status_error(st, msg);
    g_free(msg);
    return e;
}

/* The handle of a HANDLE reply, else NULL and *err */
static GBytes* handle_reply(guint8 type, SftpReader* r, const GError* error, guint32* code, GError** err) {
    if (error == NULL && type == SFTP_HANDLE) {
        const guint8* h = NULL;
        guint32 n = 0;
        if (sftp_get_data(r, &h, &n)) return g_bytes_new(h, n);
        *err = bad_reply();
        return NULL;
    }
    *err = status_error(type, r, error, code);
    if (*err == NULL) *err = bad_reply(); /* "OK" where a handle was due */
    return NULL;
}

static gboolean cancelled(GTask* task, GError** err) {
    return g_cancellable_set_error_if_cancelled(g_task_get_cancellable(task), err);
}

static void ignore_reply(SftpClient* c, guint8 type, SftpReader* r, const GError* error, gpointer data) {
    (void)c, (void)type, (void)r, (void)error, (void)data;
}

/* ------------------------------------------------------------------------- */
/* Single requests                                                           */
/* ------------------------------------------------------------------------- */

static GTask* new_task(GCancellable* cancel, GAsyncReadyCallback done, gpointer data, gpointer tag) {
    GTask* task = g_task_new(NULL, cancel, done, data);
    g_task_set_source_tag(task, tag);
    return task;
}

static void on_status_task(SftpClient* c, guint8 type, SftpReader* r, const GError* error, gpointer data) {
    (void)c;
    GTask* task = data;
    GError* e = status_error(type, r, error, NULL);
    if (e != NULL) {
        g_task_return_error(task, e);
    } else {
        g_task_return_boolean(task, TRUE);
    }
    g_object_unref(task);
}

gboolean sftp_client_finish(GAsyncResult* result, GError** error) {
    return g_task_propagate_boolean(G_TASK(result), error);
}

void sftp_client_mkdir_async(SftpClient* c, const gchar* path, guint32 mode, GCancellable* cancel,
                             GAsyncReadyCallback done, gpointer data) {
    GTask* task = new_task(cancel, done, data, sftp_client_mkdir_async);
    guint32 id = 0;
    GByteArray* b = begin(c, SFTP_MKDIR, &id);
    sftp_put_str(b, path);
    SftpAttrs a = { .flags = SFTP_ATTR_PERMISSIONS, .permissions = mode & 07777 };
    sftp_put_attrs(b, &a);
    request_send(c, b, id, on_status_task, task);
}

void sftp_client_remove_async(SftpClient* c, const gchar* path, GCancellable* cancel, GAsyncReadyCallback done,
                              gpointer data) {
    send_path(c, SFTP_REMOVE, path, on_status_task, new_task(cancel, done, data, sftp_client_remove_async));
}

void sftp_client_rmdir_async(SftpClient* c, const gchar* path, GCancellable* cancel, GAsyncReadyCallback done,
                             gpointer data) {
    send_path(c, SFTP_RMDIR, path, on_status_task, new_task(cancel, done, data, sftp_client_rmdir_async));
}

static void on_realpath(SftpClient* c, guint8 type, SftpReader* r, const GError* error, gpointer data) {
    (void)c;
    GTask* task = data;
    guint32 count = 0;
    gchar* name = NULL;
    if (error == NULL && type == SFTP_NAME && sftp_get_u32(r, &count) && count >= 1) name = sftp_get_str(r);
    if (name != NULL) {
        g_task_return_pointer(task, name, g_free);
    } else {
        GError* e = status_error(type, r, error, NULL);
        g_task_return_error(task, e != NULL ? e : bad_reply());
    }
    g_object_unref(task);
}

void sftp_client_realpath_async(SftpClient* c, const gchar* path, GCancellable* cancel, GAsyncReadyCallback done,
                                gpointer data) {
    send_path(c, SFTP_REALPATH, path, on_realpath, new_task(cancel, done, data, sftp_client_realpath_async));
}

gchar* sftp_client_realpath_finish(GAsyncResult* result, GError** error) {
    return g_task_propagate_pointer(G_TASK(result), error);
}

static void on_stat(SftpClient* c, guint8 type, SftpReader* r, const GError* error, gpointer data) {
    (void)c;
    GTask* task = data;
    SftpAttrs a;
    if (error == NULL && type == SFTP_ATTRS && sftp_get_attrs(r, &a)) {
        g_task_return_pointer(task, g_memdup2(&a, sizeof a), g_free);
    } else {
        GError* e = status_error(type, r, error, NULL);
        g_task_return_error(task, e != NULL ? e : bad_reply());
    }
    g_object_unref(task);
}

void sftp_client_stat_async(SftpClient* c, const gchar* path, gboolean follow, GCancellable* cancel,
                            GAsyncReadyCallback done, gpointer data) {
    send_path(c, follow ? SFTP_STAT : SFTP_LSTAT, path, on_stat,
              new_task(cancel, done, data, sftp_client_stat_async));
}

gboolean sftp_client_stat_finish(GAsyncResult* result, SftpAttrs* attrs, GError** error) {
    SftpAttrs* a = g_task_propagate_pointer(G_TASK(result), error);
    if (a == NULL) return FALSE;
    if (attrs != NULL) *attrs = *a;
    g_free(a);
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Rename                                                                    */
/* ------------------------------------------------------------------------- */

typedef struct {
    GTask* task;
    gchar* from;
    gchar* to;
} RenameRun;

static void rename_run_free(RenameRun* run) {
    g_free(run->from);
    g_free(run->to);
    g_free(run);
}

static void send_rename(SftpClient* c, RenameRun* run) {
    guint32 id = 0;
    GByteArray* b = begin(c, SFTP_RENAME, &id);
    sftp_put_str(b, run->from);
    sftp_put_str(b, run->to);
    request_send(c, b, id, on_status_task, run->task);
    rename_run_free(run);
}

/* The target removed (or not there): now the rename itself */
static void on_rename_cleared(SftpClient* c, guint8 type, SftpReader* r, const GError* error, gpointer data) {
    RenameRun* run = data;
    guint32 code = 0;
    GError* e = status_error(type, r, error, &code);
    if (e != NULL && (error != NULL || code != SFTP_NO_SUCH_FILE)) {
        g_task_return_error(run->task, e);
        g_object_unref(run->task);
        rename_run_free(run);
        return;
    }
    g_clear_error(&e);
    send_rename(c, run);
}

void sftp_client_rename_async(SftpClient* c, const gchar* from, const gchar* to, gboolean replace,
                              GCancellable* cancel, GAsyncReadyCallback done, gpointer data) {
    RenameRun* run = g_new0(RenameRun, 1);
    run->task = new_task(cancel, done, data, sftp_client_rename_async);
    run->from = g_strdup(from);
    run->to = g_strdup(to);
    if (replace && sftp_client_has_extension(c, POSIX_RENAME)) {
        guint32 id = 0;
        GByteArray* b = begin(c, SFTP_EXTENDED, &id);
        sftp_put_str(b, POSIX_RENAME);
        sftp_put_str(b, from);
        sftp_put_str(b, to);
        request_send(c, b, id, on_status_task, run->task);
        rename_run_free(run);
    } else if (replace) {
        send_path(c, SFTP_REMOVE, to, on_rename_cleared, run); /* SFTP v3 renames never replace */
    } else {
        send_rename(c, run);
    }
}

/* ------------------------------------------------------------------------- */
/* List a folder                                                             */
/* ------------------------------------------------------------------------- */

typedef struct {
    SftpClient* c;
    GTask* task;
    GBytes* handle;
    GPtrArray* entries;
    GError* err;
} ListRun;

static void list_run_free(ListRun* run) {
    if (run->handle != NULL) g_bytes_unref(run->handle);
    g_ptr_array_unref(run->entries);
    g_clear_error(&run->err);
    client_unref(run->c);
    g_free(run);
}

static void list_return(ListRun* run) {
    if (run->err != NULL) {
        g_task_return_error(run->task, g_steal_pointer(&run->err));
    } else {
        g_task_return_pointer(run->task, g_ptr_array_ref(run->entries), (GDestroyNotify)g_ptr_array_unref);
    }
    g_object_unref(run->task);
    list_run_free(run);
}

static void on_list_closed(SftpClient* c, guint8 type, SftpReader* r, const GError* error, gpointer data) {
    (void)c, (void)type, (void)r, (void)error;
    list_return(data);
}

static void on_readdir(SftpClient* c, guint8 type, SftpReader* r, const GError* error, gpointer data) {
    ListRun* run = data;
    if (error == NULL && type == SFTP_NAME) {
        guint32 count = 0;
        sftp_get_u32(r, &count);
        for (guint32 i = 0; i < count && r->ok; i++) {
            gchar* name = sftp_get_str(r);
            gchar* longname = sftp_get_str(r);
            SftpAttrs a;
            gboolean ok = sftp_get_attrs(r, &a);
            g_free(longname);
            if (ok && name != NULL && strcmp(name, ".") != 0 && strcmp(name, "..") != 0 && name[0] != '\0' &&
                strchr(name, '/') == NULL) {
                SftpEntry* e = g_new(SftpEntry, 1);
                e->name = g_steal_pointer(&name);
                e->attrs = a;
                g_ptr_array_add(run->entries, e);
            }
            g_free(name);
        }
        if (!r->ok) {
            run->err = bad_reply();
        } else if (!cancelled(run->task, &run->err)) {
            send_handle(c, SFTP_READDIR, run->handle, on_readdir, run);
            return;
        }
    } else {
        guint32 code = 0;
        run->err = status_error(type, r, error, &code);
        if (run->err == NULL) run->err = bad_reply();
        if (error == NULL && code == SFTP_EOF) g_clear_error(&run->err); /* the end of the folder */
    }
    send_handle(c, SFTP_CLOSE, run->handle, on_list_closed, run);
}

static void on_opendir(SftpClient* c, guint8 type, SftpReader* r, const GError* error, gpointer data) {
    ListRun* run = data;
    run->handle = handle_reply(type, r, error, NULL, &run->err);
    if (run->handle == NULL) {
        list_return(run);
        return;
    }
    send_handle(c, SFTP_READDIR, run->handle, on_readdir, run);
}

void sftp_client_list_async(SftpClient* c, const gchar* path, GCancellable* cancel, GAsyncReadyCallback done,
                            gpointer data) {
    ListRun* run = g_new0(ListRun, 1);
    run->c = client_ref(c);
    run->task = new_task(cancel, done, data, sftp_client_list_async);
    run->entries = g_ptr_array_new_with_free_func(sftp_entry_free);
    send_path(c, SFTP_OPENDIR, path, on_opendir, run);
}

GPtrArray* sftp_client_list_finish(GAsyncResult* result, GError** error) {
    return g_task_propagate_pointer(G_TASK(result), error);
}

/* ------------------------------------------------------------------------- */
/* Read a file                                                               */
/* ------------------------------------------------------------------------- */

typedef struct {
    SftpClient* c;
    GTask* task;
    GBytes* handle;
    guint64 max;
    gboolean size_known;
    guint64 size;
    guint64 start;  /* where the reading starts */
    guint64 target; /* where it ends: min(size, start + max) */
    guint64 eof_at; /* where the server said end of file (G_MAXUINT64: not yet) */
    guint64 next;   /* offset of the next READ */
    guint inflight;
    GByteArray* buf;
    GError* err;
} ReadRun;

typedef struct {
    ReadRun* run;
    guint64 off;
    guint32 len;
} ReadChunk;

static void read_run_free(ReadRun* run) {
    if (run->handle != NULL) g_bytes_unref(run->handle);
    g_byte_array_unref(run->buf);
    g_clear_error(&run->err);
    client_unref(run->c);
    g_free(run);
}

static void read_return(ReadRun* run) {
    if (run->err != NULL) {
        g_task_return_error(run->task, g_steal_pointer(&run->err));
    } else {
        guint64 stop = MIN(run->target, run->eof_at);
        guint64 end = MIN(stop > run->start ? stop - run->start : 0, (guint64)run->buf->len);
        g_byte_array_set_size(run->buf, (guint)end);
        guint64* size = g_new(guint64, 1);
        *size = run->size_known ? run->size : run->start + end;
        g_task_set_task_data(run->task, size, g_free);
        g_task_return_pointer(run->task, g_bytes_new(run->buf->data, run->buf->len), (GDestroyNotify)g_bytes_unref);
    }
    g_object_unref(run->task);
    read_run_free(run);
}

static void on_read_closed(SftpClient* c, guint8 type, SftpReader* r, const GError* error, gpointer data) {
    (void)c, (void)type, (void)r, (void)error;
    read_return(data);
}

static void on_read_data(SftpClient* c, guint8 type, SftpReader* r, const GError* error, gpointer data);

static void read_chunk(ReadRun* run, guint64 off, guint32 len) {
    ReadChunk* ch = g_new(ReadChunk, 1);
    ch->run = run;
    ch->off = off;
    ch->len = len;
    run->inflight++;
    guint32 id = 0;
    GByteArray* b = begin(run->c, SFTP_READ, &id);
    gsize n = 0;
    const void* h = g_bytes_get_data(run->handle, &n);
    sftp_put_data(b, h, n);
    sftp_put_u64(b, off);
    sftp_put_u32(b, len);
    request_send(run->c, b, id, on_read_data, ch);
}

/* Keep WINDOW reads in flight; close the file once all came back */
static void read_pump(ReadRun* run) {
    if (run->err == NULL) cancelled(run->task, &run->err);
    while (run->err == NULL && run->inflight < WINDOW && run->next < run->target && run->next < run->eof_at) {
        guint32 len = (guint32)MIN((guint64)CHUNK, run->target - run->next);
        read_chunk(run, run->next, len);
        run->next += len;
    }
    if (run->inflight == 0) send_handle(run->c, SFTP_CLOSE, run->handle, on_read_closed, run);
}

static void on_read_data(SftpClient* c, guint8 type, SftpReader* r, const GError* error, gpointer data) {
    (void)c;
    ReadChunk* ch = data;
    ReadRun* run = ch->run;
    run->inflight--;
    const guint8* bytes = NULL;
    guint32 n = 0;
    if (error == NULL && type == SFTP_DATA) {
        if (!sftp_get_data(r, &bytes, &n) || n > ch->len) {
            if (run->err == NULL) run->err = bad_reply();
        } else if (run->err == NULL) {
            guint64 at = ch->off - run->start; /* in the buffer */
            if (run->buf->len < at + n) g_byte_array_set_size(run->buf, (guint)(at + n));
            memcpy(run->buf->data + at, bytes, n);
            /* A short read before the end: ask for the rest */
            if (n < ch->len && ch->off + n < run->eof_at) read_chunk(run, ch->off + n, ch->len - n);
        }
    } else {
        guint32 code = 0;
        GError* e = status_error(type, r, error, &code);
        if (error == NULL && code == SFTP_EOF) {
            g_clear_error(&e);
            run->eof_at = MIN(run->eof_at, ch->off);
        } else if (run->err == NULL) {
            run->err = e != NULL ? e : bad_reply();
        } else {
            g_clear_error(&e);
        }
    }
    g_free(ch);
    read_pump(run);
}

static void on_read_fstat(SftpClient* c, guint8 type, SftpReader* r, const GError* error, gpointer data) {
    (void)c;
    ReadRun* run = data;
    SftpAttrs a;
    if (error == NULL && type == SFTP_ATTRS && sftp_get_attrs(r, &a) && (a.flags & SFTP_ATTR_SIZE)) {
        run->size_known = TRUE;
        run->size = a.size;
        run->target = MAX(run->start, MIN(a.size, run->target));
        if (run->target - run->start <= G_MAXUINT / 2) { /* the room at once, filled as the data comes */
            g_byte_array_unref(run->buf);
            run->buf = g_byte_array_sized_new((guint)(run->target - run->start));
        }
    } else if (error != NULL) {
        run->err = g_error_copy(error);
    }
    if (run->target - run->start > G_MAXUINT / 2) { /* GByteArray: guint lengths */
        if (!run->size_known) {
            run->target = run->start + G_MAXUINT / 2;
        } else if (run->err == NULL) {
            run->err = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NO_SPACE, _("the file is too large to read"));
        }
    }
    read_pump(run);
}

static void on_read_open(SftpClient* c, guint8 type, SftpReader* r, const GError* error, gpointer data) {
    ReadRun* run = data;
    run->handle = handle_reply(type, r, error, NULL, &run->err);
    if (run->handle == NULL) {
        read_return(run);
        return;
    }
    send_handle(c, SFTP_FSTAT, run->handle, on_read_fstat, run);
}

void sftp_client_read_async(SftpClient* c, const gchar* path, guint64 max_bytes, GCancellable* cancel,
                            GAsyncReadyCallback done, gpointer data) {
    sftp_client_read_range_async(c, path, 0, max_bytes, cancel, done, data);
}

void sftp_client_read_range_async(SftpClient* c, const gchar* path, guint64 offset, guint64 max_bytes,
                                  GCancellable* cancel, GAsyncReadyCallback done, gpointer data) {
    ReadRun* run = g_new0(ReadRun, 1);
    run->c = client_ref(c);
    run->task = new_task(cancel, done, data, sftp_client_read_async);
    run->max = max_bytes;
    run->start = offset;
    run->next = offset;
    run->target = max_bytes > G_MAXUINT64 - offset ? G_MAXUINT64 : offset + max_bytes;
    run->eof_at = G_MAXUINT64;
    run->buf = g_byte_array_new();
    guint32 id = 0;
    GByteArray* b = begin(c, SFTP_OPEN, &id);
    sftp_put_str(b, path);
    sftp_put_u32(b, SFTP_OPEN_READ);
    sftp_put_attrs(b, NULL);
    request_send(c, b, id, on_read_open, run);
}

GBytes* sftp_client_read_finish(GAsyncResult* result, guint64* size, GError** error) {
    GBytes* bytes = g_task_propagate_pointer(G_TASK(result), error);
    const guint64* s = g_task_get_task_data(G_TASK(result));
    if (size != NULL) *size = bytes != NULL && s != NULL ? *s : 0;
    return bytes;
}

/* ------------------------------------------------------------------------- */
/* Write a file                                                              */
/* ------------------------------------------------------------------------- */

typedef struct {
    SftpClient* c;
    GTask* task;
    gchar* path;
    gchar* tmp; /* NULL: in place */
    GBytes* contents;
    guint32 mode;
    gboolean existed;
    gboolean keep_mode; /* mode is the replaced file's */
    GBytes* handle;
    gsize next;
    guint inflight;
    GError* err;
} WriteRun;

static void write_run_free(WriteRun* run) {
    g_free(run->path);
    g_free(run->tmp);
    g_bytes_unref(run->contents);
    if (run->handle != NULL) g_bytes_unref(run->handle);
    g_clear_error(&run->err);
    client_unref(run->c);
    g_free(run);
}

static void write_return(WriteRun* run) {
    if (run->err != NULL) {
        if (run->tmp != NULL) send_path(run->c, SFTP_REMOVE, run->tmp, ignore_reply, NULL);
        g_task_return_error(run->task, g_steal_pointer(&run->err));
    } else {
        g_task_return_boolean(run->task, TRUE);
    }
    g_object_unref(run->task);
    write_run_free(run);
}

static void on_write_renamed(GObject* src, GAsyncResult* res, gpointer data) {
    (void)src;
    WriteRun* run = data;
    sftp_client_finish(res, &run->err);
    write_return(run);
}

static void on_write_closed(SftpClient* c, guint8 type, SftpReader* r, const GError* error, gpointer data) {
    WriteRun* run = data;
    GError* e = status_error(type, r, error, NULL); /* a full disk may only show here */
    if (run->err == NULL) {
        run->err = e;
    } else {
        g_clear_error(&e);
    }
    if (run->err == NULL && run->tmp != NULL) {
        sftp_client_rename_async(c, run->tmp, run->path, TRUE, NULL, on_write_renamed, run);
        return;
    }
    write_return(run);
}

static void write_pump(WriteRun* run);

static void on_write_done(SftpClient* c, guint8 type, SftpReader* r, const GError* error, gpointer data) {
    (void)c;
    WriteRun* run = data;
    run->inflight--;
    GError* e = status_error(type, r, error, NULL);
    if (run->err == NULL) {
        run->err = e;
    } else {
        g_clear_error(&e);
    }
    write_pump(run);
}

static void write_pump(WriteRun* run) {
    gsize total = 0;
    const guint8* data = g_bytes_get_data(run->contents, &total);
    if (run->err == NULL) cancelled(run->task, &run->err);
    while (run->err == NULL && run->inflight < WINDOW && run->next < total) {
        gsize len = MIN((gsize)CHUNK, total - run->next);
        guint32 id = 0;
        GByteArray* b = begin(run->c, SFTP_WRITE, &id);
        gsize hn = 0;
        const void* h = g_bytes_get_data(run->handle, &hn);
        sftp_put_data(b, h, hn);
        sftp_put_u64(b, run->next);
        sftp_put_data(b, data + run->next, len);
        run->inflight++;
        run->next += len;
        request_send(run->c, b, id, on_write_done, run);
    }
    if (run->inflight == 0) send_handle(run->c, SFTP_CLOSE, run->handle, on_write_closed, run);
}

static void write_open(WriteRun* run);

static void on_write_open(SftpClient* c, guint8 type, SftpReader* r, const GError* error, gpointer data) {
    WriteRun* run = data;
    guint32 code = 0;
    GError* e = NULL;
    run->handle = handle_reply(type, r, error, &code, &e);
    if (run->handle == NULL) {
        if (error == NULL && run->tmp != NULL && run->existed && code == SFTP_PERMISSION_DENIED) {
            g_error_free(e); /* a folder that takes no new file: write the file itself */
            g_clear_pointer(&run->tmp, g_free);
            write_open(run);
            return;
        }
        run->err = e;
        g_clear_pointer(&run->tmp, g_free); /* not created */
        write_return(run);
        return;
    }
    if (run->tmp != NULL && run->keep_mode) { /* exactly the old mode, whatever the umask */
        guint32 id = 0;
        GByteArray* b = begin(c, SFTP_FSETSTAT, &id);
        gsize hn = 0;
        const void* h = g_bytes_get_data(run->handle, &hn);
        sftp_put_data(b, h, hn);
        SftpAttrs a = { .flags = SFTP_ATTR_PERMISSIONS, .permissions = run->mode };
        sftp_put_attrs(b, &a);
        request_send(c, b, id, ignore_reply, NULL);
    }
    write_pump(run);
}

static void write_open(WriteRun* run) {
    guint32 id = 0;
    GByteArray* b = begin(run->c, SFTP_OPEN, &id);
    if (run->tmp != NULL) {
        sftp_put_str(b, run->tmp);
        sftp_put_u32(b, SFTP_OPEN_WRITE | SFTP_OPEN_CREAT | SFTP_OPEN_EXCL);
        SftpAttrs a = { .flags = SFTP_ATTR_PERMISSIONS, .permissions = run->mode };
        sftp_put_attrs(b, &a);
    } else {
        sftp_put_str(b, run->path);
        sftp_put_u32(b, SFTP_OPEN_WRITE | SFTP_OPEN_CREAT | SFTP_OPEN_TRUNC);
        SftpAttrs a = { .flags = SFTP_ATTR_PERMISSIONS, .permissions = run->mode };
        sftp_put_attrs(b, run->existed ? NULL : &a);
    }
    request_send(run->c, b, id, on_write_open, run);
}

/* "<folder>/.<name>.lookin-<random>" next to path */
static gchar* temp_name(const gchar* path) {
    const gchar* slash = strrchr(path, '/');
    gchar* dir = slash != NULL ? g_strndup(path, (gsize)(slash - path + 1)) : g_strdup("");
    const gchar* base = slash != NULL ? slash + 1 : path;
    gchar* tmp = g_strdup_printf("%s.%s.lookin-%08x", dir, base, g_random_int());
    g_free(dir);
    return tmp;
}

static void on_write_lstat(GObject* src, GAsyncResult* res, gpointer data) {
    (void)src;
    WriteRun* run = data;
    SftpAttrs a;
    GError* e = NULL;
    if (sftp_client_stat_finish(res, &a, &e)) {
        run->existed = TRUE;
        if (a.flags & SFTP_ATTR_PERMISSIONS) {
            run->mode = a.permissions & 07777;
            run->keep_mode = TRUE;
        }
        if (sftp_attrs_is_link(&a)) g_clear_pointer(&run->tmp, g_free); /* keep the link */
    } else if (g_error_matches(e, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
        g_clear_error(&e);
    } else {
        run->err = e;
        g_clear_pointer(&run->tmp, g_free);
        write_return(run);
        return;
    }
    if (cancelled(run->task, &run->err)) {
        g_clear_pointer(&run->tmp, g_free);
        write_return(run);
        return;
    }
    write_open(run);
}

void sftp_client_write_async(SftpClient* c, const gchar* path, GBytes* contents, guint32 mode,
                             GCancellable* cancel, GAsyncReadyCallback done, gpointer data) {
    WriteRun* run = g_new0(WriteRun, 1);
    run->c = client_ref(c);
    run->task = new_task(cancel, done, data, sftp_client_write_async);
    run->path = g_strdup(path);
    run->tmp = temp_name(path);
    run->contents = contents != NULL ? g_bytes_ref(contents) : g_bytes_new(NULL, 0);
    run->mode = mode & 07777;
    sftp_client_stat_async(c, path, FALSE, NULL, on_write_lstat, run);
}
