/* A libssh channel as a GIOStream (see ssh_channel.h). */
#include "core/ssh_channel.h"
#include "core/i18n.h"

#include <libssh/callbacks.h>
#include <string.h>

#define WRITE_CHUNK_MAX 32768u /* max bytes per ssh_channel_write() call */

typedef enum {
    CH_WAIT_ATTACH,
    CH_OPENING,
    CH_PTY_REQ,
    CH_REQUESTING,
    CH_OPEN,
    CH_CLOSED,
} ChannelState;

/* Worker-thread synchronous waiter */
typedef struct {
    GMutex mu;
    GCond cond;
    gboolean done;
    gssize res;
    GError* err;
} SyncWaiter;

/* Pending async (GTask) or sync (SyncWaiter) operation on the link thread */
typedef struct {
    SshChannelCore* core;
    GTask* task;
    SyncWaiter* waiter;
    GCancellable* cancel;
    gulong cancel_id;
    guint8* rbuf;
    const guint8* wbuf;
    gsize count;
    gboolean is_stderr;
    gboolean handled;
} IoReq;

struct SshChannelCore {
    gint refs;       /* memory lifetime (atomic) */
    gint owner_refs; /* live GIO wrappers: SshChannel + in + out + err (atomic) */

    SshChannelDriver driver;
    SshChannelKind kind;
    gchar* arg;
    gint cols;
    gint rows;

    GMutex mu;
    gboolean stopped;
    gboolean has_exit;
    gint exit_status;
    GError* final_error;

    /* Link-thread-only state */
    ChannelState state;
    ssh_channel raw;
    struct ssh_channel_callbacks_struct cb;
    gboolean cb_installed;
    SshChannel* open_channel;
    GTask* open_task;
    gulong open_cancel_id;

    gboolean remote_eof;
    gboolean remote_closed;
    gboolean stdin_closed;
    gboolean want_send_eof;
    gboolean eof_sent;
    gboolean local_closed;
    gboolean dead;

    gboolean resize_pending;
    gint resize_cols;
    gint resize_rows;

    IoReq* pending_read[2]; /* 0 = stdout, 1 = stderr */
    IoReq* pending_write;
    IoReq* pending_close_out;
    GPtrArray* exit_waiters; /* IoReq* */
};

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static gboolean is_main_thread(void) {
    return g_main_context_is_owner(g_main_context_default());
}

static GError* make_closed_error(const SshChannelCore* core) {
    if (core->final_error != NULL) return g_error_copy(core->final_error);
    return g_error_new_literal(G_IO_ERROR, G_IO_ERROR_CLOSED, _("the SSH channel is closed"));
}

static GError* make_ssh_error(ssh_channel raw, const gchar* fallback) {
    const char* msg = NULL;
    GIOErrorEnum code = G_IO_ERROR_FAILED;
    if (raw != NULL) {
        ssh_session s = ssh_channel_get_session(raw);
        if (s != NULL) {
            msg = ssh_get_error(s);
            if (!ssh_is_connected(s) || (ssh_get_status(s) & (SSH_CLOSED | SSH_CLOSED_ERROR)) != 0) {
                code = G_IO_ERROR_CONNECTION_CLOSED;
            }
        }
    }
    if (msg != NULL && msg[0] != '\0') {
        gchar* valid = g_utf8_make_valid(msg, -1);
        GError* err = g_error_new_literal(G_IO_ERROR, code, valid);
        g_free(valid);
        return err;
    }
    return g_error_new_literal(G_IO_ERROR, code, fallback);
}

static void sync_waiter_init(SyncWaiter* w) {
    memset(w, 0, sizeof *w);
    g_mutex_init(&w->mu);
    g_cond_init(&w->cond);
}

static gssize sync_waiter_wait_and_clear(SyncWaiter* w, GError** error) {
    g_mutex_lock(&w->mu);
    while (!w->done) g_cond_wait(&w->cond, &w->mu);
    gssize res = w->res;
    GError* err = g_steal_pointer(&w->err);
    g_mutex_unlock(&w->mu);
    g_mutex_clear(&w->mu);
    g_cond_clear(&w->cond);
    if (err != NULL) g_propagate_error(error, err);
    return res;
}

static void req_disconnect_cancel(IoReq* r) {
    if (r->cancel != NULL && r->cancel_id != 0) {
        g_cancellable_disconnect(r->cancel, r->cancel_id);
        r->cancel_id = 0;
    }
}

static void req_finish(IoReq* r, gssize res, GError* err) {
    r->handled = TRUE;
    req_disconnect_cancel(r);
    if (r->task != NULL) {
        if (err != NULL) g_task_return_error(r->task, err);
        else g_task_return_int(r->task, res);
        g_clear_object(&r->task);
    } else if (r->waiter != NULL) {
        SyncWaiter* w = g_steal_pointer(&r->waiter);
        g_mutex_lock(&w->mu);
        w->res = res;
        w->err = err;
        w->done = TRUE;
        g_cond_signal(&w->cond);
        g_mutex_unlock(&w->mu);
    } else if (err != NULL) {
        g_error_free(err);
    }
}

static void req_free(gpointer data) {
    IoReq* r = data;
    if (r == NULL) return;
    if (!r->handled) {
        req_finish(r, -1, g_error_new_literal(G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED,
                                              _("the SSH connection was closed")));
    }
    req_disconnect_cancel(r);
    g_clear_object(&r->cancel);
    g_clear_object(&r->task);
    if (r->core != NULL) ssh_channel_core_unref(r->core);
    g_free(r);
}

static void on_cancellable_fired(GCancellable* cancel, gpointer data);

/* Move an IoReq out of the GSource invoke destroy-notify into channel ownership */
static IoReq* req_take(IoReq* r, gboolean watch_cancel) {
    IoReq* owned = g_memdup2(r, sizeof *r);
    r->handled = TRUE;
    r->task = NULL;
    r->waiter = NULL;
    r->cancel = NULL;
    r->core = NULL;
    if (watch_cancel && owned->cancel != NULL) {
        owned->cancel_id = g_cancellable_connect(owned->cancel, G_CALLBACK(on_cancellable_fired),
                                                 ssh_channel_core_ref(owned->core),
                                                 (GDestroyNotify)ssh_channel_core_unref);
    }
    return owned;
}

