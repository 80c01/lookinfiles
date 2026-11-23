/* Loopback TCP relay for SSH channels on an SshLink (see ssh_relay.h). */
#ifdef _WIN32
#include <winsock2.h> /* before any header that may pull windows.h */
#endif
#include "core/ssh_relay.h"

#ifdef HAVE_LIBSSH

#include "core/app_paths.h"
#include "core/ssh_channel.h"
#include "core/ssh_trace.h"

#include <glib/gstdio.h>
#include <libssh/libssh.h>
#include <stdio.h>
#include <string.h>

#ifdef G_OS_WIN32
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#endif

#define RELAY_PROTO_LINE        "LOOKIN-RELAY 1"
#define RELAY_MAX_FRAME         32768u
#define RELAY_OUTQ_HIGH         (256u * 1024u)
#define RELAY_MAX_HANDSHAKE_LEN (256u * 1024u)
#define RELAY_HANDSHAKE_SEC     10u
#define TICKET_DEFAULT_TTL_MS   60000u
#define TICKET_SWEEP_SEC        5u
#define STOP_SAFETY_MS          2000u

/* ------------------------------------------------------------------------- */
/* Shared helpers (framing, secrets, runtime directory)                      */
/* ------------------------------------------------------------------------- */

static void wipe_mem(void* p, gsize n) {
    if (p == NULL || n == 0) return;
    volatile guint8* b = p;
    while (n-- > 0) *b++ = 0;
}

static void wipe_string(gchar* s) {
    if (s == NULL) return;
    wipe_mem(s, strlen(s));
    g_free(s);
}

/* Constant-time string comparison (same pattern as ssh_askpass.c) */
static gboolean same_secret(const gchar* a, const gchar* b) {
    if (a == NULL || b == NULL) return FALSE;
    gsize la = strlen(a);
    gsize lb = strlen(b);
    guchar diff = (guchar)(la != lb);
    for (gsize i = 0; i < la && i < lb; i++) diff |= (guchar)(a[i] ^ b[i]);
    return diff == 0;
}

static guint16 get_be16(const guint8* src) {
    return (guint16)(((guint16)src[0] << 8) | (guint16)src[1]);
}

static void put_be32(guint8* dst, guint32 v) {
    dst[0] = (guint8)((v >> 24) & 0xffu);
    dst[1] = (guint8)((v >> 16) & 0xffu);
    dst[2] = (guint8)((v >> 8) & 0xffu);
    dst[3] = (guint8)(v & 0xffu);
}

static guint32 get_be32(const guint8* src) {
    return ((guint32)src[0] << 24) | ((guint32)src[1] << 16) | ((guint32)src[2] << 8) | (guint32)src[3];
}

static GBytes* build_frame(guint8 type, const void* payload, guint32 len) {
    guint8* buf = g_malloc(5u + len);
    buf[0] = type;
    put_be32(buf + 1, len);
    if (len > 0 && payload != NULL) memcpy(buf + 5, payload, len);
    return g_bytes_new_take(buf, 5u + len);
}

static void enable_nodelay(GSocketConnection* conn) {
    GSocket* sock = g_socket_connection_get_socket(conn);
    if (sock != NULL) g_socket_set_option(sock, IPPROTO_TCP, TCP_NODELAY, 1, NULL);
}

/* <runtime>/relay, created 0700, or NULL when no private runtime dir exists */
static gchar* relay_dir(void) {
    gchar* runtime = app_runtime_dir();
    if (runtime == NULL) return NULL;
    gchar* dir = g_build_filename(runtime, "relay", NULL);
    g_free(runtime);
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_free(dir);
        return NULL;
    }
    return dir;
}

/* 128 random bits as 32 lowercase hex digits */
static gchar* new_secret(void) {
    guint8 bytes[16];
    gboolean ok = ssh_get_random(bytes, (int)sizeof bytes, 1) == 1;
#ifdef G_OS_UNIX
    if (!ok) {
        FILE* f = fopen("/dev/urandom", "rb");
        if (f != NULL) {
            ok = fread(bytes, 1, sizeof bytes, f) == sizeof bytes;
            fclose(f);
        }
    }
#endif
    if (!ok) {
        for (gsize i = 0; i < sizeof bytes; i++) bytes[i] = (guint8)g_random_int_range(0, 256);
    }
    gchar* hex = g_malloc(33);
    static const char HEX[] = "0123456789abcdef";
    for (gsize i = 0; i < sizeof bytes; i++) {
        hex[i * 2] = HEX[bytes[i] >> 4];
        hex[i * 2 + 1] = HEX[bytes[i] & 0x0fu];
    }
    hex[32] = '\0';
    wipe_mem(bytes, sizeof bytes);
    return hex;
}

