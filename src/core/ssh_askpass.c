/* ssh password prompts relayed to the running instance (see ssh_askpass.h).
 * The helper sends three lines, the token, the kind (0, 1, 2) and the prompt
 * (g_strescape()d); the instance answers "OK <answer escaped>" or "NO". */
#include "core/ssh_askpass.h"
#include "core/ssh_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GSocketService* service;
static gchar* token;
static gchar* address; /* "<port>:<token>" */
static gboolean display_set; /* DISPLAY given to ssh by ssh_askpass_serve() */
static SshAskpassFunc ask_func;
static gpointer ask_data;

struct SshAskpassRequest {
    GSocketConnection* conn;
};

/* ------------------------------------------------------------------------- */
/* The instance: listening                                                   */
/* ------------------------------------------------------------------------- */

typedef struct {
    GSocketConnection* conn;
    GDataInputStream* in;
    guint line;      /* lines read */
    SshAskpassKind kind;
} Incoming;

static void incoming_free(Incoming* c) {
    g_clear_object(&c->in);
    g_clear_object(&c->conn);
    g_free(c);
}

/* Same strings, in a time that does not tell how much of them matched */
static gboolean same_secret(const gchar* a, const gchar* b) {
    gsize la = strlen(a);
    gsize lb = strlen(b);
    guchar diff = la != lb;
    for (gsize i = 0; i < la && i < lb; i++) diff |= (guchar)(a[i] ^ b[i]);
    return diff == 0;
}

static void write_line(GSocketConnection* conn, const gchar* line) {
    GOutputStream* out = g_io_stream_get_output_stream(G_IO_STREAM(conn));
    /* A few bytes to a local socket: never blocks for long */
    g_output_stream_write_all(out, line, strlen(line), NULL, NULL, NULL);
    g_output_stream_write_all(out, "\n", 1, NULL, NULL, NULL);
    g_io_stream_close(G_IO_STREAM(conn), NULL, NULL);
}

void ssh_askpass_reply(SshAskpassRequest* req, const gchar* answer) {
    if (req == NULL) return;
    if (answer != NULL) {
        gchar* esc = g_strescape(answer, NULL);
        gchar* line = g_strconcat("OK ", esc, NULL);
        write_line(req->conn, line);
        memset(line, 0, strlen(line));
        memset(esc, 0, strlen(esc));
        g_free(line);
        g_free(esc);
    } else {
        write_line(req->conn, "NO");
    }
    g_object_unref(req->conn);
    g_free(req);
}

static void on_line(GObject* source, GAsyncResult* res, gpointer data) {
    Incoming* c = data;
    gchar* line = g_data_input_stream_read_line_finish_utf8(G_DATA_INPUT_STREAM(source), res, NULL, NULL);
    if (line == NULL || token == NULL) { /* closed, not UTF-8, or no longer serving */
        g_free(line);
        incoming_free(c);
        return;
    }
    c->line++;
    if (c->line == 1 && !same_secret(line, token)) {
        g_free(line);
        incoming_free(c);
        return;
    }
    if (c->line == 2) {
        c->kind = g_strcmp0(line, "1") == 0 ? SSH_ASKPASS_CONFIRM
                  : g_strcmp0(line, "2") == 0 ? SSH_ASKPASS_NOTICE : SSH_ASKPASS_SECRET;
    }
    if (c->line < 3) {
        g_free(line);
        g_data_input_stream_read_line_async(c->in, G_PRIORITY_DEFAULT, NULL, on_line, c);
        return;
    }
    gchar* prompt = g_strcompress(line);
    g_free(line);
    SshAskpassRequest* req = g_new0(SshAskpassRequest, 1);
    req->conn = g_object_ref(c->conn);
    SshAskpassKind kind = c->kind;
    incoming_free(c);
    if (kind == SSH_ASKPASS_NOTICE) {
        if (ask_func != NULL) ask_func(NULL, kind, prompt, ask_data);
        ssh_askpass_reply(req, "");
    } else if (ask_func != NULL) {
        ask_func(req, kind, prompt, ask_data);
    } else {
        ssh_askpass_reply(req, NULL);
    }
    g_free(prompt);
}

static gboolean on_incoming(GSocketService* svc, GSocketConnection* conn, GObject* source, gpointer data) {
    (void)svc;
    (void)source;
    (void)data;
    Incoming* c = g_new0(Incoming, 1);
    c->conn = g_object_ref(conn);
    c->in = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(conn)));
    g_data_input_stream_set_newline_type(c->in, G_DATA_STREAM_NEWLINE_TYPE_LF);
    g_data_input_stream_read_line_async(c->in, G_PRIORITY_DEFAULT, NULL, on_line, c);
    return TRUE;
}

/* 128 random bits in hex, from the system's generator when there is one */
static gchar* new_token(void) {
    guint8 bytes[16];
    gboolean ok = FALSE;
#ifdef G_OS_UNIX
    FILE* f = fopen("/dev/urandom", "rb");
    if (f != NULL) {
        ok = fread(bytes, 1, sizeof bytes, f) == sizeof bytes;
        fclose(f);
    }
#endif
    if (!ok) {
        for (gsize i = 0; i < sizeof bytes; i++) bytes[i] = (guint8)g_random_int_range(0, 256);
    }
    GString* s = g_string_new(NULL);
    for (gsize i = 0; i < sizeof bytes; i++) g_string_append_printf(s, "%02x", bytes[i]);
    return g_string_free(s, FALSE);
}