SshChannelCore* ssh_channel_core_ref(SshChannelCore* core) {
    g_atomic_int_inc(&core->refs);
    return core;
}

static void detach_open_cancel(SshChannelCore* core) {
    if (core->open_task != NULL && core->open_cancel_id != 0) {
        GCancellable* c = g_task_get_cancellable(core->open_task);
        if (c != NULL) g_cancellable_disconnect(c, core->open_cancel_id);
        core->open_cancel_id = 0;
    }
}

void ssh_channel_core_unref(SshChannelCore* core) {
    if (core == NULL || !g_atomic_int_dec_and_test(&core->refs)) return;
    detach_open_cancel(core);
    g_clear_object(&core->open_channel);
    g_clear_object(&core->open_task);
    if (core->exit_waiters != NULL) g_ptr_array_unref(core->exit_waiters);
    g_clear_error(&core->final_error);
    g_free(core->arg);
    g_mutex_clear(&core->mu);
    if (core->driver.context != NULL) g_main_context_unref(core->driver.context);
    if (core->driver.link_unref != NULL && core->driver.link != NULL) core->driver.link_unref(core->driver.link);
    g_free(core);
}

static void core_poke(SshChannelCore* core) {
    if (core->driver.poke != NULL && core->driver.link != NULL) core->driver.poke(core->driver.link);
}

/* ------------------------------------------------------------------------- */
/* Link-thread channel state machine                                         */
/* ------------------------------------------------------------------------- */

static void record_exit_status(SshChannelCore* core, gint status) {
    g_mutex_lock(&core->mu);
    if (!core->has_exit) {
        core->has_exit = TRUE;
        core->exit_status = status;
    }
    g_mutex_unlock(&core->mu);
}

static void flush_exit_waiters(SshChannelCore* core, const GError* err) {
    if (core->exit_waiters == NULL || core->exit_waiters->len == 0) return;
    g_mutex_lock(&core->mu);
    gboolean has = core->has_exit;
    gint status = core->exit_status;
    g_mutex_unlock(&core->mu);
    if (!has && err == NULL && !core->remote_closed && !core->dead) return;
    GPtrArray* list = core->exit_waiters;
    core->exit_waiters = g_ptr_array_new_with_free_func(req_free);
    for (guint i = 0; i < list->len; i++) {
        IoReq* ew = list->pdata[i];
        if (has) req_finish(ew, status, NULL);
        else if (err != NULL) req_finish(ew, -1, g_error_copy(err));
        else req_finish(ew, -1, NULL);
    }
    g_ptr_array_unref(list);
}

static void poll_exit_state(SshChannelCore* core) {
    if (core->raw == NULL) return;
    g_mutex_lock(&core->mu);
    gboolean already = core->has_exit;
    g_mutex_unlock(&core->mu);
    if (already) return;
    uint32_t code = 0;
    char* sig = NULL;
    int core_dumped = 0;
    if (ssh_channel_get_exit_state(core->raw, &code, &sig, &core_dumped) == SSH_OK) {
        if (sig != NULL) {
            ssh_string_free_char(sig);
            record_exit_status(core, 128);
        } else if (code != (uint32_t)-1) {
            record_exit_status(core, (gint)code);
        }
    }
}

static void free_raw_channel(SshChannelCore* core, gboolean send_close) {
    if (core->raw == NULL) return;
    poll_exit_state(core);
    if (core->cb_installed) {
        ssh_remove_channel_callbacks(core->raw, &core->cb);
        core->cb_installed = FALSE;
    }
    if (send_close && ssh_channel_is_open(core->raw) && !ssh_channel_is_closed(core->raw)) {
        ssh_channel_close(core->raw);
    }
    ssh_channel_free(core->raw);
    core->raw = NULL;
}

static void fail_open_task(SshChannelCore* core, const GError* err) {
    if (core->open_task == NULL) return;
    detach_open_cancel(core);
    GTask* t = g_steal_pointer(&core->open_task);
    g_clear_object(&core->open_channel);
    g_task_return_error(t, g_error_copy(err));
    g_object_unref(t);
}

static void succeed_open_task(SshChannelCore* core) {
    if (core->open_task == NULL) return;
    detach_open_cancel(core);
    GTask* t = g_steal_pointer(&core->open_task);
    SshChannel* ch = g_steal_pointer(&core->open_channel);
    g_task_return_pointer(t, ch, g_object_unref);
    g_object_unref(t);
}

static void terminate_channel(SshChannelCore* core, const GError* err, gboolean send_close) {
    g_mutex_lock(&core->mu);
    core->stopped = TRUE;
    if (core->final_error == NULL && err != NULL) core->final_error = g_error_copy(err);
    g_mutex_unlock(&core->mu);

    core->state = CH_CLOSED;
    core->dead = TRUE;
    free_raw_channel(core, send_close);

    for (int i = 0; i < 2; i++) {
        if (core->pending_read[i] == NULL) continue;
        IoReq* r = g_steal_pointer(&core->pending_read[i]);
        req_finish(r, err != NULL ? -1 : 0, err != NULL ? g_error_copy(err) : NULL);
        req_free(r);
    }
    if (core->pending_write != NULL) {
        IoReq* r = g_steal_pointer(&core->pending_write);
        req_finish(r, -1, err != NULL ? g_error_copy(err) : make_closed_error(core));
        req_free(r);
    }
    if (core->pending_close_out != NULL) {
        IoReq* r = g_steal_pointer(&core->pending_close_out);
        req_finish(r, 1, NULL);
        req_free(r);
    }
    if (err != NULL) fail_open_task(core, err);
    flush_exit_waiters(core, err);
}