static gboolean write_fd_all(int fd, const void* buf, gsize len) {
    const guint8* p = buf;
    gsize rem = len;
    while (rem > 0) {
#ifdef G_OS_WIN32
        int w = _write(fd, p, (unsigned int)MIN(rem, (gsize)G_MAXINT));
        if (w <= 0) return FALSE;
#else
        ssize_t w = write(fd, p, rem);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return FALSE;
#endif
        p += (gsize)w;
        rem -= (gsize)w;
    }
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Ticket registry (shared between caller threads and the relay thread)      */
/* ------------------------------------------------------------------------- */

typedef struct {
    gchar* id;
    gchar* secret;
    gchar* path;
    SshLink* link;
    gboolean tty;
    gint64 expires_us;
} RelayTicket;

static GMutex relay_mu;
static gchar* relay_program;
static guint ticket_ttl_ms; /* 0 = TICKET_DEFAULT_TTL_MS */
static guint64 ticket_seq;
static GHashTable* tickets; /* id -> RelayTicket* */

static gboolean server_running;
static guint16 server_port;
static GThread* server_thread;
static GMainContext* server_ctx;
static GMainLoop* server_loop;

/* Relay-thread-only server state */
typedef struct {
    GSocketService* service;
    GHashTable* active_conns; /* RelayConn* -> RelayConn* (unowned keys) */
    guint live_conns;
    gboolean stopping;
    GSource* sweep_src;
    GSource* stop_timer;
} RelayServer;

static RelayServer* g_server;

static void ticket_free(gpointer data) {
    RelayTicket* t = data;
    if (t == NULL) return;
    if (t->path != NULL) g_remove(t->path);
    g_free(t->path);
    wipe_string(t->secret);
    g_free(t->id);
    if (t->link != NULL) ssh_link_unref(t->link);
    g_free(t);
}

static void sweep_expired_tickets_locked(void) {
    if (tickets == NULL) return;
    gint64 now = g_get_monotonic_time();
    GHashTableIter it;
    gpointer val;
    g_hash_table_iter_init(&it, tickets);
    while (g_hash_table_iter_next(&it, NULL, &val)) {
        RelayTicket* t = val;
        if (now >= t->expires_us) {
            ssh_trace("relay: ticket expired id=%s", t->id);
            g_hash_table_iter_remove(&it);
        }
    }
}

/* Constant-time ticket lookup; consumes the ticket and deletes its file */
static gboolean consume_ticket(const gchar* candidate, SshLink** out_link, gboolean* out_tty, gchar** out_id) {
    *out_link = NULL;
    *out_tty = FALSE;
    *out_id = NULL;
    if (candidate == NULL || candidate[0] == '\0') return FALSE;

    g_mutex_lock(&relay_mu);
    RelayTicket* matched = NULL;
    gint64 now = g_get_monotonic_time();
    if (tickets != NULL) {
        GHashTableIter it;
        gpointer val;
        g_hash_table_iter_init(&it, tickets);
        while (g_hash_table_iter_next(&it, NULL, &val)) {
            RelayTicket* t = val;
            /* Always compare every ticket so timing does not reveal matches */
            if (same_secret(candidate, t->secret)) matched = t;
        }
    }
    gboolean ok = FALSE;
    if (matched != NULL) {
        gboolean expired = now >= matched->expires_us;
        if (!expired && matched->link != NULL && ssh_link_is_up(matched->link)) {
            *out_link = ssh_link_ref(matched->link);
            *out_tty = matched->tty;
            *out_id = g_strdup(matched->id);
            ok = TRUE;
        } else {
            ssh_trace("relay: ticket rejected id=%s expired=%d", matched->id, expired ? 1 : 0);
        }
        g_hash_table_remove(tickets, matched->id); /* single-use; deletes file */
    }
    g_mutex_unlock(&relay_mu);
    return ok;
}

void ssh_relay_set_program(const gchar* program) {
    g_mutex_lock(&relay_mu);
    g_free(relay_program);
    relay_program = (program != NULL && program[0] != '\0') ? g_strdup(program) : NULL;
    g_mutex_unlock(&relay_mu);
}

void ssh_relay_set_ticket_ttl_ms(guint ttl_ms) {
    g_mutex_lock(&relay_mu);
    ticket_ttl_ms = ttl_ms;
    g_mutex_unlock(&relay_mu);
}

guint16 ssh_relay_port(void) {
    g_mutex_lock(&relay_mu);
    guint16 p = server_running ? server_port : 0;
    g_mutex_unlock(&relay_mu);
    return p;
}

/* Create ticket file 0600 with O_CREAT | O_EXCL, retrying id on collision */
static gboolean write_ticket_file(const gchar* dir, const gchar* secret, gchar** out_id, gchar** out_path) {
#ifdef G_OS_WIN32
    long pid = (long)_getpid();
#else
    long pid = (long)getpid();
#endif
    for (int tries = 0; tries < 8; tries++) {
        g_mutex_lock(&relay_mu);
        guint64 seq = ++ticket_seq;
        g_mutex_unlock(&relay_mu);

        gchar* id = g_strdup_printf("%ld-%" G_GUINT64_FORMAT, pid, seq);
        gchar* path = g_build_filename(dir, id, NULL);
#ifdef G_OS_WIN32
        int fd = g_open(path, O_WRONLY | O_CREAT | O_EXCL | _O_BINARY, 0600);
#else
        int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
#endif
        if (fd < 0) {
            int err = errno;
            g_free(id);
            g_free(path);
            if (err == EEXIST) continue;
            return FALSE;
        }
        gchar* content = g_strconcat(secret, "\n", NULL);
        gsize len = strlen(content);
        gboolean ok = write_fd_all(fd, content, len);
        wipe_string(content);
#ifdef G_OS_WIN32
        if (_close(fd) != 0) ok = FALSE;
#else
        if (close(fd) != 0) ok = FALSE;
#endif
        if (!ok) {
            g_remove(path);
            g_free(id);
            g_free(path);
            return FALSE;
        }
        *out_id = id;
        *out_path = path;
        return TRUE;
    }
    return FALSE;
}

GPtrArray* ssh_relay_argv(SshLink* link, gboolean tty) {
    if (link == NULL || !ssh_link_is_up(link)) return NULL;

    g_mutex_lock(&relay_mu);
    if (!server_running || server_port == 0 || relay_program == NULL) {
        g_mutex_unlock(&relay_mu);
        return NULL;
    }
    guint16 port = server_port;
    gchar* prog = g_strdup(relay_program);
    guint ttl_ms = ticket_ttl_ms > 0 ? ticket_ttl_ms : TICKET_DEFAULT_TTL_MS;
    g_mutex_unlock(&relay_mu);

    gchar* dir = relay_dir();
    if (dir == NULL) {
        g_free(prog);
        return NULL;
    }
    gchar* secret = new_secret();
    gchar* id = NULL;
    gchar* path = NULL;
    gboolean wrote = write_ticket_file(dir, secret, &id, &path);
    g_free(dir);
    if (!wrote) {
        wipe_string(secret);
        g_free(prog);
        return NULL;
    }

    RelayTicket* t = g_new0(RelayTicket, 1);
    t->id = id;
    t->secret = secret;
    t->path = path;
    t->link = ssh_link_ref(link);
    t->tty = tty;
    t->expires_us = g_get_monotonic_time() + (gint64)ttl_ms * 1000;

    g_mutex_lock(&relay_mu);
    if (!server_running) {
        g_mutex_unlock(&relay_mu);
        ticket_free(t);
        g_free(prog);
        return NULL;
    }
    if (tickets == NULL) tickets = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, ticket_free);
    g_hash_table_insert(tickets, t->id, t);
    g_mutex_unlock(&relay_mu);

    ssh_trace("relay: issued ticket id=%s port=%u tty=%d", t->id, (guint)port, tty ? 1 : 0);

    GPtrArray* argv = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(argv, prog);
    g_ptr_array_add(argv, g_strdup("--ssh-channel"));
    g_ptr_array_add(argv, g_strdup_printf("%u", (guint)port));
    g_ptr_array_add(argv, g_strdup(t->id));
    return argv;
}

