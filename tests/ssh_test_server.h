/* In-process libssh test server (libssh/server.h) for tests/test_ssh_link.c
 * and tests/test_ssh_channel.c. Listens on 127.0.0.1:0, handles multiple
 * sequential connections and multiple concurrent channels per connection
 * without blocking, supports password, public-key and keyboard-interactive
 * authentication, fixed exec commands, PTY shell + resize, and (when
 * SSH_TEST_SERVER_WITH_SFTP is defined) the sftp subsystem bridged to
 * tests/sftp_test_server.h. */
#ifndef SSH_TEST_SERVER_H
#define SSH_TEST_SERVER_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <libssh/callbacks.h>
#include <libssh/libssh.h>
#include <libssh/server.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef SSH_TEST_SERVER_WITH_SFTP
#include "sftp_test_server.h"
#endif

#define FLOOD_TOTAL (2u * 1024u * 1024u)

typedef struct SshTestServer SshTestServer;

typedef enum {
    SRV_CH_NONE,
    SRV_CH_ECHO,
    SRV_CH_FLOOD,
    SRV_CH_HANG,
    SRV_CH_SHELL,
    SRV_CH_SFTP,
} SrvChKind;

typedef struct {
    SshTestServer* srv;
    ssh_channel ch;
    struct ssh_channel_callbacks_struct cb;
    SrvChKind kind;
    gboolean closing;
    gboolean eof_in;
    GByteArray* out_q;
    guint flood_sent;
    gint pty_cols;
    gint pty_rows;
#ifdef SSH_TEST_SERVER_WITH_SFTP
    int sftp_fd;
    Server sftp_srv;
    gboolean sftp_started;
#endif
} SrvChan;

struct SshTestServer {
    int listen_fd;
    int wake_pipe[2];
    int port;
    GMutex mu;
    ssh_key host_key;
    gchar* expected_user;
    gchar* expected_password;
    ssh_key allowed_pubkey;
    gboolean require_kbdint;
    gchar* kbdint_a1;
    gchar* kbdint_a2;
    gboolean drop_keepalive;
    gboolean drop_active_conn;
    gboolean stop;
    guint auth_success_count;
    guint keepalive_count;
    GThread* thread;
};

static guint8 flood_byte(guint idx) {
    return (guint8)((idx * 13u + idx / 257u) & 0xffu);
}

static void srv_wake(SshTestServer* srv) {
    char b = 1;
    ssize_t r = write(srv->wake_pipe[1], &b, 1);
    (void)r;
}

static void srv_chan_free(gpointer data) {
    SrvChan* c = data;
    if (c == NULL) return;
#ifdef SSH_TEST_SERVER_WITH_SFTP
    if (c->sftp_fd >= 0) {
        close(c->sftp_fd);
        c->sftp_fd = -1;
    }
    if (c->sftp_started) {
        g_thread_join(c->sftp_srv.thread);
        c->sftp_started = FALSE;
    }
#endif
    if (c->ch != NULL) {
        ssh_remove_channel_callbacks(c->ch, &c->cb);
        ssh_channel_free(c->ch);
    }
    if (c->out_q != NULL) g_byte_array_unref(c->out_q);
    g_free(c);
}

static int on_srv_ch_data(ssh_session s, ssh_channel ch, void* data, uint32_t len, int is_stderr, void* ud) {
    (void)s;
    (void)ch;
    (void)is_stderr;
    SrvChan* c = ud;
    if (c->kind == SRV_CH_ECHO) {
        g_byte_array_append(c->out_q, data, len);
        return (int)len;
    }
#ifdef SSH_TEST_SERVER_WITH_SFTP
    if (c->kind == SRV_CH_SFTP && c->sftp_fd >= 0) {
        const guint8* p = data;
        uint32_t rem = len;
        while (rem > 0) {
            ssize_t w = write(c->sftp_fd, p, rem);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) break;
            p += w;
            rem -= (uint32_t)w;
        }
        return (int)(len - rem);
    }
#endif
    return (int)len;
}