static void on_ch_eof(ssh_session s, ssh_channel ch, void* ud) {
    (void)s, (void)ch;
    ((SshChannelCore*)ud)->remote_eof = TRUE;
}
static void on_ch_close(ssh_session s, ssh_channel ch, void* ud) {
    (void)s, (void)ch;
    ((SshChannelCore*)ud)->remote_eof = ((SshChannelCore*)ud)->remote_closed = TRUE;
}
static void on_ch_exit_status(ssh_session s, ssh_channel ch, int st, void* ud) {
    (void)s, (void)ch;
    record_exit_status(ud, st);
}
static void on_ch_exit_signal(ssh_session s, ssh_channel ch, const char* sig, int cd,
                              const char* msg, const char* lang, void* ud) {
    (void)s, (void)ch, (void)sig, (void)cd, (void)msg, (void)lang;
    record_exit_status(ud, 128);
}
static int on_ch_write_wontblock(ssh_session s, ssh_channel ch, uint32_t b, void* ud) {
    (void)s, (void)ch, (void)b, (void)ud;
    return 0;
}

void ssh_channel_attach(SshChannelCore* core, ssh_channel raw) {
    if (core->dead || core->local_closed) {
        if (raw != NULL) ssh_channel_free(raw);
        return;
    }
    core->raw = raw;
    memset(&core->cb, 0, sizeof core->cb);
    core->cb.userdata = core;
    /* Keep channel_data_function NULL so libssh buffers per-channel and
     * withholds WINDOW_ADJUST when no reader is draining that channel. */
    core->cb.channel_eof_function = on_ch_eof;
    core->cb.channel_close_function = on_ch_close;
    core->cb.channel_exit_status_function = on_ch_exit_status;
    core->cb.channel_exit_signal_function = on_ch_exit_signal;
    core->cb.channel_write_wontblock_function = on_ch_write_wontblock;
    ssh_callbacks_init(&core->cb);
    if (ssh_set_channel_callbacks(raw, &core->cb) == SSH_OK) core->cb_installed = TRUE;
    if (core->open_task != NULL) {
        GCancellable* c = g_task_get_cancellable(core->open_task);
        if (c != NULL) {
            core->open_cancel_id = g_cancellable_connect(c, G_CALLBACK(on_cancellable_fired),
                                                         ssh_channel_core_ref(core),
                                                         (GDestroyNotify)ssh_channel_core_unref);
        }
    }
    core->state = CH_OPENING;
    ssh_channel_pump(core);
}

static void pump_read(SshChannelCore* core, int idx) {
    IoReq* r = core->pending_read[idx];
    if (r == NULL) return;
    GError* cerr = NULL;
    if (g_cancellable_set_error_if_cancelled(r->cancel, &cerr)) {
        core->pending_read[idx] = NULL;
        req_finish(r, -1, cerr);
        req_free(r);
        return;
    }
    if (r->count == 0 || core->raw == NULL) {
        core->pending_read[idx] = NULL;
        GError* err = (r->count > 0 && core->final_error != NULL) ? g_error_copy(core->final_error) : NULL;
        req_finish(r, err != NULL ? -1 : 0, err);
        req_free(r);
        return;
    }
    int n = ssh_channel_read_nonblocking(core->raw, r->rbuf, (uint32_t)MIN(r->count, (gsize)G_MAXINT32), idx);
    if (n > 0) {
        core->pending_read[idx] = NULL;
        req_finish(r, (gssize)n, NULL);
        req_free(r);
    } else if (n == SSH_EOF ||
               (n == 0 && (core->remote_eof || core->remote_closed || ssh_channel_is_eof(core->raw) ||
                           ssh_channel_is_closed(core->raw)))) {
        core->remote_eof = TRUE;
        core->pending_read[idx] = NULL;
        req_finish(r, 0, NULL);
        req_free(r);
    } else if (n == SSH_ERROR) {
        GError* err = make_ssh_error(core->raw, _("failed to read from the SSH channel"));
        terminate_channel(core, err, FALSE);
        g_error_free(err);
    }
}

static void pump_write(SshChannelCore* core) {
    IoReq* r = core->pending_write;
    if (r == NULL) return;
    GError* cerr = NULL;
    if (g_cancellable_set_error_if_cancelled(r->cancel, &cerr)) {
        core->pending_write = NULL;
        req_finish(r, -1, cerr);
        req_free(r);
        return;
    }
    if (r->count == 0) {
        core->pending_write = NULL;
        req_finish(r, 0, NULL);
        req_free(r);
        return;
    }
    if (core->raw == NULL || core->remote_closed || ssh_channel_is_closed(core->raw)) {
        core->pending_write = NULL;
        req_finish(r, -1, g_error_new_literal(G_IO_ERROR, G_IO_ERROR_BROKEN_PIPE,
                                              _("the SSH channel was closed by the remote machine")));
        req_free(r);
        return;
    }
    uint32_t win = ssh_channel_window_size(core->raw);
    if (win == 0) return; /* wait for WINDOW_ADJUST, never block */
    int wr = ssh_channel_write(core->raw, r->wbuf, (uint32_t)MIN(r->count, (gsize)MIN(win, WRITE_CHUNK_MAX)));
    if (wr > 0) {
        core->pending_write = NULL;
        req_finish(r, (gssize)wr, NULL);
        req_free(r);
    } else if (wr == SSH_ERROR) {
        GError* err = make_ssh_error(core->raw, _("failed to write to the SSH channel"));
        terminate_channel(core, err, FALSE);
        g_error_free(err);
    }
}

static void pump_control(SshChannelCore* core) {
    if (core->raw == NULL) return;
    if (core->resize_pending && core->cols > 0 && core->rows > 0) {
        if (ssh_channel_change_pty_size(core->raw, core->resize_cols, core->resize_rows) != SSH_AGAIN) {
            core->resize_pending = FALSE;
        }
    }
    if (core->want_send_eof && !core->eof_sent && core->pending_write == NULL) {
        if (core->remote_closed || ssh_channel_is_closed(core->raw) ||
            ssh_channel_send_eof(core->raw) != SSH_AGAIN) {
            core->eof_sent = TRUE;
            core->want_send_eof = FALSE;
        }
    }
    if (core->pending_close_out != NULL && (!core->want_send_eof || core->eof_sent)) {
        IoReq* r = g_steal_pointer(&core->pending_close_out);
        req_finish(r, 1, NULL);
        req_free(r);
    }
}