/* ------------------------------------------------------------------------- */
/* Server side: per-connection async state machine (relay thread only)       */
/* ------------------------------------------------------------------------- */

typedef struct {
    gint refs;
    RelayServer* srv;
    GSocketConnection* sock;
    GDataInputStream* sock_in;
    GOutputStream* sock_out;
    GCancellable* cancel;
    GSource* hs_timeout;

    /* Handshake state */
    guint hs_line;
    const gchar* hs_err;
    SshLink* link;
    gboolean tty;
    gchar* ticket_id;
    gint rows;
    gint cols;

    /* Active channel */
    SshChannel* ch;
    GInputStream* ch_out;
    GInputStream* ch_err;
    GOutputStream* ch_in;

    /* Channel -> socket (O, R, X frames) + flow control */
    GQueue outq; /* GBytes* */
    gsize outq_bytes;
    GBytes* cur_write;
    gboolean sock_writing;
    gboolean reading_out;
    gboolean reading_err;
    gboolean out_eof;
    gboolean err_eof;
    gboolean exit_done;
    gint exit_status;
    gboolean channel_error;
    gboolean close_after_drain;
    gboolean draining_exit;
    gboolean fin_sent;
    gboolean peer_eof;
    gboolean closing;
    guint8 out_buf[RELAY_MAX_FRAME];
    guint8 err_buf[RELAY_MAX_FRAME];

    /* Socket -> channel (I, E, W frames) + flow control */
    guint8 frame_hdr[5];
    guint8 frame_buf[RELAY_MAX_FRAME];
    guint8 frame_type;
    guint32 frame_len;
    gsize ch_write_off;
    gboolean ch_in_closed;
} RelayConn;

static RelayConn* conn_ref(RelayConn* c) {
    c->refs++;
    return c;
}

static void conn_clear_hs_timeout(RelayConn* c) {
    if (c->hs_timeout != NULL) {
        g_source_destroy(c->hs_timeout);
        g_source_unref(c->hs_timeout);
        c->hs_timeout = NULL;
    }
}

static void conn_unref(RelayConn* c) {
    if (c == NULL || --c->refs > 0) return;
    conn_clear_hs_timeout(c);
    g_queue_clear_full(&c->outq, (GDestroyNotify)g_bytes_unref);
    g_clear_pointer(&c->cur_write, g_bytes_unref);
    g_clear_object(&c->ch);
    if (c->link != NULL) ssh_link_unref(c->link);
    g_free(c->ticket_id);
    g_clear_object(&c->sock_in);
    if (c->sock != NULL) {
        g_io_stream_close(G_IO_STREAM(c->sock), NULL, NULL);
        g_clear_object(&c->sock);
    }
    g_clear_object(&c->cancel);
    RelayServer* srv = c->srv;
    g_free(c);
    if (srv != NULL && srv->live_conns > 0) {
        srv->live_conns--;
        if (srv->stopping && srv->live_conns == 0 && server_loop != NULL) {
            g_main_loop_quit(server_loop);
        }
    }
}