static void on_srv_ch_eof(ssh_session s, ssh_channel ch, void* ud) {
    (void)s;
    (void)ch;
    SrvChan* c = ud;
    c->eof_in = TRUE;
#ifdef SSH_TEST_SERVER_WITH_SFTP
    if (c->kind == SRV_CH_SFTP && c->sftp_fd >= 0) shutdown(c->sftp_fd, SHUT_WR);
#endif
}

static void on_srv_ch_close(ssh_session s, ssh_channel ch, void* ud) {
    (void)s;
    (void)ch;
    SrvChan* c = ud;
    c->closing = TRUE;
}

static int on_srv_pty_req(ssh_session s, ssh_channel ch, const char* term, int w, int h, int pw, int ph, void* ud) {
    (void)s;
    (void)ch;
    (void)term;
    (void)pw;
    (void)ph;
    SrvChan* c = ud;
    c->pty_cols = w;
    c->pty_rows = h;
    return 0;
}

static int on_srv_pty_resize(ssh_session s, ssh_channel ch, int w, int h, int pw, int ph, void* ud) {
    (void)s;
    (void)ch;
    (void)pw;
    (void)ph;
    SrvChan* c = ud;
    c->pty_cols = w;
    c->pty_rows = h;
    gchar* line = g_strdup_printf("resize:%dx%d\n", w, h);
    g_byte_array_append(c->out_q, (const guint8*)line, (guint)strlen(line));
    g_free(line);
    return 0;
}

static int on_srv_shell_req(ssh_session s, ssh_channel ch, void* ud) {
    (void)s;
    (void)ch;
    SrvChan* c = ud;
    c->kind = SRV_CH_SHELL;
    gchar* line = g_strdup_printf("pty:%dx%d\n", c->pty_cols, c->pty_rows);
    g_byte_array_append(c->out_q, (const guint8*)line, (guint)strlen(line));
    g_free(line);
    return 0;
}

static int on_srv_exec_req(ssh_session s, ssh_channel ch, const char* cmd, void* ud) {
    (void)s;
    SrvChan* c = ud;
    if (strcmp(cmd, "hello") == 0) {
        ssh_channel_write(ch, "hello stdout\n", 13);
        ssh_channel_write_stderr(ch, "hello stderr\n", 13);
        ssh_channel_send_eof(ch);
        ssh_channel_request_send_exit_status(ch, 0);
        ssh_channel_close(ch);
        c->closing = TRUE;
        return 0;
    }
    if (strcmp(cmd, "exit42") == 0) {
        ssh_channel_write(ch, "bye", 3);
        ssh_channel_send_eof(ch);
        ssh_channel_request_send_exit_status(ch, 42);
        ssh_channel_close(ch);
        c->closing = TRUE;
        return 0;
    }
    if (strcmp(cmd, "echo") == 0) {
        c->kind = SRV_CH_ECHO;
        return 0;
    }
    if (strcmp(cmd, "flood") == 0) {
        c->kind = SRV_CH_FLOOD;
        return 0;
    }
    if (strcmp(cmd, "hang") == 0) {
        c->kind = SRV_CH_HANG;
        return 0;
    }
    return 1;
}