void ssh_channel_pump(SshChannelCore* core) {
    if (core->dead) return;
    if (core->local_closed) {
        terminate_channel(core, NULL, TRUE);
        return;
    }
    if (core->state == CH_WAIT_ATTACH || core->raw == NULL) return;

    if (core->state == CH_OPENING) {
        int rc = ssh_channel_open_session(core->raw);
        if (rc == SSH_AGAIN) return;
        if (rc != SSH_OK) {
            GError* err = make_ssh_error(core->raw, _("could not open an SSH channel"));
            terminate_channel(core, err, FALSE);
            g_error_free(err);
            return;
        }
        core->state = (core->cols > 0 && core->rows > 0) ? CH_PTY_REQ : CH_REQUESTING;
    }

    if (core->state == CH_PTY_REQ) {
        const char* term = (core->kind == SSH_CHANNEL_KIND_SHELL && core->arg != NULL && core->arg[0] != '\0')
                               ? core->arg
                               : "xterm-256color";
        int rc = ssh_channel_request_pty_size(core->raw, term, core->cols, core->rows);
        if (rc == SSH_AGAIN) return;
        if (rc != SSH_OK) {
            GError* err = make_ssh_error(core->raw, _("could not request a remote terminal"));
            terminate_channel(core, err, TRUE);
            g_error_free(err);
            return;
        }
        core->state = CH_REQUESTING;
    }

    if (core->state == CH_REQUESTING) {
        int rc = core->kind == SSH_CHANNEL_KIND_EXEC
                     ? ssh_channel_request_exec(core->raw, core->arg != NULL ? core->arg : "true")
                 : core->kind == SSH_CHANNEL_KIND_SUBSYSTEM
                     ? ssh_channel_request_subsystem(core->raw, core->arg != NULL ? core->arg : "sftp")
                     : ssh_channel_request_shell(core->raw);
        if (rc == SSH_AGAIN) return;
        if (rc != SSH_OK) {
            GError* err = make_ssh_error(core->raw, _("the remote machine refused the channel request"));
            terminate_channel(core, err, TRUE);
            g_error_free(err);
            return;
        }
        core->state = CH_OPEN;
        succeed_open_task(core);
    }

    if (core->state == CH_OPEN) {
        if (ssh_channel_is_eof(core->raw)) core->remote_eof = TRUE;
        if (ssh_channel_is_closed(core->raw)) {
            core->remote_eof = TRUE;
            core->remote_closed = TRUE;
        }
        if (core->remote_eof || core->remote_closed) poll_exit_state(core);
        pump_read(core, 0);
        if (!core->dead) pump_read(core, 1);
        if (!core->dead) pump_write(core);
        if (!core->dead) pump_control(core);
        if (!core->dead) flush_exit_waiters(core, NULL);
    }
}

gboolean ssh_channel_wants_write(const SshChannelCore* core) {
    if (core->dead || core->raw == NULL) return FALSE;
    if (core->state == CH_OPENING || core->state == CH_PTY_REQ || core->state == CH_REQUESTING) return TRUE;
    if (core->state != CH_OPEN) return FALSE;
    return (core->pending_write != NULL && ssh_channel_window_size(core->raw) > 0) || core->resize_pending ||
           (core->want_send_eof && !core->eof_sent && core->pending_write == NULL);
}

void ssh_channel_fail(SshChannelCore* core, const GError* error) {
    if (core->dead) return;
    GError* err = error != NULL
                      ? g_error_copy(error)
                      : g_error_new_literal(G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED,
                                            _("the SSH connection was closed"));
    terminate_channel(core, err, FALSE);
    g_error_free(err);
}

gboolean ssh_channel_is_dead(const SshChannelCore* core) {
    return core->dead;
}

/* ------------------------------------------------------------------------- */
/* Posting requests from caller threads to the link thread                   */
/* ------------------------------------------------------------------------- */

static void cancel_slot(IoReq** slot) {
    IoReq* r = *slot;
    GError* err = NULL;
    if (r != NULL && g_cancellable_set_error_if_cancelled(r->cancel, &err)) {
        *slot = NULL;
        req_finish(r, -1, err);
        req_free(r);
    }
}

static gboolean on_cancel_on_link(gpointer data) {
    SshChannelCore* core = data;
    if (!core->dead) {
        if (core->open_task != NULL) {
            GError* err = NULL;
            if (g_cancellable_set_error_if_cancelled(g_task_get_cancellable(core->open_task), &err)) {
                terminate_channel(core, err, TRUE);
                g_error_free(err);
                core_poke(core);
                ssh_channel_core_unref(core);
                return G_SOURCE_REMOVE;
            }
        }
        cancel_slot(&core->pending_read[0]);
        cancel_slot(&core->pending_read[1]);
        cancel_slot(&core->pending_write);
        if (core->exit_waiters != NULL) {
            for (guint i = core->exit_waiters->len; i > 0; i--) {
                IoReq* ew = core->exit_waiters->pdata[i - 1];
                GError* err = NULL;
                if (g_cancellable_set_error_if_cancelled(ew->cancel, &err)) {
                    req_finish(ew, -1, err);
                    g_ptr_array_remove_index(core->exit_waiters, i - 1);
                }
            }
        }
    }
    ssh_channel_core_unref(core);
    return G_SOURCE_REMOVE;
}

static void on_cancellable_fired(GCancellable* cancel, gpointer data) {
    (void)cancel;
    SshChannelCore* core = data;
    g_main_context_invoke(core->driver.context, on_cancel_on_link, ssh_channel_core_ref(core));
}