static void conn_abort(RelayConn* c, gboolean graceful_write_fin) {
    if (c->closing) return;
    c->closing = TRUE;
    conn_clear_hs_timeout(c);
    if (c->srv != NULL && c->srv->active_conns != NULL) {
        g_hash_table_remove(c->srv->active_conns, c);
    }
    if (c->ch != NULL) ssh_channel_force_close(c->ch);
    if (c->sock != NULL) {
        GSocket* s = g_socket_connection_get_socket(c->sock);
        if (s != NULL) g_socket_shutdown(s, !graceful_write_fin, TRUE, NULL);
    }
    g_cancellable_cancel(c->cancel);
    conn_unref(c); /* drops the server's owning ref */
}

static void maybe_read_channel(RelayConn* c);
static void pump_sock_write(RelayConn* c);

static void on_sock_written(GObject* src, GAsyncResult* res, gpointer data) {
    RelayConn* c = data;
    GError* err = NULL;
    gboolean ok = g_output_stream_write_all_finish(G_OUTPUT_STREAM(src), res, NULL, &err);
    c->sock_writing = FALSE;
    if (c->cur_write != NULL) {
        gsize n = g_bytes_get_size(c->cur_write);
        c->outq_bytes = (c->outq_bytes >= n) ? (c->outq_bytes - n) : 0;
        g_clear_pointer(&c->cur_write, g_bytes_unref);
    }
    if (!ok || c->closing) {
        if (err != NULL && !g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            ssh_trace("relay: socket write failed id=%s: %s", c->ticket_id ? c->ticket_id : "-", err->message);
        }
        g_clear_error(&err);
        conn_abort(c, FALSE);
        conn_unref(c);
        return;
    }
    pump_sock_write(c);
    if (!c->closing && c->ch != NULL) maybe_read_channel(c);
    conn_unref(c);
}

static void pump_sock_write(RelayConn* c) {
    if (c->sock_writing || c->closing) return;
    GBytes* next = g_queue_pop_head(&c->outq);
    if (next == NULL) {
        if (c->close_after_drain) {
            if (c->draining_exit && !c->peer_eof) {
                if (!c->fin_sent) {
                    c->fin_sent = TRUE;
                    if (c->ch != NULL) ssh_channel_force_close(c->ch);
                    GSocket* s = c->sock != NULL ? g_socket_connection_get_socket(c->sock) : NULL;
                    if (s != NULL) g_socket_shutdown(s, FALSE, TRUE, NULL);
                }
            } else {
                conn_abort(c, TRUE);
            }
        }
        return;
    }
    c->cur_write = next;
    c->sock_writing = TRUE;
    gsize len = 0;
    const void* ptr = g_bytes_get_data(next, &len);
    g_output_stream_write_all_async(c->sock_out, ptr, len, G_PRIORITY_DEFAULT, c->cancel,
                                    on_sock_written, conn_ref(c));
}

static void queue_sock_bytes(RelayConn* c, GBytes* bytes) {
    c->outq_bytes += g_bytes_get_size(bytes);
    g_queue_push_tail(&c->outq, bytes);
    pump_sock_write(c);
}

static void send_err_and_close(RelayConn* c, const gchar* msg) {
    if (c->closing || c->close_after_drain) return;
    conn_clear_hs_timeout(c);
    gchar* clean = g_strdup(msg != NULL ? msg : "error");
    for (gchar* p = clean; *p != '\0'; p++) {
        if (*p == '\r' || *p == '\n') *p = ' ';
    }
    gchar* line = g_strdup_printf("ERR %s\n", clean);
    g_free(clean);
    c->close_after_drain = TRUE;
    queue_sock_bytes(c, g_bytes_new_take(line, strlen(line)));
}

static void maybe_finish_exit(RelayConn* c) {
    if (c->closing || c->channel_error || c->close_after_drain) return;
    if (!c->out_eof || !c->err_eof || !c->exit_done) return;
    guint8 payload[4];
    put_be32(payload, (guint32)(gint32)c->exit_status);
    ssh_trace("relay: channel exit id=%s status=%d", c->ticket_id ? c->ticket_id : "-", c->exit_status);
    c->draining_exit = TRUE;
    c->close_after_drain = TRUE;
    queue_sock_bytes(c, build_frame('X', payload, 4));
}

static void on_ch_read_done(GObject* src, GAsyncResult* res, RelayConn* c, gboolean is_stderr) {
    if (is_stderr) c->reading_err = FALSE;
    else c->reading_out = FALSE;

    GError* err = NULL;
    gssize n = g_input_stream_read_finish(G_INPUT_STREAM(src), res, &err);
    if (c->closing) {
        g_clear_error(&err);
        conn_unref(c);
        return;
    }
    if (n > 0) {
        queue_sock_bytes(c, build_frame(is_stderr ? 'R' : 'O',
                                        is_stderr ? c->err_buf : c->out_buf, (guint32)n));
        maybe_read_channel(c);
    } else if (n == 0) {
        if (is_stderr) c->err_eof = TRUE;
        else c->out_eof = TRUE;
        maybe_finish_exit(c);
    } else {
        if (err != NULL && !g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            ssh_trace("relay: channel read error id=%s: %s", c->ticket_id ? c->ticket_id : "-", err->message);
        }
        g_clear_error(&err);
        c->channel_error = TRUE;
        conn_abort(c, FALSE); /* link lost: close without X */
    }
    conn_unref(c);
}