static int on_srv_subsys_req(ssh_session s, ssh_channel ch, const char* subsys, void* ud) {
    (void)s;
    (void)ch;
    SrvChan* c = ud;
#ifdef SSH_TEST_SERVER_WITH_SFTP
    if (strcmp(subsys, "sftp") == 0) {
        int fds[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return 1;
        int fl = fcntl(fds[0], F_GETFL, 0);
        if (fl >= 0) fcntl(fds[0], F_SETFL, fl | O_NONBLOCK);
        c->kind = SRV_CH_SFTP;
        c->sftp_fd = fds[0];
        memset(&c->sftp_srv, 0, sizeof c->sftp_srv);
        c->sftp_srv.fd = fds[1];
        c->sftp_srv.out_fd = fds[1];
        c->sftp_srv.posix_rename = TRUE;
        c->sftp_srv.thread = g_thread_new("test-sftp", server_main, &c->sftp_srv);
        c->sftp_started = TRUE;
        return 0;
    }
#else
    (void)subsys;
    (void)c;
#endif
    return 1;
}

static void pump_srv_chan(SrvChan* c) {
    if (c->closing || c->ch == NULL || ssh_channel_is_closed(c->ch)) return;
    if (c->kind == SRV_CH_ECHO || c->kind == SRV_CH_SHELL) {
        if (c->out_q->len > 0) {
            uint32_t win = ssh_channel_window_size(c->ch);
            if (win > 0) {
                uint32_t n = MIN(c->out_q->len, MIN(win, 32768u));
                int w = ssh_channel_write(c->ch, c->out_q->data, n);
                if (w > 0) g_byte_array_remove_range(c->out_q, 0, (guint)w);
            }
        }
        if (c->kind == SRV_CH_ECHO && c->eof_in && c->out_q->len == 0) {
            ssh_channel_send_eof(c->ch);
            ssh_channel_request_send_exit_status(c->ch, 0);
            ssh_channel_close(c->ch);
            c->closing = TRUE;
        }
    } else if (c->kind == SRV_CH_FLOOD) {
        guint8 chunk[16384];
        while (c->flood_sent < FLOOD_TOTAL) {
            uint32_t win = ssh_channel_window_size(c->ch);
            if (win == 0) break;
            uint32_t n = MIN(FLOOD_TOTAL - c->flood_sent, MIN(win, (uint32_t)sizeof chunk));
            for (uint32_t i = 0; i < n; i++) chunk[i] = flood_byte(c->flood_sent + i);
            int w = ssh_channel_write(c->ch, chunk, n);
            if (w <= 0) break;
            c->flood_sent += (guint)w;
        }
        if (c->flood_sent >= FLOOD_TOTAL) {
            ssh_channel_send_eof(c->ch);
            ssh_channel_request_send_exit_status(c->ch, 0);
            ssh_channel_close(c->ch);
            c->closing = TRUE;
        }
    }
#ifdef SSH_TEST_SERVER_WITH_SFTP
    else if (c->kind == SRV_CH_SFTP && c->sftp_fd >= 0) {
        if (c->out_q->len < 65536) {
            guint8 buf[32768];
            ssize_t r = read(c->sftp_fd, buf, sizeof buf);
            if (r > 0) {
                g_byte_array_append(c->out_q, buf, (guint)r);
            } else if (r == 0) {
                close(c->sftp_fd);
                c->sftp_fd = -1;
            }
        }
        if (c->out_q->len > 0) {
            uint32_t win = ssh_channel_window_size(c->ch);
            if (win > 0) {
                uint32_t n = MIN(c->out_q->len, MIN(win, 32768u));
                int w = ssh_channel_write(c->ch, c->out_q->data, n);
                if (w > 0) g_byte_array_remove_range(c->out_q, 0, (guint)w);
            }
        }
        if (c->sftp_fd < 0 && c->out_q->len == 0) {
            ssh_channel_send_eof(c->ch);
            ssh_channel_request_send_exit_status(c->ch, 0);
            ssh_channel_close(c->ch);
            c->closing = TRUE;
        }
    }
#endif
}

typedef struct {
    SshTestServer* srv;
    GPtrArray* chans;
    gboolean authed;
    gboolean kbdint_sent;
} SrvConn;

static int on_srv_auth_password(ssh_session s, const char* user, const char* password, void* ud) {
    (void)s;
    SrvConn* conn = ud;
    SshTestServer* srv = conn->srv;
    g_mutex_lock(&srv->mu);
    gboolean ok = !srv->require_kbdint && srv->expected_password != NULL &&
                  g_strcmp0(user, srv->expected_user) == 0 &&
                  g_strcmp0(password, srv->expected_password) == 0;
    if (ok) srv->auth_success_count++;
    g_mutex_unlock(&srv->mu);
    if (ok) conn->authed = TRUE;
    return ok ? SSH_AUTH_SUCCESS : SSH_AUTH_DENIED;
}

static int on_srv_auth_pubkey(ssh_session s, const char* user, struct ssh_key_struct* pubkey,
                              char sig_state, void* ud) {
    (void)s;
    SrvConn* conn = ud;
    SshTestServer* srv = conn->srv;
    g_mutex_lock(&srv->mu);
    gboolean match = srv->allowed_pubkey != NULL && g_strcmp0(user, srv->expected_user) == 0 &&
                     ssh_key_cmp(pubkey, srv->allowed_pubkey, SSH_KEY_CMP_PUBLIC) == 0;
    if (match && sig_state == SSH_PUBLICKEY_STATE_VALID) srv->auth_success_count++;
    g_mutex_unlock(&srv->mu);
    if (!match) return SSH_AUTH_DENIED;
    if (sig_state == SSH_PUBLICKEY_STATE_NONE) return SSH_AUTH_SUCCESS;
    if (sig_state == SSH_PUBLICKEY_STATE_VALID) {
        conn->authed = TRUE;
        return SSH_AUTH_SUCCESS;
    }
    return SSH_AUTH_DENIED;
}

static ssh_channel on_srv_open_session(ssh_session s, void* ud) {
    SrvConn* conn = ud;
    if (!conn->authed) return NULL;
    SrvChan* c = g_new0(SrvChan, 1);
    c->srv = conn->srv;
    c->out_q = g_byte_array_new();
#ifdef SSH_TEST_SERVER_WITH_SFTP
    c->sftp_fd = -1;
#endif
    c->ch = ssh_channel_new(s);
    c->cb.userdata = c;
    c->cb.channel_data_function = on_srv_ch_data;
    c->cb.channel_eof_function = on_srv_ch_eof;
    c->cb.channel_close_function = on_srv_ch_close;
    c->cb.channel_exec_request_function = on_srv_exec_req;
    c->cb.channel_subsystem_request_function = on_srv_subsys_req;
    c->cb.channel_pty_request_function = on_srv_pty_req;
    c->cb.channel_pty_window_change_function = on_srv_pty_resize;
    c->cb.channel_shell_request_function = on_srv_shell_req;
    ssh_callbacks_init(&c->cb);
    ssh_set_channel_callbacks(c->ch, &c->cb);
    g_ptr_array_add(conn->chans, c);
    return c->ch;
}

static void on_srv_global_req(ssh_session s, ssh_message msg, void* ud) {
    (void)s;
    SrvConn* conn = ud;
    SshTestServer* srv = conn->srv;
    g_mutex_lock(&srv->mu);
    srv->keepalive_count++;
    gboolean drop = srv->drop_keepalive;
    g_mutex_unlock(&srv->mu);
    if (!drop) ssh_message_reply_default(msg);
}

static int on_srv_message(ssh_session s, ssh_message msg, void* ud) {
    SrvConn* conn = ud;
    SshTestServer* srv = conn->srv;
    int type = ssh_message_type(msg);
    int sub = ssh_message_subtype(msg);

    if (type == SSH_REQUEST_AUTH && sub == SSH_AUTH_METHOD_INTERACTIVE) {
        g_mutex_lock(&srv->mu);
        gboolean want = srv->require_kbdint;
        gchar* a1 = g_strdup(srv->kbdint_a1);
        gchar* a2 = g_strdup(srv->kbdint_a2);
        g_mutex_unlock(&srv->mu);
        if (!want) {
            g_free(a1);
            g_free(a2);
            return 1;
        }
        if (!ssh_message_auth_kbdint_is_response(msg)) {
            const char* prompts[2] = { "Code 1:", "Code 2:" };
            char echo[2] = { 0, 0 };
            ssh_message_auth_interactive_request(msg, "2FA", "Enter both codes", 2, prompts, echo);
        } else {
            int n = ssh_userauth_kbdint_getnanswers(s);
            const char* r1 = n >= 1 ? ssh_userauth_kbdint_getanswer(s, 0) : NULL;
            const char* r2 = n >= 2 ? ssh_userauth_kbdint_getanswer(s, 1) : NULL;
            if (n == 2 && g_strcmp0(r1, a1) == 0 && g_strcmp0(r2, a2) == 0) {
                g_mutex_lock(&srv->mu);
                srv->auth_success_count++;
                g_mutex_unlock(&srv->mu);
                conn->authed = TRUE;
                ssh_message_auth_reply_success(msg, 0);
            } else {
                ssh_message_reply_default(msg);
            }
        }
        g_free(a1);
        g_free(a2);
        return 0;
    }

    if (type == SSH_REQUEST_GLOBAL) {
        g_mutex_lock(&srv->mu);
        srv->keepalive_count++;
        gboolean drop = srv->drop_keepalive;
        g_mutex_unlock(&srv->mu);
        if (!drop) ssh_message_reply_default(msg);
        return 0;
    }

    return 1; /* let libssh handle via server/channel callbacks or default */
}

static void serve_connection(SshTestServer* srv, int client_fd) {
    ssh_bind b = ssh_bind_new();
    g_mutex_lock(&srv->mu);
    ssh_key hk = ssh_key_dup(srv->host_key);
    int methods = 0;
    if (srv->allowed_pubkey != NULL) methods |= SSH_AUTH_METHOD_PUBLICKEY;
    if (srv->require_kbdint) {
        methods |= SSH_AUTH_METHOD_INTERACTIVE;
    } else if (srv->expected_password != NULL) {
        methods |= SSH_AUTH_METHOD_PASSWORD;
    }
    srv->drop_active_conn = FALSE;
    g_mutex_unlock(&srv->mu);

    ssh_bind_options_set(b, SSH_BIND_OPTIONS_IMPORT_KEY, hk);

    ssh_session s = ssh_new();
    if (ssh_bind_accept_fd(b, s, client_fd) != SSH_OK) {
        close(client_fd);
        ssh_free(s);
        ssh_bind_free(b);
        return;
    }

    SrvConn conn = { .srv = srv, .chans = g_ptr_array_new_with_free_func(srv_chan_free) };
    struct ssh_callbacks_struct ccb;
    memset(&ccb, 0, sizeof ccb);
    ccb.userdata = &conn;
    ccb.global_request_function = on_srv_global_req;
    ssh_callbacks_init(&ccb);
    ssh_set_callbacks(s, &ccb);

    struct ssh_server_callbacks_struct scb;
    memset(&scb, 0, sizeof scb);
    scb.userdata = &conn;
    scb.auth_password_function = on_srv_auth_password;
    scb.auth_pubkey_function = on_srv_auth_pubkey;
    scb.channel_open_request_session_function = on_srv_open_session;
    ssh_callbacks_init(&scb);
    ssh_set_server_callbacks(s, &scb);
    ssh_set_message_callback(s, on_srv_message, &conn);
    ssh_set_auth_methods(s, methods);

    if (ssh_handle_key_exchange(s) != SSH_OK) {
        g_ptr_array_unref(conn.chans);
        ssh_disconnect(s);
        ssh_free(s);
        ssh_bind_free(b);
        return;
    }

    ssh_set_blocking(s, 0);
    ssh_event ev = ssh_event_new();
    ssh_event_add_session(ev, s);

    for (;;) {
        g_mutex_lock(&srv->mu);
        gboolean stop = srv->stop;
        gboolean drop = srv->drop_active_conn;
        g_mutex_unlock(&srv->mu);
        if (stop || drop) {
            if (drop) {
                /* Abruptly close TCP socket without SSH disconnect packet */
                socket_t fd = ssh_get_fd(s);
                if (fd != SSH_INVALID_SOCKET) shutdown(fd, SHUT_RDWR);
            }
            break;
        }
        int rc = ssh_event_dopoll(ev, 10);
        if (rc == SSH_ERROR || !ssh_is_connected(s)) break;
        for (guint i = 0; i < conn.chans->len; i++) {
            pump_srv_chan(conn.chans->pdata[i]);
        }
    }

    ssh_event_remove_session(ev, s);
    ssh_event_free(ev);
    g_ptr_array_unref(conn.chans);
    ssh_disconnect(s);
    ssh_free(s);
    ssh_bind_free(b);
}

static gpointer ssh_test_server_main(gpointer data) {
    SshTestServer* srv = data;
    for (;;) {
        g_mutex_lock(&srv->mu);
        gboolean stop = srv->stop;
        g_mutex_unlock(&srv->mu);
        if (stop) break;

        struct pollfd pfds[2] = {
            { .fd = srv->listen_fd, .events = POLLIN },
            { .fd = srv->wake_pipe[0], .events = POLLIN },
        };
        int pr = poll(pfds, 2, 50);
        if (pr <= 0) continue;
        if (pfds[1].revents & POLLIN) {
            char drain[64];
            ssize_t r = read(srv->wake_pipe[0], drain, sizeof drain);
            (void)r;
            continue;
        }
        if (pfds[0].revents & POLLIN) {
            int cfd = accept(srv->listen_fd, NULL, NULL);
            if (cfd >= 0) serve_connection(srv, cfd);
        }
    }
    return NULL;
}

G_GNUC_UNUSED static SshTestServer* ssh_test_server_start(const gchar* user, const gchar* password) {
    ssh_threads_set_callbacks(ssh_threads_get_default());
    ssh_init();

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    g_assert_cmpint(fd, >=, 0);
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    g_assert_cmpint(bind(fd, (struct sockaddr*)&addr, sizeof addr), ==, 0);
    g_assert_cmpint(listen(fd, 8), ==, 0);
    socklen_t alen = sizeof addr;
    g_assert_cmpint(getsockname(fd, (struct sockaddr*)&addr, &alen), ==, 0);

    SshTestServer* srv = g_new0(SshTestServer, 1);
    srv->listen_fd = fd;
    g_assert_cmpint(pipe(srv->wake_pipe), ==, 0);
    srv->port = (int)ntohs(addr.sin_port);
    g_mutex_init(&srv->mu);
    g_assert_cmpint(ssh_pki_generate(SSH_KEYTYPE_ED25519, 0, &srv->host_key), ==, SSH_OK);
    srv->expected_user = g_strdup(user);
    srv->expected_password = g_strdup(password);
    srv->thread = g_thread_new("ssh-test-server", ssh_test_server_main, srv);
    return srv;
}

G_GNUC_UNUSED static void ssh_test_server_rotate_host_key(SshTestServer* srv) {
    ssh_key nk = NULL;
    g_assert_cmpint(ssh_pki_generate(SSH_KEYTYPE_ED25519, 0, &nk), ==, SSH_OK);
    g_mutex_lock(&srv->mu);
    if (srv->host_key != NULL) ssh_key_free(srv->host_key);
    srv->host_key = nk;
    g_mutex_unlock(&srv->mu);
}

G_GNUC_UNUSED static void ssh_test_server_set_pubkey(SshTestServer* srv, ssh_key pubkey) {
    g_mutex_lock(&srv->mu);
    if (srv->allowed_pubkey != NULL) ssh_key_free(srv->allowed_pubkey);
    srv->allowed_pubkey = pubkey != NULL ? ssh_key_dup(pubkey) : NULL;
    g_mutex_unlock(&srv->mu);
}

G_GNUC_UNUSED static void ssh_test_server_set_kbdint(SshTestServer* srv, const gchar* a1, const gchar* a2) {
    g_mutex_lock(&srv->mu);
    srv->require_kbdint = (a1 != NULL);
    g_free(srv->kbdint_a1);
    g_free(srv->kbdint_a2);
    srv->kbdint_a1 = g_strdup(a1);
    srv->kbdint_a2 = g_strdup(a2);
    g_mutex_unlock(&srv->mu);
}

G_GNUC_UNUSED static void ssh_test_server_drop_connection(SshTestServer* srv) {
    g_mutex_lock(&srv->mu);
    srv->drop_active_conn = TRUE;
    g_mutex_unlock(&srv->mu);
    srv_wake(srv);
}

G_GNUC_UNUSED static void ssh_test_server_stop(SshTestServer* srv) {
    if (srv == NULL) return;
    g_mutex_lock(&srv->mu);
    srv->stop = TRUE;
    g_mutex_unlock(&srv->mu);
    srv_wake(srv);
    g_thread_join(srv->thread);
    close(srv->listen_fd);
    close(srv->wake_pipe[0]);
    close(srv->wake_pipe[1]);
    if (srv->host_key != NULL) ssh_key_free(srv->host_key);
    if (srv->allowed_pubkey != NULL) ssh_key_free(srv->allowed_pubkey);
    g_free(srv->expected_user);
    g_free(srv->expected_password);
    g_free(srv->kbdint_a1);
    g_free(srv->kbdint_a2);
    g_mutex_clear(&srv->mu);
    g_free(srv);
}

#endif /* SSH_TEST_SERVER_H */
