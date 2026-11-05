/* Pseudo-terminal of the built-in terminal: ConPTY on Windows, POSIX pty elsewhere. */
#ifdef _WIN32
/* CreatePseudoConsole() needs the Windows 10 API */
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#else
/* posix_openpt(), grantpt(), ptsname() with the BSD ioctls (TIOCSCTTY) */
#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#endif

#include "vt_pty.h"

#include <string.h>

#ifdef G_OS_WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <glib-unix.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#ifdef G_OS_WIN32

/* ------------------------------------------------------------------------- */
/* Windows: pseudo console, output read by a thread                          */
/* ------------------------------------------------------------------------- */

struct VtPty {
    gint refs;              /* main thread, reader thread, pending idle */
    HPCON console;
    HANDLE input;           /* we write what the user types here */
    HANDLE output;          /* the reader thread reads the console here */
    GMutex lock;
    GByteArray* pending;    /* read, not yet handed to the main thread */
    gboolean idle_queued;
    gboolean closed;        /* main thread only */
    VtPtyOutputFunc output_func;
    gpointer user_data;
};

static void pty_unref(VtPty* pty) {
    if (!g_atomic_int_dec_and_test(&pty->refs)) {
        return;
    }
    CloseHandle(pty->output);
    g_mutex_clear(&pty->lock);
    g_byte_array_unref(pty->pending);
    g_free(pty);
}

static gboolean flush_output(gpointer data) {
    VtPty* pty = data;
    g_mutex_lock(&pty->lock);
    GByteArray* chunk = pty->pending;
    pty->pending = g_byte_array_new();
    pty->idle_queued = FALSE;
    g_mutex_unlock(&pty->lock);
    if (!pty->closed && chunk->len > 0 && pty->output_func != NULL) {
        pty->output_func((const gchar*)chunk->data, chunk->len, pty->user_data);
    }
    g_byte_array_unref(chunk);
    pty_unref(pty);
    return G_SOURCE_REMOVE;
}

static gpointer reader_thread(gpointer data) {
    VtPty* pty = data;
    guint8 buf[16384];
    DWORD n = 0;
    while (ReadFile(pty->output, buf, sizeof buf, &n, NULL) && n > 0) {
        g_mutex_lock(&pty->lock);
        g_byte_array_append(pty->pending, buf, n);
        if (!pty->idle_queued) {
            pty->idle_queued = TRUE;
            g_atomic_int_inc(&pty->refs);
            g_idle_add(flush_output, pty);
        }
        g_mutex_unlock(&pty->lock);
    }
    pty_unref(pty);
    return NULL;
}

/* ClosePseudoConsole() waits for the console host to finish (which may wait
 * for its output to be read): off the main thread */
static gpointer closer_thread(gpointer data) {
    VtPty* pty = data;
    ClosePseudoConsole(pty->console);
    CloseHandle(pty->input);
    pty_unref(pty);
    return NULL;
}

/* One argument quoted for CommandLineToArgvW() / the C runtime */
static void append_quoted(GString* cmd, const gchar* arg) {
    if (arg[0] != '\0' && strpbrk(arg, " \t\n\v\"") == NULL) {
        g_string_append(cmd, arg);
        return;
    }
    g_string_append_c(cmd, '"');
    for (const gchar* p = arg;; p++) {
        gsize backslashes = 0;
        while (*p == '\\') {
            backslashes++;
            p++;
        }
        if (*p == '\0') {
            for (gsize i = 0; i < backslashes * 2; i++) {
                g_string_append_c(cmd, '\\');
            }
            break;
        }
        gsize count = *p == '"' ? backslashes * 2 + 1 : backslashes;
        for (gsize i = 0; i < count; i++) {
            g_string_append_c(cmd, '\\');
        }
        g_string_append_c(cmd, *p);
    }
    g_string_append_c(cmd, '"');
}

/* Environment block of envv: "NAME=value" strings, each ended by a NUL,
 * then a final NUL (UTF-16) */
