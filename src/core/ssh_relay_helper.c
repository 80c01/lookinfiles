/* Relay helper process (`lookinfiles --ssh-channel <port> <id> [command...]`,
 * see ssh_relay.h). Runs in its own short-lived process before GTK starts,
 * reads and deletes the ticket file, connects to the running instance over
 * loopback TCP, and copies stdio <-> framed SSH channel traffic. Writes no log. */
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h> /* before any header that may pull windows.h */
#include <windows.h>
#endif
#include "core/ssh_relay.h"

#ifdef HAVE_LIBSSH

#include "core/app_paths.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef G_OS_WIN32
#include <fcntl.h>
#include <io.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
#else
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#define RELAY_PROTO_LINE "LOOKIN-RELAY 1"
#define RELAY_MAX_FRAME  32768u

static void wipe_string(gchar* s) {
    if (s == NULL) return;
    volatile gchar* p = s;
    while (*p != '\0') *p++ = '\0';
    g_free(s);
}

static void put_be16(guint8* dst, guint16 v) {
    dst[0] = (guint8)((v >> 8) & 0xffu);
    dst[1] = (guint8)(v & 0xffu);
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

static gboolean valid_ticket_id(const gchar* id) {
    if (id == NULL || id[0] == '\0') return FALSE;
    for (const gchar* p = id; *p != '\0'; p++) {
        if (!g_ascii_isdigit(*p) && *p != '-') return FALSE;
    }
    return TRUE;
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

static gboolean helper_get_winsize(gint* out_rows, gint* out_cols) {
    *out_rows = 0;
    *out_cols = 0;
#ifdef G_OS_WIN32
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (hin == INVALID_HANDLE_VALUE || !GetConsoleMode(hin, &mode)) return FALSE;
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (hout != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(hout, &info)) {
        *out_cols = (gint)(info.srWindow.Right - info.srWindow.Left + 1);
        *out_rows = (gint)(info.srWindow.Bottom - info.srWindow.Top + 1);
    }
    return TRUE;
#else
    if (!isatty(STDIN_FILENO)) return FALSE;
    struct winsize ws;
    memset(&ws, 0, sizeof ws);
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        *out_rows = (gint)ws.ws_row;
        *out_cols = (gint)ws.ws_col;
    }
    return TRUE;
#endif
}

typedef struct {
    GOutputStream* sock_out;
    GMutex write_mu;
    gint stop; /* atomic */
    gboolean use_tty;
#ifdef G_OS_WIN32
    GMutex win_mu;
    HANDLE stdin_thread_handle;
    HANDLE stdin_done_event;
#else
    int stop_pipe[2];
#endif
} HelperIo;

static gboolean helper_send_frame(HelperIo* io, guint8 type, const void* data, guint32 len) {
    guint8 hdr[5];
    hdr[0] = type;
    put_be32(hdr + 1, len);
    g_mutex_lock(&io->write_mu);
    gboolean ok = g_output_stream_write_all(io->sock_out, hdr, sizeof hdr, NULL, NULL, NULL);
    if (ok && len > 0) ok = g_output_stream_write_all(io->sock_out, data, len, NULL, NULL, NULL);
    g_mutex_unlock(&io->write_mu);
    return ok;
}

#ifndef G_OS_WIN32
static int g_winch_write_fd = -1;

static void on_sigwinch(int sig) {
    (void)sig;
    if (g_winch_write_fd >= 0) {
        char b = 1;
        ssize_t r = write(g_winch_write_fd, &b, 1);
        (void)r;
    }
}

static gpointer helper_stdin_thread(gpointer data) {
    HelperIo* io = data;
    guint8 buf[RELAY_MAX_FRAME];
    int winch_pipe[2] = { -1, -1 };
    if (io->use_tty && pipe(winch_pipe) == 0) {
        int fl = fcntl(winch_pipe[1], F_GETFL, 0);
        if (fl >= 0) fcntl(winch_pipe[1], F_SETFL, fl | O_NONBLOCK);
        g_winch_write_fd = winch_pipe[1];
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = on_sigwinch;
        sa.sa_flags = SA_RESTART;
        sigaction(SIGWINCH, &sa, NULL);
    }

    for (;;) {
        if (g_atomic_int_get(&io->stop)) break;
        struct pollfd pfds[3];
        nfds_t nfd = 2;
        pfds[0].fd = STDIN_FILENO;
        pfds[0].events = POLLIN;
        pfds[1].fd = io->stop_pipe[0];
        pfds[1].events = POLLIN;
        if (winch_pipe[0] >= 0) {
            pfds[2].fd = winch_pipe[0];
            pfds[2].events = POLLIN;
            nfd = 3;
        }
        int pr = poll(pfds, nfd, -1);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pfds[1].revents != 0) break;
        if (nfd == 3 && (pfds[2].revents & POLLIN)) {
            char drain[32];
            ssize_t r = read(winch_pipe[0], drain, sizeof drain);
            (void)r;
            gint rows = 0, cols = 0;
            if (helper_get_winsize(&rows, &cols) && rows > 0 && cols > 0) {
                guint8 payload[4];
                put_be16(payload, (guint16)rows);
                put_be16(payload + 2, (guint16)cols);
                if (!helper_send_frame(io, 'W', payload, 4)) break;
            }
        }
        if (pfds[0].revents & POLLNVAL) {
            helper_send_frame(io, 'E', NULL, 0);
            break;
        }
        if (pfds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
            if (n > 0) {
                if (!helper_send_frame(io, 'I', buf, (guint32)n)) break;
            } else if (n == 0) {
                helper_send_frame(io, 'E', NULL, 0);
                break;
            } else if (errno != EINTR) {
                helper_send_frame(io, 'E', NULL, 0);
                break;
            }
        }
    }

    if (winch_pipe[0] >= 0) {
        signal(SIGWINCH, SIG_DFL);
        g_winch_write_fd = -1;
        close(winch_pipe[0]);
        close(winch_pipe[1]);
    }
    return NULL;
}
#else
static gpointer helper_win_resize_thread(gpointer data) {
    HelperIo* io = data;
    gint last_rows = 0, last_cols = 0;
    helper_get_winsize(&last_rows, &last_cols);
    while (!g_atomic_int_get(&io->stop)) {
        Sleep(250);
        if (g_atomic_int_get(&io->stop)) break;
        gint rows = 0, cols = 0;
        if (helper_get_winsize(&rows, &cols) && rows > 0 && cols > 0 &&
            (rows != last_rows || cols != last_cols)) {
            last_rows = rows;
            last_cols = cols;
            guint8 payload[4];
            put_be16(payload, (guint16)rows);
            put_be16(payload + 2, (guint16)cols);
            if (!helper_send_frame(io, 'W', payload, 4)) break;
        }
    }
    return NULL;
}