static void on_ch_stdout(GObject* src, GAsyncResult* res, gpointer data) {
    on_ch_read_done(src, res, data, FALSE);
}

static void on_ch_stderr(GObject* src, GAsyncResult* res, gpointer data) {
    on_ch_read_done(src, res, data, TRUE);
}

static void maybe_read_channel(RelayConn* c) {
    if (c->closing || c->channel_error || c->ch == NULL) return;
    if (c->outq_bytes >= RELAY_OUTQ_HIGH) return; /* backpressure on this channel only */
    if (!c->out_eof && !c->reading_out) {
        c->reading_out = TRUE;
        g_input_stream_read_async(c->ch_out, c->out_buf, sizeof c->out_buf, G_PRIORITY_DEFAULT,
                                  c->cancel, on_ch_stdout, conn_ref(c));
    }
    if (!c->err_eof && !c->reading_err) {
        c->reading_err = TRUE;
        g_input_stream_read_async(c->ch_err, c->err_buf, sizeof c->err_buf, G_PRIORITY_DEFAULT,
                                  c->cancel, on_ch_stderr, conn_ref(c));
    }
}

static void on_ch_exit(GObject* src, GAsyncResult* res, gpointer data) {
    RelayConn* c = data;
    GError* err = NULL;
    gint status = ssh_channel_wait_exit_finish(SSH_CHANNEL(src), res, &err);
    if (c->closing) {
        g_clear_error(&err);
        conn_unref(c);
        return;
    }
    if (err != NULL) {
        ssh_trace("relay: channel wait_exit error id=%s: %s", c->ticket_id ? c->ticket_id : "-", err->message);
        g_clear_error(&err);
        c->channel_error = TRUE;
        conn_abort(c, FALSE); /* link lost: close without X */
    } else {
        c->exit_done = TRUE;
        c->exit_status = status;
        maybe_finish_exit(c);
    }
    conn_unref(c);
}

/* Socket -> channel framing */
static void read_next_frame(RelayConn* c);
static void pump_ch_write(RelayConn* c);

static void on_ch_written(GObject* src, GAsyncResult* res, gpointer data) {
    RelayConn* c = data;
    GError* err = NULL;
    gssize n = g_output_stream_write_finish(G_OUTPUT_STREAM(src), res, &err);
    if (c->closing) {
        g_clear_error(&err);
        conn_unref(c);
        return;
    }
    if (n > 0) {
        c->ch_write_off += (gsize)n;
        if (c->ch_write_off < c->frame_len) {
            pump_ch_write(c);
        } else {
            read_next_frame(c);
        }
    } else {
        gboolean lost = err != NULL && g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED);
        g_clear_error(&err);
        if (lost) {
            c->channel_error = TRUE;
            conn_abort(c, FALSE);
        } else {
            /* Remote command closed stdin early; keep draining stdout/stderr/exit */
            c->ch_in_closed = TRUE;
            read_next_frame(c);
        }
    }
    conn_unref(c);
}

static void pump_ch_write(RelayConn* c) {
    g_output_stream_write_async(c->ch_in, c->frame_buf + c->ch_write_off,
                                c->frame_len - c->ch_write_off, G_PRIORITY_DEFAULT,
                                c->cancel, on_ch_written, conn_ref(c));
}

static void on_ch_stdin_closed(GObject* src, GAsyncResult* res, gpointer data) {
    RelayConn* c = data;
    g_output_stream_close_finish(G_OUTPUT_STREAM(src), res, NULL);
    if (!c->closing) read_next_frame(c);
    conn_unref(c);
}

static void dispatch_frame(RelayConn* c) {
    if (c->draining_exit) {
        read_next_frame(c);
        return;
    }
    if (c->frame_type == 'W') {
        guint16 rows = get_be16(c->frame_buf);
        guint16 cols = get_be16(c->frame_buf + 2);
        if (c->tty && rows > 0 && cols > 0) ssh_channel_resize_pty(c->ch, (gint)cols, (gint)rows);
        read_next_frame(c);
    } else if (c->frame_type == 'E') {
        if (!c->ch_in_closed) {
            c->ch_in_closed = TRUE;
            g_output_stream_close_async(c->ch_in, G_PRIORITY_DEFAULT, c->cancel,
                                        on_ch_stdin_closed, conn_ref(c));
        } else {
            read_next_frame(c);
        }
    } else if (c->frame_type == 'I') {
        if (c->ch_in_closed || c->frame_len == 0) {
            read_next_frame(c);
        } else {
            c->ch_write_off = 0;
            pump_ch_write(c);
        }
    }
}

static void on_frame_body(GObject* src, GAsyncResult* res, gpointer data) {
    RelayConn* c = data;
    gsize got = 0;
    GError* err = NULL;
    gboolean ok = g_input_stream_read_all_finish(G_INPUT_STREAM(src), res, &got, &err);
    g_clear_error(&err);
    if (c->closing || !ok || got < c->frame_len) {
        conn_abort(c, FALSE);
        conn_unref(c);
        return;
    }
    dispatch_frame(c);
    conn_unref(c);
}