static gunichar2* environment_block(gchar** envv) {
    GString* block = g_string_new(NULL);
    for (gint i = 0; envv[i] != NULL; i++) {
        g_string_append_len(block, envv[i], (gssize)strlen(envv[i]) + 1);
    }
    g_string_append_c(block, '\0');
    glong n = 0;
    gunichar2* out = g_utf8_to_utf16(block->str, (glong)block->len, NULL, &n, NULL);
    g_string_free(block, TRUE);
    return out;
}

VtPty* vt_pty_spawn(gchar** argv, const gchar* cwd, gchar** envv, gboolean search_path,
                    gint cols, gint rows, VtPtyOutputFunc output, gpointer user_data,
                    GPid* pid, GError** error) {
    g_return_val_if_fail(argv != NULL && argv[0] != NULL, NULL);
    gchar* program = search_path ? g_find_program_in_path(argv[0]) : g_strdup(argv[0]);
    if (program == NULL) {
        g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT, "%s: program not found", argv[0]);
        return NULL;
    }

    HANDLE in_read = NULL, in_write = NULL, out_read = NULL, out_write = NULL;
    if (!CreatePipe(&in_read, &in_write, NULL, 0) || !CreatePipe(&out_read, &out_write, NULL, 0)) {
        gchar* msg = g_win32_error_message((gint)GetLastError());
        g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED, "Cannot create pipes: %s", msg);
        g_free(msg);
        if (in_read != NULL) {
            CloseHandle(in_read);
            CloseHandle(in_write);
        }
        g_free(program);
        return NULL;
    }
    COORD size = {(SHORT)MAX(cols, 1), (SHORT)MAX(rows, 1)};
    HPCON console = NULL;
    HRESULT hr = CreatePseudoConsole(size, in_read, out_write, 0, &console);
    /* The console host has its own copies of its ends of the pipes */
    CloseHandle(in_read);
    CloseHandle(out_write);
    if (FAILED(hr)) {
        g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
                    "Cannot create a pseudo console (error 0x%08lx; Windows 10 1809 or later needed)",
                    (unsigned long)hr);
        CloseHandle(in_write);
        CloseHandle(out_read);
        g_free(program);
        return NULL;
    }

    STARTUPINFOEXW si;
    memset(&si, 0, sizeof si);
    si.StartupInfo.cb = sizeof si;
    /* No standard handles of ours: the child uses the pseudo console */
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    si.lpAttributeList = g_malloc(attr_size);
    gboolean ok = InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_size) &&
                  UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                            console, sizeof console, NULL, NULL);

    GString* cmd = g_string_new(NULL);
    for (gint i = 0; argv[i] != NULL; i++) {
        if (i > 0) {
            g_string_append_c(cmd, ' ');
        }
        append_quoted(cmd, argv[i]);
    }
    gunichar2* program_w = g_utf8_to_utf16(program, -1, NULL, NULL, NULL);
    gunichar2* cmd_w = g_utf8_to_utf16(cmd->str, -1, NULL, NULL, NULL);
    gunichar2* cwd_w = (cwd != NULL && g_file_test(cwd, G_FILE_TEST_IS_DIR))
                           ? g_utf8_to_utf16(cwd, -1, NULL, NULL, NULL) : NULL;
    gunichar2* env_w = envv != NULL ? environment_block(envv) : NULL;

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof pi);
    DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;
    ok = ok && program_w != NULL && cmd_w != NULL &&
         CreateProcessW((LPCWSTR)program_w, (LPWSTR)cmd_w, NULL, NULL, FALSE, flags, env_w,
                        (LPCWSTR)cwd_w, &si.StartupInfo, &pi);
    DWORD err = ok ? 0 : GetLastError();
    DeleteProcThreadAttributeList(si.lpAttributeList);
    g_free(si.lpAttributeList);
    g_free(program_w);
    g_free(cmd_w);
    g_free(cwd_w);
    g_free(env_w);
    g_string_free(cmd, TRUE);

    if (!ok) {
        gchar* msg = g_win32_error_message((gint)err);
        g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED, "Cannot run %s: %s", program, msg);
        g_free(msg);
        g_free(program);
        ClosePseudoConsole(console);
        CloseHandle(in_write);
        CloseHandle(out_read);
        return NULL;
    }
    g_free(program);
    CloseHandle(pi.hThread);
    if (pid != NULL) {
        *pid = pi.hProcess;
    } else {
        CloseHandle(pi.hProcess);
    }

    VtPty* pty = g_new0(VtPty, 1);
    pty->refs = 2; /* main thread + reader thread */
    pty->console = console;
    pty->input = in_write;
    pty->output = out_read;
    g_mutex_init(&pty->lock);
    pty->pending = g_byte_array_new();
    pty->output_func = output;
    pty->user_data = user_data;
    g_thread_unref(g_thread_new("conpty-reader", reader_thread, pty));
    return pty;
}