gboolean ssh_askpass_serve(const gchar* program, SshAskpassFunc ask, gpointer data, GError** error) {
    ssh_askpass_stop();
    GSocketService* svc = g_socket_service_new();
    GInetAddress* lo = g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4);
    GSocketAddress* addr = g_inet_socket_address_new(lo, 0);
    GSocketAddress* bound = NULL;
    gboolean ok = g_socket_listener_add_address(G_SOCKET_LISTENER(svc), addr, G_SOCKET_TYPE_STREAM,
                                                G_SOCKET_PROTOCOL_TCP, NULL, &bound, error);
    g_object_unref(addr);
    g_object_unref(lo);
    if (!ok) {
        g_object_unref(svc);
        return FALSE;
    }
    guint16 port = g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(bound));
    g_object_unref(bound);
    service = svc;
    token = new_token();
    ask_func = ask;
    ask_data = data;
    g_signal_connect(svc, "incoming", G_CALLBACK(on_incoming), NULL);
    g_socket_service_start(svc);

    address = g_strdup_printf("%u:%s", (guint)port, token);
    ssh_session_set_env(SSH_ASKPASS_ENV, address);
    ssh_session_set_env("SSH_ASKPASS", program);
    ssh_session_set_env("SSH_ASKPASS_REQUIRE", "force"); /* OpenSSH 8.4+ */
#ifndef G_OS_WIN32
    /* Older OpenSSH runs SSH_ASKPASS only with a DISPLAY (and no terminal) */
    if (g_getenv("DISPLAY") == NULL) {
        ssh_session_set_env("DISPLAY", ":0");
        display_set = TRUE;
    }
#endif
    return TRUE;
}

const gchar* ssh_askpass_address(void) {
    return address;
}

void ssh_askpass_stop(void) {
    if (service != NULL) {
        g_socket_service_stop(service);
        g_socket_listener_close(G_SOCKET_LISTENER(service));
        g_clear_object(&service);
    }
    g_clear_pointer(&token, g_free);
    g_clear_pointer(&address, g_free);
    ask_func = NULL;
    ask_data = NULL;
    ssh_session_set_env(SSH_ASKPASS_ENV, NULL);
    ssh_session_set_env("SSH_ASKPASS", NULL);
    ssh_session_set_env("SSH_ASKPASS_REQUIRE", NULL);
    if (display_set) ssh_session_set_env("DISPLAY", NULL);
    display_set = FALSE;
}

/* ------------------------------------------------------------------------- */
/* The helper                                                                */
/* ------------------------------------------------------------------------- */

gboolean ssh_askpass_request(const gchar* where, SshAskpassKind kind, const gchar* prompt,
                             gchar** answer, GError** error) {
    *answer = NULL;
    const gchar* colon = where != NULL ? strchr(where, ':') : NULL;
    gint64 port = colon != NULL ? g_ascii_strtoll(where, NULL, 10) : 0;
    if (port <= 0 || port > 65535) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "bad %s", SSH_ASKPASS_ENV);
        return FALSE;
    }
    GSocketClient* client = g_socket_client_new();
    g_socket_client_set_enable_proxy(client, FALSE); /* the instance is on this machine */
    GSocketConnection* conn = g_socket_client_connect_to_host(client, "127.0.0.1", (guint16)port, NULL, error);
    g_object_unref(client);
    if (conn == NULL) return FALSE;

    gchar* esc = g_strescape(prompt != NULL ? prompt : "", NULL);
    gchar* msg = g_strdup_printf("%s\n%d\n%s\n", colon + 1, (gint)kind, esc);
    g_free(esc);
    GOutputStream* out = g_io_stream_get_output_stream(G_IO_STREAM(conn));
    gboolean ok = g_output_stream_write_all(out, msg, strlen(msg), NULL, NULL, error);
    g_free(msg);
    gchar* line = NULL;
    if (ok) {
        GDataInputStream* in = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(conn)));
        g_data_input_stream_set_newline_type(in, G_DATA_STREAM_NEWLINE_TYPE_LF);
        line = g_data_input_stream_read_line(in, NULL, NULL, error);
        g_object_unref(in);
    }
    g_io_stream_close(G_IO_STREAM(conn), NULL, NULL);
    g_object_unref(conn);
    if (line == NULL) {
        if (ok && error != NULL && *error == NULL) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_CLOSED, "no answer");
        }
        return FALSE;
    }
    ok = g_str_has_prefix(line, "OK ");
    if (ok) *answer = g_strcompress(line + 3);
    memset(line, 0, strlen(line));
    g_free(line);
    if (!ok) g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_CANCELLED, "cancelled");
    return ok;
}

gboolean ssh_askpass_is_helper(int argc, char** argv) {
    (void)argv;
    const gchar* where = g_getenv(SSH_ASKPASS_ENV);
    return argc == 2 && where != NULL && where[0] != '\0';
}

int ssh_askpass_helper_main(int argc, char** argv) {
    const gchar* mode = g_getenv("SSH_ASKPASS_PROMPT");
    SshAskpassKind kind = g_strcmp0(mode, "confirm") == 0 ? SSH_ASKPASS_CONFIRM
                          : g_strcmp0(mode, "none") == 0  ? SSH_ASKPASS_NOTICE : SSH_ASKPASS_SECRET;
    gchar* answer = NULL;
    GError* error = NULL;
    gboolean ok = ssh_askpass_request(g_getenv(SSH_ASKPASS_ENV), kind, argc > 1 ? argv[1] : "", &answer, &error);
    if (!ok) {
        if (error != NULL && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            fprintf(stderr, "lookinfiles askpass: %s\n", error->message);
        }
        g_clear_error(&error);
        return 1;
    }
    fputs(answer, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    memset(answer, 0, strlen(answer));
    g_free(answer);
    return 0;
}