static gpointer helper_stdin_thread(gpointer data) {
    HelperIo* io = data;
    HANDLE dup = NULL;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                    &dup, THREAD_TERMINATE, FALSE, 0);
    g_mutex_lock(&io->win_mu);
    io->stdin_thread_handle = dup;
    g_mutex_unlock(&io->win_mu);

    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    guint8 buf[RELAY_MAX_FRAME];
    while (!g_atomic_int_get(&io->stop)) {
        DWORD nread = 0;
        BOOL ok = ReadFile(hin, buf, sizeof buf, &nread, NULL);
        if (g_atomic_int_get(&io->stop)) break;
        if (ok && nread > 0) {
            if (!helper_send_frame(io, 'I', buf, (guint32)nread)) break;
        } else {
            helper_send_frame(io, 'E', NULL, 0);
            break;
        }
    }

    g_mutex_lock(&io->win_mu);
    HANDLE h = io->stdin_thread_handle;
    io->stdin_thread_handle = NULL;
    g_mutex_unlock(&io->win_mu);
    if (io->stdin_done_event != NULL) SetEvent(io->stdin_done_event);
    if (h != NULL) CloseHandle(h);
    return NULL;
}
#endif

static gchar* read_and_delete_ticket(const gchar* id) {
    if (!valid_ticket_id(id)) return NULL;
    gchar* runtime = app_runtime_dir();
    if (runtime == NULL) return NULL;
    gchar* path = g_build_filename(runtime, "relay", id, NULL);
    g_free(runtime);
    gchar* data = NULL;
    gsize len = 0;
    gboolean ok = g_file_get_contents(path, &data, &len, NULL);
    g_remove(path); /* delete at once, even if read failed */
    g_free(path);
    if (!ok || data == NULL) {
        wipe_string(data);
        return NULL;
    }
    g_strstrip(data);
    if (data[0] == '\0') {
        wipe_string(data);
        return NULL;
    }
    return data;
}

gboolean ssh_relay_is_helper(int argc, char** argv) {
    return argc >= 4 && argv != NULL && g_strcmp0(argv[1], "--ssh-channel") == 0;
}

static int helper_run(int argc, char** argv);