void vt_pty_write(VtPty* pty, const gchar* data, gsize len) {
    while (pty != NULL && !pty->closed && len > 0) {
        DWORD written = 0;
        if (!WriteFile(pty->input, data, (DWORD)MIN(len, 65536), &written, NULL) || written == 0) {
            return;
        }
        data += written;
        len -= written;
    }
}

void vt_pty_resize(VtPty* pty, gint cols, gint rows) {
    if (pty == NULL || pty->closed) {
        return;
    }
    COORD size = {(SHORT)CLAMP(cols, 1, G_MAXSHORT), (SHORT)CLAMP(rows, 1, G_MAXSHORT)};
    ResizePseudoConsole(pty->console, size);
}

void vt_pty_free(VtPty* pty) {
    if (pty == NULL) {
        return;
    }
    pty->closed = TRUE;
    pty->output_func = NULL;
    /* The closer thread takes over the main thread's reference */
    g_thread_unref(g_thread_new("conpty-close", closer_thread, pty));
}

#else /* POSIX */

/* ------------------------------------------------------------------------- */
/* POSIX: pty master read in the main loop                                   */
/* ------------------------------------------------------------------------- */

struct VtPty {
    gint master;
    GPid pid;
    guint read_source;
    guint write_source;
    GByteArray* unsent; /* what the pty did not take yet */
    VtPtyOutputFunc output_func;
    gpointer user_data;
};