static gboolean invoke_read(gpointer data) {
    IoReq* r = data;
    SshChannelCore* core = r->core;
    int idx = r->is_stderr ? 1 : 0;
    GError* err = NULL;
    if (g_cancellable_set_error_if_cancelled(r->cancel, &err)) {
        req_finish(r, -1, err);
        return G_SOURCE_REMOVE;
    }
    if (core->dead) {
        req_finish(r, core->final_error != NULL ? -1 : 0,
                   core->final_error != NULL ? g_error_copy(core->final_error) : NULL);
        return G_SOURCE_REMOVE;
    }
    if (core->pending_read[idx] != NULL) {
        req_finish(r, -1, g_error_new_literal(G_IO_ERROR, G_IO_ERROR_PENDING, _("read already in progress")));
        return G_SOURCE_REMOVE;
    }
    core->pending_read[idx] = req_take(r, TRUE);
    ssh_channel_pump(core);
    core_poke(core);
    return G_SOURCE_REMOVE;
}

static gboolean invoke_write(gpointer data) {
    IoReq* r = data;
    SshChannelCore* core = r->core;
    GError* err = NULL;
    if (g_cancellable_set_error_if_cancelled(r->cancel, &err)) {
        req_finish(r, -1, err);
        return G_SOURCE_REMOVE;
    }
    if (core->dead || core->stdin_closed) {
        req_finish(r, -1, make_closed_error(core));
        return G_SOURCE_REMOVE;
    }
    if (core->pending_write != NULL) {
        req_finish(r, -1, g_error_new_literal(G_IO_ERROR, G_IO_ERROR_PENDING, _("write already in progress")));
        return G_SOURCE_REMOVE;
    }
    core->pending_write = req_take(r, TRUE);
    ssh_channel_pump(core);
    core_poke(core);
    return G_SOURCE_REMOVE;
}

static gboolean invoke_close_out(gpointer data) {
    IoReq* r = data;
    SshChannelCore* core = r->core;
    core->stdin_closed = TRUE;
    core->want_send_eof = TRUE;
    if (core->dead || core->eof_sent || core->pending_close_out != NULL) {
        req_finish(r, 1, NULL);
        return G_SOURCE_REMOVE;
    }
    core->pending_close_out = req_take(r, FALSE);
    ssh_channel_pump(core);
    core_poke(core);
    return G_SOURCE_REMOVE;
}

static gboolean invoke_force_close(gpointer data) {
    SshChannelCore* core = data;
    core->local_closed = TRUE;
    if (!core->dead) {
        terminate_channel(core, NULL, TRUE);
        core_poke(core);
    }
    ssh_channel_core_unref(core);
    return G_SOURCE_REMOVE;
}

typedef struct {
    SshChannelCore* core;
    gint cols;
    gint rows;
} ResizeCall;

static gboolean invoke_resize(gpointer data) {
    ResizeCall* rc = data;
    if (!rc->core->dead) {
        rc->core->resize_cols = rc->cols;
        rc->core->resize_rows = rc->rows;
        rc->core->resize_pending = TRUE;
        ssh_channel_pump(rc->core);
        core_poke(rc->core);
    }
    ssh_channel_core_unref(rc->core);
    g_free(rc);
    return G_SOURCE_REMOVE;
}

static gboolean invoke_wait_exit(gpointer data) {
    IoReq* ew = data;
    SshChannelCore* core = ew->core;
    GError* err = NULL;
    if (g_cancellable_set_error_if_cancelled(ew->cancel, &err)) {
        req_finish(ew, -1, err);
        return G_SOURCE_REMOVE;
    }
    g_mutex_lock(&core->mu);
    gboolean has = core->has_exit;
    gint status = core->exit_status;
    g_mutex_unlock(&core->mu);
    if (has) {
        req_finish(ew, status, NULL);
        return G_SOURCE_REMOVE;
    }
    if (core->dead || core->remote_closed) {
        req_finish(ew, -1, core->final_error != NULL ? g_error_copy(core->final_error) : NULL);
        return G_SOURCE_REMOVE;
    }
    if (core->exit_waiters == NULL) core->exit_waiters = g_ptr_array_new_with_free_func(req_free);
    g_ptr_array_add(core->exit_waiters, req_take(ew, TRUE));
    return G_SOURCE_REMOVE;
}

static gboolean core_is_stopped(SshChannelCore* core, GError** out_err) {
    g_mutex_lock(&core->mu);
    gboolean stopped = core->stopped;
    if (stopped && out_err != NULL && core->final_error != NULL) *out_err = g_error_copy(core->final_error);
    g_mutex_unlock(&core->mu);
    return stopped;
}

static void core_owner_release(SshChannelCore* core) {
    if (core == NULL) return;
    if (g_atomic_int_dec_and_test(&core->owner_refs) && !core_is_stopped(core, NULL)) {
        g_main_context_invoke(core->driver.context, invoke_force_close, ssh_channel_core_ref(core));
    }
    ssh_channel_core_unref(core);
}

static IoReq* req_new(SshChannelCore* core, GTask* task, SyncWaiter* waiter, GCancellable* cancel) {
    IoReq* r = g_new0(IoReq, 1);
    r->core = ssh_channel_core_ref(core);
    r->task = task;
    r->waiter = waiter;
    r->cancel = cancel != NULL ? g_object_ref(cancel) : NULL;
    return r;
}

static void req_post(SshChannelCore* core, GSourceFunc fn, IoReq* r) {
    g_main_context_invoke_full(core->driver.context, G_PRIORITY_DEFAULT, fn, r, req_free);
}

/* ------------------------------------------------------------------------- */
/* SshChannelInputStream (stdout and stderr)                                 */
/* ------------------------------------------------------------------------- */

#define SSH_TYPE_CHANNEL_INPUT_STREAM (ssh_channel_input_stream_get_type())
G_DECLARE_FINAL_TYPE(SshChannelInputStream, ssh_channel_input_stream, SSH, CHANNEL_INPUT_STREAM, GInputStream)

