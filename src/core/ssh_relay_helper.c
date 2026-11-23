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
#include "core/ssh_trace.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef G_OS_WIN32
#include <fcntl.h>
#include <io.h>
#ifndef ENABLE_PROCESSED_OUTPUT
#define ENABLE_PROCESSED_OUTPUT 0x0001
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#endif
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

#ifdef G_OS_WIN32
/* The handle of C file descriptor fd (0, 1, 2), else the standard handle
 * which: GLib passes a child's pipes as C descriptors, so in a GUI program
 * such as this one GetStdHandle() alone may give none (stdin then seemed
 * to end at once: the remote command got EOF before its first line) */
static HANDLE std_handle(int fd, DWORD which) {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == NULL || h == INVALID_HANDLE_VALUE || h == (HANDLE)(intptr_t)-2) h = GetStdHandle(which);
    return h;
}

static gboolean write_fd_all(int fd, const void* buf, gsize len) {
    HANDLE h = std_handle(fd, fd == 2 ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (h == NULL || h == INVALID_HANDLE_VALUE) return FALSE;
    const guint8* p = buf;
    gsize rem = len;
    while (rem > 0) {
        DWORD chunk = (DWORD)MIN(rem, (gsize)RELAY_MAX_FRAME);
        DWORD w = 0;
        if (!WriteFile(h, p, chunk, &w, NULL) || w == 0) return FALSE;
        p += (gsize)w;
        rem -= (gsize)w;
    }
    return TRUE;
}

typedef struct {
    gboolean has_in_mode;
    DWORD saved_in_mode;
    gboolean has_out_mode;
    DWORD saved_out_mode;
    gboolean has_in_cp;
    UINT saved_in_cp;
    gboolean has_out_cp;
    UINT saved_out_cp;
    gboolean ctrl_ignored;
    int saved_stdin_mode;
    int saved_stdout_mode;
    int saved_stderr_mode;
} WinConsoleState;

static void win_console_setup(WinConsoleState* wcs, gboolean use_tty) {
    memset(wcs, 0, sizeof *wcs);
    /* Binary mode always (both pipes and ConPTY): text mode translates \n to
     * \r\n and stops reading at ^Z. */
    wcs->saved_stdin_mode = _setmode(_fileno(stdin), _O_BINARY);
    wcs->saved_stdout_mode = _setmode(_fileno(stdout), _O_BINARY);
    wcs->saved_stderr_mode = _setmode(_fileno(stderr), _O_BINARY);
    if (!use_tty) return;

    HANDLE hin = std_handle(0, STD_INPUT_HANDLE);
    if (hin != NULL && hin != INVALID_HANDLE_VALUE && GetConsoleMode(hin, &wcs->saved_in_mode)) {
        DWORD raw = (wcs->saved_in_mode | ENABLE_VIRTUAL_TERMINAL_INPUT) &
                    ~(DWORD)(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
        if (SetConsoleMode(hin, raw)) wcs->has_in_mode = TRUE;
        ssh_trace("relay helper: win console in 0x%lx -> 0x%lx (ok=%d)",
                  (unsigned long)wcs->saved_in_mode, (unsigned long)raw, wcs->has_in_mode ? 1 : 0);
    } else {
        ssh_trace("relay helper: win tty stdin has no console (err=%lu)", (unsigned long)GetLastError());
    }

    HANDLE hout = std_handle(1, STD_OUTPUT_HANDLE);
    if (hout != NULL && hout != INVALID_HANDLE_VALUE && GetConsoleMode(hout, &wcs->saved_out_mode)) {
        DWORD vt = wcs->saved_out_mode | ENABLE_PROCESSED_OUTPUT |
                   ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
        if (SetConsoleMode(hout, vt)) {
            wcs->has_out_mode = TRUE;
        } else {
            vt = wcs->saved_out_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            if (SetConsoleMode(hout, vt)) wcs->has_out_mode = TRUE;
        }
        ssh_trace("relay helper: win console out 0x%lx -> 0x%lx (ok=%d)",
                  (unsigned long)wcs->saved_out_mode, (unsigned long)vt, wcs->has_out_mode ? 1 : 0);
    } else {
        ssh_trace("relay helper: win tty stdout has no console (err=%lu)", (unsigned long)GetLastError());
    }

    UINT in_cp = GetConsoleCP();
    if (in_cp != 0 && SetConsoleCP(CP_UTF8)) {
        wcs->saved_in_cp = in_cp;
        wcs->has_in_cp = TRUE;
    }
    UINT out_cp = GetConsoleOutputCP();
    if (out_cp != 0 && SetConsoleOutputCP(CP_UTF8)) {
        wcs->saved_out_cp = out_cp;
        wcs->has_out_cp = TRUE;
    }
    ssh_trace("relay helper: win console cp in=%u->%u out=%u->%u",
              (unsigned int)in_cp, (unsigned int)GetConsoleCP(),
              (unsigned int)out_cp, (unsigned int)GetConsoleOutputCP());

    if (wcs->has_in_mode && SetConsoleCtrlHandler(NULL, TRUE)) {
        wcs->ctrl_ignored = TRUE;
    }
}

static void win_console_restore(const WinConsoleState* wcs) {
    if (wcs->ctrl_ignored) SetConsoleCtrlHandler(NULL, FALSE);
    if (wcs->has_in_cp) SetConsoleCP(wcs->saved_in_cp);
    if (wcs->has_out_cp) SetConsoleOutputCP(wcs->saved_out_cp);
    if (wcs->has_in_mode) {
        HANDLE hin = std_handle(0, STD_INPUT_HANDLE);
        if (hin != NULL && hin != INVALID_HANDLE_VALUE) SetConsoleMode(hin, wcs->saved_in_mode);
    }
    if (wcs->has_out_mode) {
        HANDLE hout = std_handle(1, STD_OUTPUT_HANDLE);
        if (hout != NULL && hout != INVALID_HANDLE_VALUE) SetConsoleMode(hout, wcs->saved_out_mode);
    }
    if (wcs->saved_stdin_mode != -1) _setmode(_fileno(stdin), wcs->saved_stdin_mode);
    if (wcs->saved_stdout_mode != -1) _setmode(_fileno(stdout), wcs->saved_stdout_mode);
    if (wcs->saved_stderr_mode != -1) _setmode(_fileno(stderr), wcs->saved_stderr_mode);
}
#else
static gboolean write_fd_all(int fd, const void* buf, gsize len) {
    const guint8* p = buf;
    gsize rem = len;
    while (rem > 0) {
        ssize_t w = write(fd, p, rem);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return FALSE;
        p += (gsize)w;
        rem -= (gsize)w;
    }
    return TRUE;
}
#endif

static gboolean helper_get_winsize(gint* out_rows, gint* out_cols) {
    *out_rows = 0;
    *out_cols = 0;
#ifdef G_OS_WIN32
    HANDLE hin = std_handle(0, STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (hin == NULL || hin == INVALID_HANDLE_VALUE || !GetConsoleMode(hin, &mode)) return FALSE;
    HANDLE hout = std_handle(1, STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (hout != NULL && hout != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(hout, &info) &&
        info.srWindow.Right >= info.srWindow.Left && info.srWindow.Bottom >= info.srWindow.Top) {
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
    gint init_rows;
    gint init_cols;
#ifdef G_OS_WIN32
    GMutex win_mu;
    HANDLE stdin_thread_handle;
    HANDLE stdin_done_event;
    HANDLE stop_event;
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
        /* Catch a resize that happened during the handshake before SIGWINCH was installed */
        gint cur_rows = 0, cur_cols = 0;
        if (helper_get_winsize(&cur_rows, &cur_cols) && cur_rows > 0 && cur_cols > 0 &&
            (cur_rows != io->init_rows || cur_cols != io->init_cols)) {
            ssh_trace("relay helper: resize %dx%d", cur_cols, cur_rows);
            guint8 payload[4];
            put_be16(payload, (guint16)cur_rows);
            put_be16(payload + 2, (guint16)cur_cols);
            helper_send_frame(io, 'W', payload, 4);
        }
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
                ssh_trace("relay helper: resize %dx%d", cols, rows);
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
    gint last_rows = io->init_rows;
    gint last_cols = io->init_cols;
    gboolean first = TRUE;
    while (!g_atomic_int_get(&io->stop)) {
        if (!first) {
            if (io->stop_event != NULL) {
                if (WaitForSingleObject(io->stop_event, 250) == WAIT_OBJECT_0) break;
            } else {
                Sleep(250);
            }
            if (g_atomic_int_get(&io->stop)) break;
        }
        first = FALSE;
        gint rows = 0, cols = 0;
        if (helper_get_winsize(&rows, &cols) && rows > 0 && cols > 0 &&
            (rows != last_rows || cols != last_cols)) {
            last_rows = rows;
            last_cols = cols;
            ssh_trace("relay helper: resize %dx%d", cols, rows);
            guint8 payload[4];
            put_be16(payload, (guint16)rows);
            put_be16(payload + 2, (guint16)cols);
            if (!helper_send_frame(io, 'W', payload, 4)) break;
        }
    }
    return NULL;
}

#define CONSOLE_UNITS 4096 /* UTF-16 units per console read: at most 3 UTF-8 bytes each */

/* Read the console as UTF-16 and give UTF-8 in out (CONSOLE_UNITS * 3
 * bytes at least): ReadFile() in the UTF-8 code page gives NUL bytes for
 * non-ASCII keys on many Windows versions (an accent typed in the terminal
 * would be lost). A high surrogate at the end waits in *pending for its
 * pair. The byte count (0: nothing yet), -1 on error. */
static gssize read_console_utf8(HANDLE h, gunichar2* pending, guint8* out) {
    gunichar2 w[CONSOLE_UNITS + 1];
    gsize start = 0;
    if (*pending != 0) {
        w[0] = *pending;
        start = 1;
        *pending = 0;
    }
    DWORD n = 0;
    if (!ReadConsoleW(h, w + start, CONSOLE_UNITS, &n, NULL)) return -1;
    gsize units = start + n;
    if (units > 0 && w[units - 1] >= 0xD800 && w[units - 1] <= 0xDBFF) {
        *pending = w[units - 1];
        units--;
    }
    gsize len = 0;
    for (gsize i = 0; i < units; i++) {
        gunichar c = w[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < units && w[i + 1] >= 0xDC00 && w[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (w[i + 1] - 0xDC00);
            i++;
        } else if (c >= 0xD800 && c <= 0xDFFF) {
            c = 0xFFFD; /* a lone surrogate */
        }
        len += (gsize)g_unichar_to_utf8(c, (gchar*)out + len);
    }
    return (gssize)len;
}

static gpointer helper_stdin_thread(gpointer data) {
    HelperIo* io = data;
    HANDLE dup = NULL;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                    &dup, THREAD_TERMINATE, FALSE, 0);
    g_mutex_lock(&io->win_mu);
    io->stdin_thread_handle = dup;
    g_mutex_unlock(&io->win_mu);

    HANDLE hin = std_handle(0, STD_INPUT_HANDLE);
    DWORD cmode = 0;
    gboolean is_console = (hin != NULL && hin != INVALID_HANDLE_VALUE && GetConsoleMode(hin, &cmode));
    guint8 buf[RELAY_MAX_FRAME];
    G_STATIC_ASSERT(CONSOLE_UNITS * 4 <= RELAY_MAX_FRAME); /* a surrogate pair: 2 units, 4 bytes */
    gunichar2 pending = 0;
    guint64 total = 0;
    while (!g_atomic_int_get(&io->stop)) {
        DWORD nread = 0;
        BOOL ok;
        if (is_console) {
            gssize got = read_console_utf8(hin, &pending, buf);
            ok = got >= 0;
            nread = got > 0 ? (DWORD)got : 0;
        } else {
            ok = ReadFile(hin, buf, sizeof buf, &nread, NULL);
        }
        if (g_atomic_int_get(&io->stop)) break;
        if (ok && nread > 0) {
            total += nread;
            if (!helper_send_frame(io, 'I', buf, (guint32)nread)) break;
        } else if (ok && nread == 0 && is_console) {
            /* A console read can return 0 bytes on non-character events; never EOF */
            continue;
        } else {
            /* 109 (ERROR_BROKEN_PIPE): the normal end of a pipe */
            ssh_trace("relay helper: stdin ended after %" G_GUINT64_FORMAT " bytes (error %lu)", total,
                      ok ? 0UL : (unsigned long)GetLastError());
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
    ssh_trace_setup(); /* LOOKINFILES_SSH_LOG, as the instance */
    gchar* ticket = read_and_delete_ticket(argv[3]);
    if (ticket == NULL) {
        ssh_trace("relay helper: missing ticket id=%s", argv[3]);
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
        ssh_trace("relay helper: connect failed id=%s port=%u: %s", argv[3], (guint)port,
                  err != NULL ? err->message : "connection failed");
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
        ssh_trace("relay helper: handshake write failed id=%s", argv[3]);
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
        ssh_trace("relay helper: handshake reply lost id=%s", argv[3]);
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
        ssh_trace("relay helper: refused id=%s: %s", argv[3], msg);
        fprintf(stderr, "lookinfiles relay: %s\n", msg);
        g_free(reply);
        g_object_unref(in);
        g_io_stream_close(G_IO_STREAM(conn), NULL, NULL);
        g_object_unref(conn);
        return 255;
    }
    g_free(reply);
    ssh_trace("relay helper: connected id=%s tty=%d size=%dx%d", argv[3], use_tty ? 1 : 0, cols, rows);

    /* Configure TTY raw mode or Windows binary stdio */
#ifdef G_OS_WIN32
    WinConsoleState wcs;
    win_console_setup(&wcs, use_tty);
#else
    struct termios saved_termios;
    gboolean restore_termios = FALSE;
    if (use_tty && isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &saved_termios) == 0) {
        struct termios raw = saved_termios;
        cfmakeraw(&raw);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) restore_termios = TRUE;
    }
#endif

    HelperIo io;
    memset(&io, 0, sizeof io);
    io.sock_out = sock_out;
    io.use_tty = use_tty;
    io.init_rows = rows;
    io.init_cols = cols;
    g_mutex_init(&io.write_mu);
#ifdef G_OS_WIN32
    g_mutex_init(&io.win_mu);
    io.stdin_done_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    io.stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    GThread* resize_th = use_tty ? g_thread_new("relay-resize", helper_win_resize_thread, &io) : NULL;
#else
    io.stop_pipe[0] = io.stop_pipe[1] = -1;
    if (pipe(io.stop_pipe) != 0) io.stop_pipe[0] = io.stop_pipe[1] = -1;
#endif
    GThread* stdin_th = g_thread_new("relay-stdin", helper_stdin_thread, &io);

    /* Main thread: socket frames -> stdout / stderr / exit status */
    gint exit_code = 255;
    gboolean got_exit = FALSE;
    guint64 out_total = 0;
    guint64 err_total = 0;
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
            out_total += len;
            if (len > 0 && !write_fd_all(1, buf, len)) {
#ifdef G_OS_WIN32
                ssh_trace("relay helper: cannot write stdout (err=%lu)", (unsigned long)GetLastError());
#else
                ssh_trace("relay helper: cannot write stdout");
#endif
                break;
            }
        } else if (type == 'R') {
            err_total += len;
            if (len > 0 && !write_fd_all(2, buf, len)) {
#ifdef G_OS_WIN32
                ssh_trace("relay helper: cannot write stderr (err=%lu)", (unsigned long)GetLastError());
#else
                ssh_trace("relay helper: cannot write stderr");
#endif
                break;
            }
        } else if (type == 'X' && len == 4) {
            gint32 st = (gint32)get_be32(buf);
            exit_code = st < 0 ? 255 : (gint)st;
            got_exit = TRUE;
            break;
        } else {
            break;
        }
    }

    ssh_trace("relay helper: end, stdout %" G_GUINT64_FORMAT " bytes, stderr %" G_GUINT64_FORMAT
              " bytes, exit %d%s",
              out_total, err_total, exit_code, got_exit ? "" : " (no exit status)");

    /* Stop stdin thread cleanly */
    g_atomic_int_set(&io.stop, 1);
#ifdef G_OS_WIN32
    if (io.stop_event != NULL) SetEvent(io.stop_event);
    gboolean stdin_done = io.stdin_done_event == NULL;
    for (int tries = 0; tries < 50 && !stdin_done; tries++) {
        g_mutex_lock(&io.win_mu);
        if (io.stdin_thread_handle != NULL) CancelSynchronousIo(io.stdin_thread_handle);
        g_mutex_unlock(&io.win_mu);
        stdin_done = WaitForSingleObject(io.stdin_done_event, 10) == WAIT_OBJECT_0;
    }
    if (resize_th != NULL) g_thread_join(resize_th);
    win_console_restore(&wcs);
    if (!stdin_done) {
        /* Its ReadFile() could not be cancelled: never wait for it (the
         * kernel or language server would seem to hang); the process ends
         * with its exit status, which ends the thread too. io stays valid:
         * nothing below is freed before ExitProcess(). */
        if (!got_exit) fprintf(stderr, "lookinfiles relay: connection lost\n");
        fflush(NULL);
        ExitProcess((UINT)exit_code);
    }
    g_thread_join(stdin_th);
    if (io.stdin_done_event != NULL) CloseHandle(io.stdin_done_event);
    if (io.stop_event != NULL) CloseHandle(io.stop_event);
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