static void on_frame_hdr(GObject* src, GAsyncResult* res, gpointer data) {
    RelayConn* c = data;
    gsize got = 0;
    GError* err = NULL;
    gboolean ok = g_input_stream_read_all_finish(G_INPUT_STREAM(src), res, &got, &err);
    g_clear_error(&err);
    if (c->closing || !ok || got < 5) {
        c->peer_eof = TRUE;
        /* Relay exited or closed socket: finish if FIN was already sent or not draining */
        if (!c->close_after_drain || c->fin_sent) conn_abort(c, c->fin_sent);
        conn_unref(c);
        return;
    }
    guint8 type = c->frame_hdr[0];
    guint32 len = get_be32(c->frame_hdr + 1);
    gboolean valid = (len <= RELAY_MAX_FRAME) &&
                     ((type == 'I') || (type == 'E' && len == 0) || (type == 'W' && len == 4));
    if (!valid) {
        ssh_trace("relay: bad frame id=%s type=0x%02x len=%u", c->ticket_id ? c->ticket_id : "-",
                  (guint)type, (guint)len);
        conn_abort(c, FALSE);
        conn_unref(c);
        return;
    }
    c->frame_type = type;
    c->frame_len = len;
    if (len == 0) {
        dispatch_frame(c);
    } else {
        g_input_stream_read_all_async(G_INPUT_STREAM(c->sock_in), c->frame_buf, len,
                                      G_PRIORITY_DEFAULT, c->cancel, on_frame_body, conn_ref(c));
    }
    conn_unref(c);
}

static void read_next_frame(RelayConn* c) {
    if (c->closing || (c->close_after_drain && !c->draining_exit)) return;
    g_input_stream_read_all_async(G_INPUT_STREAM(c->sock_in), c->frame_hdr, 5,
                                  G_PRIORITY_DEFAULT, c->cancel, on_frame_hdr, conn_ref(c));
}

/* Handshake state machine */
static void on_channel_opened(GObject* src, GAsyncResult* res, gpointer data) {
    (void)src;
    RelayConn* c = data;
    GError* err = NULL;
    SshChannel* ch = ssh_link_open_channel_finish(c->link, res, &err);
    if (c->closing) {
        g_clear_object(&ch);
        g_clear_error(&err);
        conn_unref(c);
        return;
    }
    if (ch == NULL) {
        ssh_trace("relay: channel open failed id=%s: %s", c->ticket_id ? c->ticket_id : "-",
                  err != NULL ? err->message : "error");
        send_err_and_close(c, err != NULL ? err->message : "could not open channel");
        g_clear_error(&err);
        conn_unref(c);
        return;
    }
    conn_clear_hs_timeout(c);
    c->ch = ch;
    c->ch_out = g_io_stream_get_input_stream(G_IO_STREAM(ch));
    c->ch_err = ssh_channel_get_stderr(ch);
    c->ch_in = g_io_stream_get_output_stream(G_IO_STREAM(ch));

    ssh_trace("relay: channel open id=%s tty=%d", c->ticket_id ? c->ticket_id : "-", c->tty ? 1 : 0);
    const gchar* ok_line = c->tty ? "OK tty\n" : "OK\n";
    queue_sock_bytes(c, g_bytes_new_static(ok_line, strlen(ok_line)));

    ssh_channel_wait_exit_async(ch, c->cancel, on_ch_exit, conn_ref(c));
    maybe_read_channel(c);
    read_next_frame(c);
    conn_unref(c);
}

static gboolean parse_size_line(const gchar* line, gint* out_rows, gint* out_cols) {
    if (line == NULL || !g_ascii_isdigit(line[0])) return FALSE;
    gchar* end1 = NULL;
    gint64 rows = g_ascii_strtoll(line, &end1, 10);
    if (end1 == NULL || *end1 != ' ' || !g_ascii_isdigit(end1[1])) return FALSE;
    gchar* end2 = NULL;
    gint64 cols = g_ascii_strtoll(end1 + 1, &end2, 10);
    if (end2 == NULL || *end2 != '\0') return FALSE;
    if (rows < 0 || rows > 65535 || cols < 0 || cols > 65535) return FALSE;
    *out_rows = (gint)rows;
    *out_cols = (gint)cols;
    return TRUE;
}