struct _SshChannelInputStream {
    GInputStream parent_instance;
    SshChannelCore* core;
    gboolean is_stderr;
};

G_DEFINE_TYPE(SshChannelInputStream, ssh_channel_input_stream, G_TYPE_INPUT_STREAM)

static void ssh_channel_input_stream_finalize(GObject* obj) {
    core_owner_release(g_steal_pointer(&SSH_CHANNEL_INPUT_STREAM(obj)->core));
    G_OBJECT_CLASS(ssh_channel_input_stream_parent_class)->finalize(obj);
}

static gssize input_read_sync(GInputStream* stream, void* buffer, gsize count, GCancellable* cancel, GError** error) {
    SshChannelInputStream* self = SSH_CHANNEL_INPUT_STREAM(stream);
    g_return_val_if_fail(!is_main_thread(), -1);
    g_return_val_if_fail(!g_main_context_is_owner(self->core->driver.context), -1);
    if (g_cancellable_set_error_if_cancelled(cancel, error)) return -1;
    GError* serr = NULL;
    if (core_is_stopped(self->core, &serr)) {
        if (serr != NULL) {
            g_propagate_error(error, serr);
            return -1;
        }
        return 0;
    }
    SyncWaiter w;
    sync_waiter_init(&w);
    IoReq* r = req_new(self->core, NULL, &w, cancel);
    r->rbuf = buffer;
    r->count = count;
    r->is_stderr = self->is_stderr;
    req_post(self->core, invoke_read, r);
    return sync_waiter_wait_and_clear(&w, error);
}

static void input_read_async(GInputStream* stream, void* buffer, gsize count, int io_priority,
                             GCancellable* cancel, GAsyncReadyCallback cb, gpointer ud) {
    (void)io_priority;
    SshChannelInputStream* self = SSH_CHANNEL_INPUT_STREAM(stream);
    GTask* task = g_task_new(stream, cancel, cb, ud);
    g_task_set_source_tag(task, input_read_async);
    g_task_set_check_cancellable(task, FALSE);
    GError* err = NULL;
    if (g_cancellable_set_error_if_cancelled(cancel, &err)) {
        g_task_return_error(task, err);
        g_object_unref(task);
        return;
    }
    if (core_is_stopped(self->core, &err)) {
        if (err != NULL) g_task_return_error(task, err);
        else g_task_return_int(task, 0);
        g_object_unref(task);
        return;
    }
    IoReq* r = req_new(self->core, task, NULL, cancel);
    r->rbuf = buffer;
    r->count = count;
    r->is_stderr = self->is_stderr;
    req_post(self->core, invoke_read, r);
}

static gboolean input_close_sync(GInputStream* s, GCancellable* c, GError** e) { (void)s, (void)c, (void)e; return TRUE; }
static gboolean input_close_finish(GInputStream* s, GAsyncResult* res, GError** e) { (void)s; return g_task_propagate_boolean(G_TASK(res), e); }
static gssize input_read_finish(GInputStream* s, GAsyncResult* res, GError** e) { (void)s; return g_task_propagate_int(G_TASK(res), e); }

static void input_close_async(GInputStream* s, int prio, GCancellable* c, GAsyncReadyCallback cb, gpointer ud) {
    (void)prio;
    GTask* t = g_task_new(s, c, cb, ud);
    g_task_return_boolean(t, TRUE);
    g_object_unref(t);
}

static void ssh_channel_input_stream_class_init(SshChannelInputStreamClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = ssh_channel_input_stream_finalize;
    GInputStreamClass* ic = G_INPUT_STREAM_CLASS(klass);
    ic->read_fn = input_read_sync;
    ic->read_async = input_read_async;
    ic->read_finish = input_read_finish;
    ic->close_fn = input_close_sync;
    ic->close_async = input_close_async;
    ic->close_finish = input_close_finish;
}

static void ssh_channel_input_stream_init(SshChannelInputStream* self) { (void)self; }

/* ------------------------------------------------------------------------- */
/* SshChannelOutputStream (stdin)                                            */
/* ------------------------------------------------------------------------- */

#define SSH_TYPE_CHANNEL_OUTPUT_STREAM (ssh_channel_output_stream_get_type())
G_DECLARE_FINAL_TYPE(SshChannelOutputStream, ssh_channel_output_stream, SSH, CHANNEL_OUTPUT_STREAM, GOutputStream)

struct _SshChannelOutputStream {
    GOutputStream parent_instance;
    SshChannelCore* core;
};

G_DEFINE_TYPE(SshChannelOutputStream, ssh_channel_output_stream, G_TYPE_OUTPUT_STREAM)

static void ssh_channel_output_stream_finalize(GObject* obj) {
    core_owner_release(g_steal_pointer(&SSH_CHANNEL_OUTPUT_STREAM(obj)->core));
    G_OBJECT_CLASS(ssh_channel_output_stream_parent_class)->finalize(obj);
}

static gssize output_write_sync(GOutputStream* stream, const void* buffer, gsize count, GCancellable* cancel,
                                GError** error) {
    SshChannelOutputStream* self = SSH_CHANNEL_OUTPUT_STREAM(stream);
    g_return_val_if_fail(!is_main_thread(), -1);
    g_return_val_if_fail(!g_main_context_is_owner(self->core->driver.context), -1);
    if (g_cancellable_set_error_if_cancelled(cancel, error)) return -1;
    GError* serr = NULL;
    if (core_is_stopped(self->core, &serr)) {
        g_propagate_error(error, serr != NULL ? serr : make_closed_error(self->core));
        return -1;
    }
    SyncWaiter w;
    sync_waiter_init(&w);
    IoReq* r = req_new(self->core, NULL, &w, cancel);
    r->wbuf = buffer;
    r->count = count;
    req_post(self->core, invoke_write, r);
    return sync_waiter_wait_and_clear(&w, error);
}