int ssh_relay_helper_main(int argc, char** argv) {
#ifdef G_OS_WIN32
    /* main()'s argv is in the ANSI code page: an accent in the command (a
     * folder name) would reach the server as invalid UTF-8. The wide
     * command line, as UTF-8, is what ssh.exe gets too. */
    gchar** wargv = g_win32_get_command_line();
    int rc = helper_run((int)g_strv_length(wargv), wargv);
    g_strfreev(wargv);
    (void)argc;
    (void)argv;
    return rc;
#else
    return helper_run(argc, argv);
#endif
}

static int helper_run(int argc, char** argv) {
    if (!ssh_relay_is_helper(argc, argv)) {
        fprintf(stderr, "lookinfiles relay: bad arguments\n");
        return 255;
    }
#ifndef G_OS_WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    gchar* end = NULL;
    gint64 port = g_ascii_strtoll(argv[2], &end, 10);
    if (end == NULL || *end != '\0' || port <= 0 || port > 65535) {
        fprintf(stderr, "lookinfiles relay: bad port\n");
        return 255;
    }
    gchar* ticket = read_and_delete_ticket(argv[3]);
    if (ticket == NULL) {
        fprintf(stderr, "lookinfiles relay: missing ticket\n");
        return 255;
    }

    /* Join command words after <id> with spaces (none: empty line -> login shell) */
    GString* cmd = g_string_new(NULL);
    for (int i = 4; i < argc; i++) {
        if (i > 4) g_string_append_c(cmd, ' ');
        g_string_append(cmd, argv[i]);
    }
    gchar* esc_cmd = g_strescape(cmd->str, NULL);
    g_string_free(cmd, TRUE);

    gint rows = 0, cols = 0;
    helper_get_winsize(&rows, &cols);

    GSocketClient* client = g_socket_client_new();
    g_socket_client_set_enable_proxy(client, FALSE);
    GError* err = NULL;
    GSocketConnection* conn = g_socket_client_connect_to_host(client, "127.0.0.1", (guint16)port, NULL, &err);
    g_object_unref(client);
    if (conn == NULL) {
        fprintf(stderr, "lookinfiles relay: %s\n", err != NULL ? err->message : "connection failed");
        g_clear_error(&err);
        wipe_string(ticket);
        g_free(esc_cmd);
        return 255;
    }
    GSocket* sock = g_socket_connection_get_socket(conn);
    if (sock != NULL) g_socket_set_option(sock, IPPROTO_TCP, TCP_NODELAY, 1, NULL);

    gchar* hs = g_strdup_printf("%s\n%s\n%d %d\n%s\n", RELAY_PROTO_LINE, ticket, rows, cols, esc_cmd);
    wipe_string(ticket);
    g_free(esc_cmd);

    GOutputStream* sock_out = g_io_stream_get_output_stream(G_IO_STREAM(conn));
    gboolean sent = g_output_stream_write_all(sock_out, hs, strlen(hs), NULL, NULL, NULL);
    wipe_string(hs);
    if (!sent) {
        fprintf(stderr, "lookinfiles relay: connection lost\n");
        g_io_stream_close(G_IO_STREAM(conn), NULL, NULL);
        g_object_unref(conn);
        return 255;
    }

    /* Keep this GDataInputStream for the binary frames so buffered bytes are never lost */
    GDataInputStream* in = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(conn)));
    g_data_input_stream_set_newline_type(in, G_DATA_STREAM_NEWLINE_TYPE_LF);
    gchar* reply = g_data_input_stream_read_line(in, NULL, NULL, NULL);
    if (reply == NULL) {
        fprintf(stderr, "lookinfiles relay: connection lost\n");
        g_object_unref(in);
        g_io_stream_close(G_IO_STREAM(conn), NULL, NULL);
        g_object_unref(conn);
        return 255;
    }
    gboolean use_tty = FALSE;
    if (strcmp(reply, "OK") == 0) {
        use_tty = FALSE;
    } else if (strcmp(reply, "OK tty") == 0) {
        use_tty = TRUE;
    } else {
        const gchar* msg = g_str_has_prefix(reply, "ERR ") ? reply + 4 : reply;
        fprintf(stderr, "lookinfiles relay: %s\n", msg);
        g_free(reply);
        g_object_unref(in);
        g_io_stream_close(G_IO_STREAM(conn), NULL, NULL);
        g_object_unref(conn);
        return 255;
    }
    g_free(reply);

    /* Configure TTY raw mode or Windows binary stdio */
#ifdef G_OS_WIN32
    DWORD saved_mode = 0;
    gboolean restore_console = FALSE;
    if (use_tty) {
        HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
        if (hin != INVALID_HANDLE_VALUE && GetConsoleMode(hin, &saved_mode)) {
            DWORD raw = (saved_mode | ENABLE_VIRTUAL_TERMINAL_INPUT) &
                        ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
            if (SetConsoleMode(hin, raw)) restore_console = TRUE;
        }
    } else {
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
        _setmode(_fileno(stderr), _O_BINARY);
    }