static void on_hs_line(GObject* src, GAsyncResult* res, gpointer data) {
    RelayConn* c = data;
    gsize len = 0;
    GError* err = NULL;
    gchar* line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(src), res, &len, &err);
    if (c->closing || line == NULL) {
        wipe_string(line);
        g_clear_error(&err);
        if (!c->closing && c->hs_err != NULL) {
            send_err_and_close(c, c->hs_err);
        } else {
            conn_abort(c, FALSE);
        }
        conn_unref(c);
        return;
    }
    if (len > RELAY_MAX_HANDSHAKE_LEN) {
        wipe_string(line);
        send_err_and_close(c, "handshake line too long");
        conn_unref(c);
        return;
    }
    c->hs_line++;
    if (c->hs_line == 1) {
        gboolean ok = strcmp(line, RELAY_PROTO_LINE) == 0;
        g_free(line);
        if (!ok) {
            send_err_and_close(c, "unsupported protocol");
            conn_unref(c);
            return;
        }
        g_data_input_stream_read_line_async(c->sock_in, G_PRIORITY_DEFAULT, c->cancel, on_hs_line, conn_ref(c));
        conn_unref(c);
        return;
    }
    if (c->hs_line == 2) {
        gboolean ok = consume_ticket(line, &c->link, &c->tty, &c->ticket_id);
        wipe_string(line);
        if (!ok) c->hs_err = "invalid ticket";
        g_data_input_stream_read_line_async(c->sock_in, G_PRIORITY_DEFAULT, c->cancel, on_hs_line, conn_ref(c));
        conn_unref(c);
        return;
    }
    if (c->hs_line == 3) {
        if (c->hs_err == NULL && !parse_size_line(line, &c->rows, &c->cols)) {
            c->hs_err = "invalid window size";
        }
        g_free(line);
        g_data_input_stream_read_line_async(c->sock_in, G_PRIORITY_DEFAULT, c->cancel, on_hs_line, conn_ref(c));
        conn_unref(c);
        return;
    }
    /* Line 4: command (g_strescape()d so multi-line scripts such as ipynb_bridge.py pass intact) */
    if (c->hs_err != NULL) {
        g_free(line);
        send_err_and_close(c, c->hs_err);
        conn_unref(c);
        return;
    }
    gchar* cmd = g_strcompress(line);
    g_free(line);
    SshChannelKind kind = (cmd[0] == '\0') ? SSH_CHANNEL_KIND_SHELL : SSH_CHANNEL_KIND_EXEC;
    const gchar* arg = (kind == SSH_CHANNEL_KIND_SHELL) ? "xterm-256color" : cmd;
    gint cols = 0;
    gint rows = 0;
    if (c->tty) {
        cols = c->cols > 0 ? c->cols : 80;
        rows = c->rows > 0 ? c->rows : 24;
    }
    ssh_link_open_channel_async(c->link, kind, arg, cols, rows, c->cancel, on_channel_opened, conn_ref(c));
    g_free(cmd);
    conn_unref(c);
}

static gboolean on_hs_timeout(gpointer data) {
    RelayConn* c = data;
    c->hs_timeout = NULL;
    ssh_trace("relay: handshake timeout id=%s", c->ticket_id ? c->ticket_id : "-");
    conn_abort(c, FALSE);
    return G_SOURCE_REMOVE;
}

static gboolean on_incoming(GSocketService* svc, GSocketConnection* sock, GObject* src, gpointer data) {
    (void)svc;
    (void)src;
    RelayServer* srv = data;
    if (srv->stopping) return TRUE;
    enable_nodelay(sock);

    RelayConn* c = g_new0(RelayConn, 1);
    c->refs = 1; /* owned by srv->active_conns until conn_abort() */
    c->srv = srv;
    c->sock = g_object_ref(sock);
    c->sock_in = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(sock)));
    g_data_input_stream_set_newline_type(c->sock_in, G_DATA_STREAM_NEWLINE_TYPE_LF);
    c->sock_out = g_io_stream_get_output_stream(G_IO_STREAM(sock));
    c->cancel = g_cancellable_new();
    g_queue_init(&c->outq);
    c->exit_status = -1;

    srv->live_conns++;
    g_hash_table_add(srv->active_conns, c);

    c->hs_timeout = g_timeout_source_new_seconds(RELAY_HANDSHAKE_SEC);
    g_source_set_callback(c->hs_timeout, on_hs_timeout, c, NULL);
    g_source_attach(c->hs_timeout, server_ctx);

    g_data_input_stream_read_line_async(c->sock_in, G_PRIORITY_DEFAULT, c->cancel, on_hs_line, conn_ref(c));
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/* Server thread lifecycle                                                   */
/* ------------------------------------------------------------------------- */

typedef struct {
    GMutex mu;
    GCond cond;
    gboolean ready;
    guint16 port;
    GError* err;
    GMainContext* ctx;
    GMainLoop* loop;
} StartSync;

static gboolean on_sweep_tickets(gpointer data) {
    (void)data;
    g_mutex_lock(&relay_mu);
    sweep_expired_tickets_locked();
    g_mutex_unlock(&relay_mu);
    return G_SOURCE_CONTINUE;
}

static gboolean on_stop_safety(gpointer data) {
    RelayServer* srv = data;
    srv->stop_timer = NULL;
    if (server_loop != NULL) g_main_loop_quit(server_loop);
    return G_SOURCE_REMOVE;
}

static gboolean on_server_stop(gpointer data) {
    (void)data;
    RelayServer* srv = g_server;
    if (srv == NULL) {
        if (server_loop != NULL) g_main_loop_quit(server_loop);
        return G_SOURCE_REMOVE;
    }
    srv->stopping = TRUE;
    if (srv->sweep_src != NULL) {
        g_source_destroy(srv->sweep_src);
        g_source_unref(srv->sweep_src);
        srv->sweep_src = NULL;
    }
    if (srv->service != NULL) {
        g_socket_service_stop(srv->service);
        g_socket_listener_close(G_SOCKET_LISTENER(srv->service));
        g_clear_object(&srv->service);
    }
    /* Copy keys before aborting since conn_abort() removes from active_conns */
    GPtrArray* list = g_ptr_array_new();
    GHashTableIter it;
    gpointer key;
    g_hash_table_iter_init(&it, srv->active_conns);
    while (g_hash_table_iter_next(&it, &key, NULL)) g_ptr_array_add(list, key);
    for (guint i = 0; i < list->len; i++) conn_abort(list->pdata[i], FALSE);
    g_ptr_array_unref(list);

    if (srv->live_conns == 0) {
        if (server_loop != NULL) g_main_loop_quit(server_loop);
    } else if (srv->stop_timer == NULL) {
        srv->stop_timer = g_timeout_source_new(STOP_SAFETY_MS);
        g_source_set_callback(srv->stop_timer, on_stop_safety, srv, NULL);
        g_source_attach(srv->stop_timer, server_ctx);
    }
    return G_SOURCE_REMOVE;
}