static void output_write_async(GOutputStream* stream, const void* buffer, gsize count, int io_priority,
                               GCancellable* cancel, GAsyncReadyCallback cb, gpointer ud) {
    (void)io_priority;
    SshChannelOutputStream* self = SSH_CHANNEL_OUTPUT_STREAM(stream);
    GTask* task = g_task_new(stream, cancel, cb, ud);
    g_task_set_source_tag(task, output_write_async);
    g_task_set_check_cancellable(task, FALSE);
    GError* err = NULL;
    if (g_cancellable_set_error_if_cancelled(cancel, &err)) {
        g_task_return_error(task, err);
        g_object_unref(task);
        return;
    }
    if (core_is_stopped(self->core, &err)) {
        g_task_return_error(task, err != NULL ? err : make_closed_error(self->core));
        g_object_unref(task);
        return;
    }
    IoReq* r = req_new(self->core, task, NULL, cancel);
    r->wbuf = buffer;
    r->count = count;
    req_post(self->core, invoke_write, r);
}

static gssize output_write_finish(GOutputStream* s, GAsyncResult* res, GError** e) { (void)s; return g_task_propagate_int(G_TASK(res), e); }
static gboolean output_close_finish(GOutputStream* s, GAsyncResult* res, GError** e) { (void)s; return g_task_propagate_int(G_TASK(res), e) >= 0; }

static gboolean output_close_sync(GOutputStream* stream, GCancellable* cancel, GError** error) {
    SshChannelOutputStream* self = SSH_CHANNEL_OUTPUT_STREAM(stream);
    (void)cancel;
    if (core_is_stopped(self->core, NULL)) return TRUE;
    if (is_main_thread() || g_main_context_is_owner(self->core->driver.context)) {
        req_post(self->core, invoke_close_out, req_new(self->core, NULL, NULL, NULL));
        return TRUE;
    }
    SyncWaiter w;
    sync_waiter_init(&w);
    req_post(self->core, invoke_close_out, req_new(self->core, NULL, &w, NULL));
    sync_waiter_wait_and_clear(&w, error);
    return TRUE;
}

static void output_close_async(GOutputStream* stream, int io_priority, GCancellable* cancel,
                               GAsyncReadyCallback cb, gpointer ud) {
    (void)io_priority;
    SshChannelOutputStream* self = SSH_CHANNEL_OUTPUT_STREAM(stream);
    GTask* task = g_task_new(stream, cancel, cb, ud);
    g_task_set_source_tag(task, output_close_async);
    g_task_set_check_cancellable(task, FALSE);
    if (core_is_stopped(self->core, NULL)) {
        g_task_return_int(task, 1);
        g_object_unref(task);
        return;
    }
    req_post(self->core, invoke_close_out, req_new(self->core, task, NULL, NULL));
}

static void ssh_channel_output_stream_class_init(SshChannelOutputStreamClass* klass) {
    G_OBJECT_CLASS(klass)->finalize = ssh_channel_output_stream_finalize;
    GOutputStreamClass* oc = G_OUTPUT_STREAM_CLASS(klass);
    oc->write_fn = output_write_sync;
    oc->write_async = output_write_async;
    oc->write_finish = output_write_finish;
    oc->close_fn = output_close_sync;
    oc->close_async = output_close_async;
    oc->close_finish = output_close_finish;
}

static void ssh_channel_output_stream_init(SshChannelOutputStream* self) { (void)self; }

/* ------------------------------------------------------------------------- */
/* SshChannel (GIOStream)                                                    */
/* ------------------------------------------------------------------------- */

struct _SshChannel {
    GIOStream parent_instance;
    SshChannelCore* core;
    GInputStream* in;
    GOutputStream* out;
    GInputStream* err;
};

G_DEFINE_TYPE(SshChannel, ssh_channel, G_TYPE_IO_STREAM)

static GInputStream* channel_get_input_stream(GIOStream* s) { return SSH_CHANNEL(s)->in; }
static GOutputStream* channel_get_output_stream(GIOStream* s) { return SSH_CHANNEL(s)->out; }
static gboolean channel_close_sync(GIOStream* s, GCancellable* c, GError** e) { (void)c, (void)e; ssh_channel_force_close(SSH_CHANNEL(s)); return TRUE; }
static gboolean channel_close_finish(GIOStream* s, GAsyncResult* res, GError** e) { (void)s; return g_task_propagate_boolean(G_TASK(res), e); }
static void ssh_channel_init(SshChannel* self) { (void)self; }

static void channel_close_async(GIOStream* s, int prio, GCancellable* c, GAsyncReadyCallback cb, gpointer ud) {
    (void)prio;
    ssh_channel_force_close(SSH_CHANNEL(s));
    GTask* t = g_task_new(s, c, cb, ud);
    g_task_return_boolean(t, TRUE);
    g_object_unref(t);
}

static void ssh_channel_dispose(GObject* obj) {
    SshChannel* self = SSH_CHANNEL(obj);
    G_OBJECT_CLASS(ssh_channel_parent_class)->dispose(obj);
    g_clear_object(&self->in);
    g_clear_object(&self->out);
    g_clear_object(&self->err);
}

static void ssh_channel_finalize(GObject* obj) {
    core_owner_release(g_steal_pointer(&SSH_CHANNEL(obj)->core));
    G_OBJECT_CLASS(ssh_channel_parent_class)->finalize(obj);
}

static void ssh_channel_class_init(SshChannelClass* klass) {
    GObjectClass* oc = G_OBJECT_CLASS(klass);
    oc->dispose = ssh_channel_dispose;
    oc->finalize = ssh_channel_finalize;
    GIOStreamClass* sc = G_IO_STREAM_CLASS(klass);
    sc->get_input_stream = channel_get_input_stream;
    sc->get_output_stream = channel_get_output_stream;
    sc->close_fn = channel_close_sync;
    sc->close_async = channel_close_async;
    sc->close_finish = channel_close_finish;
}

/* ------------------------------------------------------------------------- */
/* Public & driver entry points                                              */
/* ------------------------------------------------------------------------- */