#else
    struct termios saved_termios;
    gboolean restore_termios = FALSE;
    if (use_tty && isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &saved_termios) == 0) {
        struct termios raw = saved_termios;
        cfmakeraw(&raw);
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) restore_termios = TRUE;
    }
#endif

    HelperIo io;
    memset(&io, 0, sizeof io);
    io.sock_out = sock_out;
    io.use_tty = use_tty;
    g_mutex_init(&io.write_mu);
#ifdef G_OS_WIN32
    g_mutex_init(&io.win_mu);
    io.stdin_done_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    GThread* resize_th = use_tty ? g_thread_new("relay-resize", helper_win_resize_thread, &io) : NULL;
#else
    io.stop_pipe[0] = io.stop_pipe[1] = -1;
    if (pipe(io.stop_pipe) != 0) io.stop_pipe[0] = io.stop_pipe[1] = -1;
#endif
    GThread* stdin_th = g_thread_new("relay-stdin", helper_stdin_thread, &io);

    /* Main thread: socket frames -> stdout / stderr / exit status */
    gint exit_code = 255;
    gboolean got_exit = FALSE;
    guint8 hdr[5];
    guint8 buf[RELAY_MAX_FRAME];
    for (;;) {
        gsize got = 0;
        if (!g_input_stream_read_all(G_INPUT_STREAM(in), hdr, sizeof hdr, &got, NULL, NULL) || got < sizeof hdr) {
            break;
        }
        guint8 type = hdr[0];
        guint32 len = get_be32(hdr + 1);
        if (len > RELAY_MAX_FRAME) break;
        if (len > 0) {
            gsize body_got = 0;
            if (!g_input_stream_read_all(G_INPUT_STREAM(in), buf, len, &body_got, NULL, NULL) || body_got < len) {
                break;
            }
        }
        if (type == 'O') {
            if (len > 0 && !write_fd_all(1, buf, len)) break;
        } else if (type == 'R') {
            if (len > 0 && !write_fd_all(2, buf, len)) break;
        } else if (type == 'X' && len == 4) {
            gint32 st = (gint32)get_be32(buf);
            exit_code = st < 0 ? 255 : (gint)st;
            got_exit = TRUE;
            break;
        } else {
            break;
        }
    }

    /* Stop stdin thread cleanly */
    g_atomic_int_set(&io.stop, 1);
#ifdef G_OS_WIN32
    gboolean stdin_done = io.stdin_done_event == NULL;
    for (int tries = 0; tries < 50 && !stdin_done; tries++) {
        g_mutex_lock(&io.win_mu);
        if (io.stdin_thread_handle != NULL) CancelSynchronousIo(io.stdin_thread_handle);
        g_mutex_unlock(&io.win_mu);
        stdin_done = WaitForSingleObject(io.stdin_done_event, 10) == WAIT_OBJECT_0;
    }
    if (resize_th != NULL) g_thread_join(resize_th);
    if (restore_console) SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), saved_mode);
    if (!stdin_done) {
        /* Its ReadFile() could not be cancelled: never wait for it (the
         * kernel or language server would seem to hang); the process ends
         * with its exit status, which ends the thread too. io stays valid:
         * nothing below is freed before ExitProcess(). */
        fflush(NULL);
        if (!got_exit) fprintf(stderr, "lookinfiles relay: connection lost\n");
        ExitProcess((UINT)exit_code);
    }
    g_thread_join(stdin_th);
    if (io.stdin_done_event != NULL) CloseHandle(io.stdin_done_event);
    g_mutex_clear(&io.win_mu);
#else
    if (io.stop_pipe[1] >= 0) {
        char b = 1;
        ssize_t r = write(io.stop_pipe[1], &b, 1);
        (void)r;
    }
    g_thread_join(stdin_th);
    if (io.stop_pipe[0] >= 0) close(io.stop_pipe[0]);
    if (io.stop_pipe[1] >= 0) close(io.stop_pipe[1]);
    if (restore_termios) tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
#endif
    g_mutex_clear(&io.write_mu);

    g_object_unref(in);
    g_io_stream_close(G_IO_STREAM(conn), NULL, NULL);
    g_object_unref(conn);

    if (!got_exit) fprintf(stderr, "lookinfiles relay: connection lost\n");
    return exit_code;
}

#endif /* HAVE_LIBSSH */