static gboolean on_readable(gint fd, GIOCondition cond, gpointer data) {
    VtPty* pty = data;
    (void)cond;
    gchar buf[16384];
    for (gint i = 0; i < 8; i++) {
        ssize_t n = read(fd, buf, sizeof buf);
        if (n > 0) {
            pty->output_func(buf, (gsize)n, pty->user_data);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
            return G_SOURCE_CONTINUE;
        }
        /* EIO: the child and its descendants closed the terminal */
        pty->read_source = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void flush_unsent(VtPty* pty) {
    while (pty->unsent->len > 0) {
        ssize_t n = write(pty->master, pty->unsent->data, pty->unsent->len);
        if (n > 0) {
            g_byte_array_remove_range(pty->unsent, 0, (guint)n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n < 0 && errno == EAGAIN) {
            return;
        } else {
            g_byte_array_set_size(pty->unsent, 0); /* terminal gone */
        }
    }
}

static gboolean on_writable(gint fd, GIOCondition cond, gpointer data) {
    VtPty* pty = data;
    (void)fd;
    (void)cond;
    flush_unsent(pty);
    if (pty->unsent->len > 0) {
        return G_SOURCE_CONTINUE;
    }
    pty->write_source = 0;
    return G_SOURCE_REMOVE;
}

static void set_size(gint fd, gint cols, gint rows) {
    struct winsize ws;
    memset(&ws, 0, sizeof ws);
    ws.ws_col = (unsigned short)CLAMP(cols, 1, G_MAXUSHORT);
    ws.ws_row = (unsigned short)CLAMP(rows, 1, G_MAXUSHORT);
    ioctl(fd, TIOCSWINSZ, &ws);
}

VtPty* vt_pty_spawn(gchar** argv, const gchar* cwd, gchar** envv, gboolean search_path,
                    gint cols, gint rows, VtPtyOutputFunc output, gpointer user_data,
                    GPid* pid, GError** error) {
    g_return_val_if_fail(argv != NULL && argv[0] != NULL, NULL);
    gchar* program = (search_path && strchr(argv[0], '/') == NULL) ? g_find_program_in_path(argv[0])
                                                                    : g_strdup(argv[0]);
    if (program == NULL) {
        g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT, "%s: program not found", argv[0]);
        return NULL;
    }
    gint master = posix_openpt(O_RDWR | O_NOCTTY);
    const gchar* slave_name = master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0
                                  ? ptsname(master) : NULL;
    if (slave_name == NULL) {
        g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED, "Cannot open a pseudo-terminal: %s",
                    g_strerror(errno));
        if (master >= 0) {
            close(master);
        }
        g_free(program);
        return NULL;
    }
    gchar* slave = g_strdup(slave_name);
    set_size(master, cols, rows);
    fcntl(master, F_SETFD, FD_CLOEXEC);

    /* Everything the child needs is prepared before fork() */
    gchar** env = envv != NULL ? g_strdupv(envv) : g_get_environ();
    env = g_environ_setenv(env, "TERM", "xterm-256color", TRUE);
    env = g_environ_setenv(env, "COLORTERM", "truecolor", TRUE);

    pid_t child = fork();
    if (child == 0) {
        setsid();
        gint fd = open(slave, O_RDWR);
        if (fd < 0) {
            _exit(127);
        }
        ioctl(fd, TIOCSCTTY, 0);
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        if (fd > 2) {
            close(fd);
        }
        if (cwd != NULL && chdir(cwd) != 0) {
            /* stays in the current directory */
        }
        signal(SIGPIPE, SIG_DFL);
        execve(program, argv, env);
        _exit(127);
    }
    gint fork_errno = errno;
    g_free(slave);
    g_free(program);
    g_strfreev(env);
    if (child < 0) {
        g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_FORK, "Cannot start %s: %s", argv[0],
                    g_strerror(fork_errno));
        close(master);
        return NULL;
    }
    g_unix_set_fd_nonblocking(master, TRUE, NULL);

    VtPty* pty = g_new0(VtPty, 1);
    pty->master = master;
    pty->pid = child;
    pty->unsent = g_byte_array_new();
    pty->output_func = output;
    pty->user_data = user_data;
    pty->read_source = g_unix_fd_add(master, G_IO_IN | G_IO_HUP | G_IO_ERR, on_readable, pty);
    if (pid != NULL) {
        *pid = child;
    }
    return pty;
}

void vt_pty_write(VtPty* pty, const gchar* data, gsize len) {
    if (pty == NULL || len == 0) {
        return;
    }
    g_byte_array_append(pty->unsent, (const guint8*)data, (guint)len);
    flush_unsent(pty);
    if (pty->unsent->len > 0 && pty->write_source == 0) {
        pty->write_source = g_unix_fd_add(pty->master, G_IO_OUT, on_writable, pty);
    }
}

void vt_pty_resize(VtPty* pty, gint cols, gint rows) {
    if (pty != NULL) {
        set_size(pty->master, cols, rows); /* the kernel sends SIGWINCH */
    }
}

void vt_pty_free(VtPty* pty) {
    if (pty == NULL) {
        return;
    }
    if (pty->read_source != 0) {
        g_source_remove(pty->read_source);
    }
    if (pty->write_source != 0) {
        g_source_remove(pty->write_source);
    }
    kill(pty->pid, SIGHUP);
    close(pty->master);
    g_byte_array_unref(pty->unsent);
    g_free(pty);
}

#endif