SshChannel* ssh_channel_create(const SshChannelDriver* driver, SshChannelKind kind, const gchar* arg,
                               gint cols, gint rows, GTask* open_task, SshChannelCore** out_core) {
    SshChannelCore* core = g_new0(SshChannelCore, 1);
    core->refs = 1;
    core->owner_refs = 4; /* SshChannel + in + out + err */
    core->driver = *driver;
    if (core->driver.context != NULL) g_main_context_ref(core->driver.context);
    if (core->driver.link_ref != NULL && core->driver.link != NULL) core->driver.link_ref(core->driver.link);
    core->kind = kind;
    core->arg = g_strdup(arg);
    core->cols = cols;
    core->rows = rows;
    core->state = CH_WAIT_ATTACH;
    core->exit_status = -1;
    g_mutex_init(&core->mu);

    SshChannel* ch = g_object_new(SSH_TYPE_CHANNEL, NULL);
    ch->core = ssh_channel_core_ref(core);

    SshChannelInputStream* in = g_object_new(SSH_TYPE_CHANNEL_INPUT_STREAM, NULL);
    in->core = ssh_channel_core_ref(core);
    in->is_stderr = FALSE;
    ch->in = G_INPUT_STREAM(in);

    SshChannelInputStream* err = g_object_new(SSH_TYPE_CHANNEL_INPUT_STREAM, NULL);
    err->core = ssh_channel_core_ref(core);
    err->is_stderr = TRUE;
    ch->err = G_INPUT_STREAM(err);

    SshChannelOutputStream* out = g_object_new(SSH_TYPE_CHANNEL_OUTPUT_STREAM, NULL);
    out->core = ssh_channel_core_ref(core);
    ch->out = G_OUTPUT_STREAM(out);

    if (open_task != NULL) {
        core->open_task = g_object_ref(open_task);
        core->open_channel = g_object_ref(ch);
    }
    if (out_core != NULL) *out_core = core;
    else ssh_channel_core_unref(core);
    return ch;
}

GInputStream* ssh_channel_get_stderr(SshChannel* ch) {
    g_return_val_if_fail(SSH_IS_CHANNEL(ch), NULL);
    return ch->err;
}

void ssh_channel_resize_pty(SshChannel* ch, gint cols, gint rows) {
    g_return_if_fail(SSH_IS_CHANNEL(ch));
    if (cols <= 0 || rows <= 0 || core_is_stopped(ch->core, NULL)) return;
    ResizeCall* rc = g_new(ResizeCall, 1);
    rc->core = ssh_channel_core_ref(ch->core);
    rc->cols = cols;
    rc->rows = rows;
    g_main_context_invoke(ch->core->driver.context, invoke_resize, rc);
}

void ssh_channel_force_close(SshChannel* ch) {
    g_return_if_fail(SSH_IS_CHANNEL(ch));
    if (core_is_stopped(ch->core, NULL)) return;
    g_main_context_invoke(ch->core->driver.context, invoke_force_close, ssh_channel_core_ref(ch->core));
}

gboolean ssh_channel_exit_status(SshChannel* ch, gint* status) {
    g_return_val_if_fail(SSH_IS_CHANNEL(ch), FALSE);
    g_mutex_lock(&ch->core->mu);
    gboolean has = ch->core->has_exit;
    gint code = ch->core->exit_status;
    g_mutex_unlock(&ch->core->mu);
    if (has && status != NULL) *status = code;
    return has;
}

void ssh_channel_wait_exit_async(SshChannel* ch, GCancellable* cancel, GAsyncReadyCallback done, gpointer data) {
    g_return_if_fail(SSH_IS_CHANNEL(ch));
    GTask* task = g_task_new(ch, cancel, done, data);
    g_task_set_source_tag(task, ssh_channel_wait_exit_async);
    g_task_set_check_cancellable(task, FALSE);
    GError* err = NULL;
    gint status = -1;
    if (g_cancellable_set_error_if_cancelled(cancel, &err)) {
        g_task_return_error(task, err);
        g_object_unref(task);
        return;
    }
    if (ssh_channel_exit_status(ch, &status)) {
        g_task_return_int(task, status);
        g_object_unref(task);
        return;
    }
    if (core_is_stopped(ch->core, &err)) {
        if (err != NULL) g_task_return_error(task, err);
        else g_task_return_int(task, -1);
        g_object_unref(task);
        return;
    }
    req_post(ch->core, invoke_wait_exit, req_new(ch->core, task, NULL, cancel));
}

gint ssh_channel_wait_exit_finish(SshChannel* ch, GAsyncResult* result, GError** error) {
    g_return_val_if_fail(SSH_IS_CHANNEL(ch), -1);
    return (gint)g_task_propagate_int(G_TASK(result), error);
}

gboolean ssh_channel_wait_exit_sync(SshChannel* ch, GCancellable* cancel, gint* status, GError** error) {
    g_return_val_if_fail(SSH_IS_CHANNEL(ch), FALSE);
    g_return_val_if_fail(!is_main_thread(), FALSE);
    g_return_val_if_fail(!g_main_context_is_owner(ch->core->driver.context), FALSE);
    if (g_cancellable_set_error_if_cancelled(cancel, error)) return FALSE;
    gint code = -1;
    if (ssh_channel_exit_status(ch, &code)) {
        if (status != NULL) *status = code;
        return TRUE;
    }
    GError* serr = NULL;
    if (core_is_stopped(ch->core, &serr)) {
        if (serr != NULL) {
            g_propagate_error(error, serr);
            return FALSE;
        }
        if (status != NULL) *status = -1;
        return TRUE;
    }
    SyncWaiter w;
    sync_waiter_init(&w);
    req_post(ch->core, invoke_wait_exit, req_new(ch->core, NULL, &w, cancel));
    GError* err = NULL;
    gssize res = sync_waiter_wait_and_clear(&w, &err);
    if (err != NULL) {
        g_propagate_error(error, err);
        return FALSE;
    }
    if (status != NULL) *status = (gint)res;
    return TRUE;
}