static gpointer relay_thread_main(gpointer data) {
    StartSync* s = data;
    GMainContext* ctx = g_main_context_ref(s->ctx);
    GMainLoop* loop = g_main_loop_ref(s->loop);
    g_main_context_push_thread_default(ctx);

    GSocketService* svc = g_socket_service_new();
    GInetAddress* lo = g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4);
    GSocketAddress* addr = g_inet_socket_address_new(lo, 0);
    GSocketAddress* bound = NULL;
    GError* err = NULL;
    gboolean ok = g_socket_listener_add_address(G_SOCKET_LISTENER(svc), addr, G_SOCKET_TYPE_STREAM,
                                                G_SOCKET_PROTOCOL_TCP, NULL, &bound, &err);
    g_object_unref(addr);
    g_object_unref(lo);
    if (!ok) {
        g_object_unref(svc);
        g_mutex_lock(&s->mu);
        s->err = err;
        s->ready = TRUE;
        g_cond_signal(&s->cond);
        g_mutex_unlock(&s->mu);
        g_main_context_pop_thread_default(ctx);
        g_main_loop_unref(loop);
        g_main_context_unref(ctx);
        return NULL;
    }

    guint16 port = g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(bound));
    g_object_unref(bound);

    RelayServer srv;
    memset(&srv, 0, sizeof srv);
    srv.service = svc;
    srv.active_conns = g_hash_table_new(g_direct_hash, g_direct_equal);
    g_server = &srv;

    g_signal_connect(svc, "incoming", G_CALLBACK(on_incoming), &srv);
    g_socket_service_start(svc);

    srv.sweep_src = g_timeout_source_new_seconds(TICKET_SWEEP_SEC);
    g_source_set_callback(srv.sweep_src, on_sweep_tickets, NULL, NULL);
    g_source_attach(srv.sweep_src, ctx);

    ssh_trace("relay: listening on 127.0.0.1:%u", (guint)port);

    g_mutex_lock(&s->mu);
    s->port = port;
    s->ready = TRUE;
    g_cond_signal(&s->cond);
    g_mutex_unlock(&s->mu);

    g_main_loop_run(loop);

    if (srv.stop_timer != NULL) {
        g_source_destroy(srv.stop_timer);
        g_source_unref(srv.stop_timer);
    }
    if (srv.sweep_src != NULL) {
        g_source_destroy(srv.sweep_src);
        g_source_unref(srv.sweep_src);
    }
    g_clear_object(&srv.service);
    g_hash_table_destroy(srv.active_conns);
    g_server = NULL;

    ssh_trace("relay: stopped");

    g_main_context_pop_thread_default(ctx);
    g_main_loop_unref(loop);
    g_main_context_unref(ctx);
    return NULL;
}

gboolean ssh_relay_serve(GError** error) {
    g_mutex_lock(&relay_mu);
    if (server_running) {
        g_mutex_unlock(&relay_mu);
        return TRUE;
    }
    GMainContext* ctx = g_main_context_new();
    GMainLoop* loop = g_main_loop_new(ctx, FALSE);
    StartSync s;
    memset(&s, 0, sizeof s);
    g_mutex_init(&s.mu);
    g_cond_init(&s.cond);
    s.ctx = ctx;
    s.loop = loop;

    server_ctx = ctx;
    server_loop = loop;
    GThread* th = g_thread_new("ssh-relay", relay_thread_main, &s);

    g_mutex_lock(&s.mu);
    while (!s.ready) g_cond_wait(&s.cond, &s.mu);
    guint16 port = s.port;
    GError* err = s.err;
    g_mutex_unlock(&s.mu);
    g_mutex_clear(&s.mu);
    g_cond_clear(&s.cond);

    if (err != NULL) {
        g_thread_join(th);
        g_clear_pointer(&server_loop, g_main_loop_unref);
        g_clear_pointer(&server_ctx, g_main_context_unref);
        g_mutex_unlock(&relay_mu);
        g_propagate_error(error, err);
        return FALSE;
    }
    server_thread = th;
    server_port = port;
    server_running = TRUE;
    g_mutex_unlock(&relay_mu);
    return TRUE;
}

void ssh_relay_stop(void) {
    g_mutex_lock(&relay_mu);
    GThread* th = g_steal_pointer(&server_thread);
    GMainContext* ctx = server_ctx;
    server_running = FALSE;
    server_port = 0;
    if (tickets != NULL) g_hash_table_remove_all(tickets);
    g_mutex_unlock(&relay_mu);

    if (th != NULL && ctx != NULL) {
        g_main_context_invoke(ctx, on_server_stop, NULL);
        g_thread_join(th);
    }

    g_mutex_lock(&relay_mu);
    g_clear_pointer(&server_loop, g_main_loop_unref);
    g_clear_pointer(&server_ctx, g_main_context_unref);
    g_clear_pointer(&tickets, g_hash_table_destroy);
    g_mutex_unlock(&relay_mu);
}

#endif /* HAVE_LIBSSH */
