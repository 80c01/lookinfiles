/* SSH sessions through the OpenSSH client (see ssh_session.h). */
#include "core/ssh_session.h"
#include "core/app_paths.h"
#include "core/i18n.h"
#ifdef HAVE_LIBSSH
#include "core/ssh_link.h"
#include "core/ssh_link_exec.h"
#include "core/ssh_relay.h"
#endif

#include <glib/gstdio.h>
#include <string.h>

#ifdef G_OS_UNIX
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#endif
#ifdef __linux__
#include <sys/prctl.h>
#endif

#ifdef G_OS_WIN32
#define SSH_DEFAULT_PROGRAM "ssh.exe"
#else
#define SSH_DEFAULT_PROGRAM "ssh"
#endif

static gchar* program;   /* NULL: SSH_DEFAULT_PROGRAM */
static GHashTable* envs; /* name -> value, set in every program started */
static SshMasterMissingFunc missing_func;
static gpointer missing_data;

void ssh_session_on_master_missing(SshMasterMissingFunc func, gpointer data) {
    missing_func = func;
    missing_data = data;
}

void ssh_session_set_program(const gchar* prog) {
    g_free(program);
    program = (prog != NULL && prog[0] != '\0') ? g_strdup(prog) : NULL;
}

void ssh_session_set_env(const gchar* name, const gchar* value) {
    if (name == NULL) return;
    if (envs == NULL) envs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    if (value != NULL) {
        g_hash_table_insert(envs, g_strdup(name), g_strdup(value));
    } else {
        g_hash_table_remove(envs, name);
    }
}

static gboolean has_text(const gchar* s) {
    return s != NULL && s[0] != '\0';
}

gchar* ssh_session_destination(const SshTarget* t) {
    if (t == NULL || t->host == NULL) return g_strdup("");
    return has_text(t->user) ? g_strdup_printf("%s@%s", t->user, t->host) : g_strdup(t->host);
}

/* ------------------------------------------------------------------------- */
/* Multiplexing: the sockets of the master connections                      */
/* ------------------------------------------------------------------------- */

/* <runtime>/ssh, created 0700, or NULL */
static gchar* sockets_dir(void) {
    gchar* runtime = app_runtime_dir();
    if (runtime == NULL) return NULL;
    gchar* dir = g_build_filename(runtime, "ssh", NULL);
    g_free(runtime);
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_free(dir);
        return NULL;
    }
    return dir;
}

gboolean ssh_session_can_multiplex(void) {
#ifdef G_OS_WIN32
    return FALSE; /* the Windows OpenSSH client has no ControlMaster */
#else
    gchar* dir = sockets_dir();
    gboolean ok = dir != NULL;
    g_free(dir);
    return ok;
#endif
}

gboolean ssh_session_shares_connection(void) {
#ifdef HAVE_LIBSSH
    if (ssh_link_enabled()) return TRUE;
#endif
    return ssh_session_can_multiplex();
}

gchar* ssh_session_control_path(const SshTarget* t) {
    if (t == NULL || t->host == NULL || !ssh_session_can_multiplex()) return NULL;
    gchar* dir = sockets_dir();
    gchar* dest = ssh_session_destination(t);
    gchar* key = g_strdup_printf("%s:%d", dest, t->port > 0 ? t->port : 22);
    gchar* sum = g_compute_checksum_for_string(G_CHECKSUM_SHA1, key, -1);
    sum[16] = '\0'; /* short: a socket path has ~100 bytes at most */
    /* One per process: an instance never uses (nor removes) another's */
#ifdef G_OS_UNIX
    gchar* name = g_strdup_printf("%ld-%s", (long)getpid(), sum);
#else
    gchar* name = g_strdup(sum);
#endif
    gchar* path = g_build_filename(dir, name, NULL);
    g_free(name);
    g_free(sum);
    g_free(key);
    g_free(dest);
    g_free(dir);
    return path;
}

guint ssh_session_cleanup_stale(void) {
    guint removed = 0;
#ifdef G_OS_UNIX
    gchar* dir = sockets_dir();
    GDir* d = dir != NULL ? g_dir_open(dir, 0, NULL) : NULL;
    const gchar* name;
    while (d != NULL && (name = g_dir_read_name(d)) != NULL) {
        gchar* end = NULL;
        gint64 pid = g_ascii_strtoll(name, &end, 10);
        if (pid <= 0 || end == NULL || *end != '-' || pid == (gint64)getpid()) continue;
        if (kill((pid_t)pid, 0) == 0 || errno != ESRCH) continue; /* still running */
        gchar* path = g_build_filename(dir, name, NULL);
        if (g_remove(path) == 0) removed++;
        g_free(path);
    }
    if (d != NULL) g_dir_close(d);
    g_free(dir);
#endif
    return removed;
}

/* ------------------------------------------------------------------------- */
/* Command lines                                                             */
/* ------------------------------------------------------------------------- */

/* "Name=value", the value in double quotes when it holds a space */
static gchar* option(const gchar* name, const gchar* value) {
    return strpbrk(value, " \t") != NULL ? g_strdup_printf("%s=\"%s\"", name, value)
                                          : g_strdup_printf("%s=%s", name, value);
}

GPtrArray* ssh_session_options(const SshTarget* t, SshArgvFlags flags) {
    GPtrArray* opts = g_ptr_array_new_with_free_func(g_free);
    if (t != NULL && t->port > 0 && t->port != 22) g_ptr_array_add(opts, g_strdup_printf("Port=%d", t->port));
    if (t != NULL && has_text(t->identity_file)) g_ptr_array_add(opts, option("IdentityFile", t->identity_file));
    g_ptr_array_add(opts, g_strdup("ServerAliveInterval=15"));
    g_ptr_array_add(opts, g_strdup("ServerAliveCountMax=3"));
    g_ptr_array_add(opts, g_strdup("ConnectTimeout=20"));
    g_ptr_array_add(opts, g_strdup("StrictHostKeyChecking=accept-new"));
    gchar* sock = (flags & SSH_ARGV_NO_MUX) ? NULL : ssh_session_control_path(t);
    if (sock != NULL) {
        g_ptr_array_add(opts, option("ControlPath", sock));
        if (flags & SSH_ARGV_MASTER) {
            g_ptr_array_add(opts, g_strdup("ControlMaster=yes"));
            g_ptr_array_add(opts, g_strdup("ControlPersist=no"));
        } else {
            g_ptr_array_add(opts, g_strdup("ControlMaster=no")); /* the master, or a connection of its own */
            g_ptr_array_add(opts, g_strdup("BatchMode=yes"));    /* never a prompt: the master logs in */
        }
        g_free(sock);
    }
    return opts;
}

typedef struct {
    gchar* host;
    gchar* user;
    gint port;
    gchar* identity;
} MissingCall;

/* On the main thread: the application opens the master */
static gboolean call_missing(gpointer data) {
    MissingCall* m = data;
    SshTarget t = { .host = m->host, .user = m->user, .port = m->port, .identity_file = m->identity };
    if (missing_func != NULL) missing_func(&t, missing_data);
    g_free(m->host);
    g_free(m->user);
    g_free(m->identity);
    g_free(m);
    return G_SOURCE_REMOVE;
}

/* Tell the application that t has no shared connection */
static void report_missing(const SshTarget* t) {
    if (missing_func == NULL) return;
    MissingCall* m = g_new0(MissingCall, 1);
    m->host = g_strdup(t->host);
    m->user = g_strdup(t->user);
    m->port = t->port;
    m->identity = g_strdup(t->identity_file);
    g_main_context_invoke(NULL, call_missing, m); /* at once on the main thread, else queued there */
}

GPtrArray* ssh_session_argv(const SshTarget* t, SshArgvFlags flags) {
    gboolean force_batch = FALSE;
#ifdef HAVE_LIBSSH
    if (ssh_link_enabled() && !(flags & SSH_ARGV_MASTER)) {
        /* With a libssh link, SSH_ARGV_NO_MUX only skips the OpenSSH master:
         * interactive terminals also go through the relay (its per-channel
         * flow control keeps one slow terminal from stalling the link). */
        SshLink* link = ssh_link_lookup(t);
        if (link != NULL) {
            GPtrArray* relay = ssh_relay_argv(link, (flags & SSH_ARGV_TTY) != 0);
            ssh_link_unref(link);
            if (relay != NULL) return relay;
            /* No relay (no runtime directory, missing console helper): ssh
             * then, never asking in background restart loops. */
            if (!(flags & SSH_ARGV_TTY)) force_batch = TRUE;
        } else {
            if (missing_func != NULL) report_missing(t);
            force_batch = TRUE;
        }
    } else
#endif
    if (!(flags & (SSH_ARGV_MASTER | SSH_ARGV_NO_MUX))) {
        if (missing_func != NULL) {
            gchar* sock = ssh_session_control_path(t);
            gboolean missing = (sock != NULL && !g_file_test(sock, G_FILE_TEST_EXISTS));
            g_free(sock);
            if (missing) report_missing(t);
        }
    }
    GPtrArray* argv = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(argv, g_strdup(program != NULL ? program : SSH_DEFAULT_PROGRAM));
    GPtrArray* opts = ssh_session_options(t, flags);
    gboolean has_batch = FALSE;
    for (guint i = 0; i < opts->len; i++) {
        if (g_strcmp0(opts->pdata[i], "BatchMode=yes") == 0) has_batch = TRUE;
        g_ptr_array_add(argv, g_strdup("-o"));
        g_ptr_array_add(argv, g_strdup(opts->pdata[i]));
    }
    g_ptr_array_unref(opts);
    if (force_batch && !has_batch) {
        g_ptr_array_add(argv, g_strdup("-o"));
        g_ptr_array_add(argv, g_strdup("BatchMode=yes"));
    }
    if (flags & SSH_ARGV_MASTER) {
        /* No -M: after "-o ControlMaster=yes" it turns it into "ask", a
         * confirmation for every command sent through the master */
        g_ptr_array_add(argv, g_strdup("-N"));
    } else if (flags & SSH_ARGV_TTY) {
        g_ptr_array_add(argv, g_strdup("-t"));
    }
    g_ptr_array_add(argv, ssh_session_destination(t));
    return argv;
}

gchar* ssh_session_quote(const gchar* arg) {
    return g_shell_quote(arg != NULL ? arg : "");
}

#ifdef G_OS_UNIX
/* In the child: no controlling terminal (ssh asks through SSH_ASKPASS, never
 * on the terminal the application was started from) and, on Linux, killed
 * when the application ends (even by a crash) */
static void child_setup(gpointer data) {
    (void)data;
    setsid();
#ifdef __linux__
    prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
}
#endif

GSubprocessLauncher* ssh_session_launcher(GSubprocessFlags flags) {
    GSubprocessLauncher* l = g_subprocess_launcher_new(flags);
    if (envs != NULL) {
        GHashTableIter it;
        gpointer name, value;
        g_hash_table_iter_init(&it, envs);
        while (g_hash_table_iter_next(&it, &name, &value)) {
            g_subprocess_launcher_setenv(l, name, value, TRUE);
        }
    }
#ifdef G_OS_UNIX
    g_subprocess_launcher_set_child_setup(l, child_setup, NULL, NULL);
#endif
    return l;
}

static GSubprocess* spawn(GPtrArray* argv, GSubprocessFlags flags, GError** error) {
    g_ptr_array_add(argv, NULL);
    GSubprocessLauncher* l = ssh_session_launcher(flags);
    GSubprocess* p = g_subprocess_launcher_spawnv(l, (const gchar* const*)argv->pdata, error);
    g_object_unref(l);
    return p;
}

/* What a process that failed wrote on its standard error, else a sentence */
static gchar* failure_text(GBytes* err, gint status) {
    gsize n = 0;
    const gchar* data = err != NULL ? g_bytes_get_data(err, &n) : NULL;
    gchar* text = n > 0 ? g_utf8_make_valid(data, (gssize)n) : NULL;
    if (text != NULL) g_strstrip(text);
    if (text == NULL || text[0] == '\0') {
        g_free(text);
        text = status >= 0 ? g_strdup_printf("ssh exited with status %d", status) : g_strdup("ssh could not run");
    }
    return text;
}

static gint exit_status(GSubprocess* p) {
    return g_subprocess_get_if_exited(p) ? g_subprocess_get_exit_status(p) : -1;
}

/* ------------------------------------------------------------------------- */
/* Master connection                                                         */
/* ------------------------------------------------------------------------- */

struct SshMaster {
    gint refs;              /* this handle + the running process wait */
    gchar* host;
    gchar* user;
    gint port;
    gchar* identity_file;
    GSubprocess* proc;
    GCancellable* cancel;
    gchar* socket;          /* NULL without multiplexing */
    guint poll_id;
    gboolean up;
    gboolean reported;      /* ready() called */
    gboolean stopped;
    gchar* spawn_error;     /* ssh could not start: reported from an idle */
    SshMasterFunc ready;
    gpointer data;
};

static SshTarget master_target(const SshMaster* m) {
    SshTarget t = { m->host, m->user, m->port, m->identity_file };
    return t;
}

static void master_unref(SshMaster* m) {
    if (--m->refs > 0) return;
    g_clear_object(&m->proc);
    g_clear_object(&m->cancel);
    g_free(m->socket);
    g_free(m->spawn_error);
    g_free(m->host);
    g_free(m->user);
    g_free(m->identity_file);
    g_free(m);
}

static void master_report(SshMaster* m, gboolean ok, const gchar* text) {
    if (m->reported || m->stopped) return;
    m->reported = TRUE;
    if (m->ready != NULL) m->ready(m, ok, text, m->data);
}

/* The socket appears once ssh is connected and authenticated */
static gboolean on_master_poll(gpointer data) {
    SshMaster* m = data;
    if (m->socket == NULL || !g_file_test(m->socket, G_FILE_TEST_EXISTS)) return G_SOURCE_CONTINUE;
    m->poll_id = 0;
    m->up = TRUE;
    master_report(m, TRUE, NULL);
    return G_SOURCE_REMOVE;
}

static void on_master_exit(GObject* source, GAsyncResult* res, gpointer data) {
    SshMaster* m = data;
    GBytes* err = NULL;
    GError* error = NULL;
    g_subprocess_communicate_finish(G_SUBPROCESS(source), res, NULL, &err, &error);
    if (!m->stopped) {
        gint status = error == NULL ? exit_status(G_SUBPROCESS(source)) : -1;
        if (m->poll_id != 0) {
            g_source_remove(m->poll_id);
            m->poll_id = 0;
        }
        if (m->socket == NULL && status == 0) { /* the check without multiplexing */
            m->up = TRUE;
            master_report(m, TRUE, NULL);
        } else {
            m->up = FALSE; /* failed, or the connection dropped */
            gchar* text = failure_text(err, status);
            master_report(m, FALSE, text);
            g_free(text);
        }
    }
    if (err != NULL) g_bytes_unref(err);
    g_clear_error(&error);
    master_unref(m);
}

static gboolean on_spawn_failed(gpointer data) {
    SshMaster* m = data;
    master_report(m, FALSE, m->spawn_error);
    master_unref(m);
    return G_SOURCE_REMOVE;
}

SshMaster* ssh_master_start(const SshTarget* t, SshMasterFunc ready, gpointer data) {
    SshMaster* m = g_new0(SshMaster, 1);
    m->refs = 1;
    m->host = g_strdup(t != NULL ? t->host : NULL);
    m->user = g_strdup(t != NULL ? t->user : NULL);
    m->port = t != NULL ? t->port : 0;
    m->identity_file = g_strdup(t != NULL ? t->identity_file : NULL);
    m->ready = ready;
    m->data = data;
    m->cancel = g_cancellable_new();
    SshTarget target = master_target(m);
    m->socket = ssh_session_control_path(&target);

    GPtrArray* argv;
    if (m->socket != NULL) {
        g_remove(m->socket); /* left by an earlier master of this process */
        argv = ssh_session_argv(&target, SSH_ARGV_MASTER);
    } else {
        argv = ssh_session_argv(&target, SSH_ARGV_NO_MUX);
        g_ptr_array_add(argv, g_strdup("exit 0"));
    }
    GError* error = NULL;
    m->proc = spawn(argv, G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                              G_SUBPROCESS_FLAGS_STDERR_PIPE, &error);
    g_ptr_array_unref(argv);
    if (m->proc == NULL) {
        /* Reported from the main loop, as when ssh fails later */
        m->spawn_error = g_strdup(error->message);
        g_error_free(error);
        m->refs++;
        g_idle_add(on_spawn_failed, m);
        return m;
    }
    m->refs++; /* held by the wait */
    GBytes* none = g_bytes_new(NULL, 0); /* stdin closed at once (ssh -N reads none) */
    g_subprocess_communicate_async(m->proc, none, m->cancel, on_master_exit, m);
    g_bytes_unref(none);
    if (m->socket != NULL) m->poll_id = g_timeout_add(100, on_master_poll, m);
    return m;
}

gboolean ssh_master_is_up(const SshMaster* m) {
    return m != NULL && m->up;
}

void ssh_master_stop(SshMaster* m) {
    if (m == NULL) return;
    m->stopped = TRUE;
    m->up = FALSE;
    if (m->poll_id != 0) {
        g_source_remove(m->poll_id);
        m->poll_id = 0;
    }
    if (m->proc != NULL) {
#ifdef G_OS_UNIX
        g_subprocess_send_signal(m->proc, SIGTERM); /* ssh removes its socket */
#else
        g_subprocess_force_exit(m->proc);
#endif
    }
    g_cancellable_cancel(m->cancel);
    if (m->socket != NULL) g_remove(m->socket);
    master_unref(m);
}

/* ------------------------------------------------------------------------- */
/* Remote commands                                                           */
/* ------------------------------------------------------------------------- */

typedef struct {
    SshExecFunc done;
    gpointer data;
    GError* error; /* ssh could not start */
} ExecRun;

static gboolean on_exec_spawn_failed(gpointer data) {
    ExecRun* run = data;
    if (run->done != NULL) run->done(-1, "", "", run->error, run->data);
    g_error_free(run->error);
    g_free(run);
    return G_SOURCE_REMOVE;
}

/* NUL-terminated copy of the bytes (never NULL) */
static gchar* bytes_text(GBytes* b) {
    gsize n = 0;
    const gchar* data = b != NULL ? g_bytes_get_data(b, &n) : NULL;
    gchar* s = g_malloc(n + 1);
    if (n > 0) memcpy(s, data, n);
    s[n] = '\0';
    return s;
}

static void on_exec_done(GObject* source, GAsyncResult* res, gpointer data) {
    ExecRun* run = data;
    GBytes* out = NULL;
    GBytes* err = NULL;
    GError* error = NULL;
    g_subprocess_communicate_finish(G_SUBPROCESS(source), res, &out, &err, &error);
    gint status = error == NULL ? exit_status(G_SUBPROCESS(source)) : -1;
    gchar* out_text = bytes_text(out);
    gchar* err_text = bytes_text(err);
    if (run->done != NULL) run->done(status, out_text, err_text, error, run->data);
    g_free(out_text);
    g_free(err_text);
    if (out != NULL) g_bytes_unref(out);
    if (err != NULL) g_bytes_unref(err);
    g_clear_error(&error);
    g_object_unref(source);
    g_free(run);
}

/* command on t through its master (none: NOT_CONNECTED, no login of its
 * own, as ssh_exec_async()) */
static GSubprocess* spawn_command(const SshTarget* t, const gchar* command, GSubprocessFlags flags, GError** error) {
    gchar* sock = ssh_session_control_path(t);
    gboolean missing = sock != NULL && !g_file_test(sock, G_FILE_TEST_EXISTS);
    g_free(sock);
    if (missing) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, _("the SSH connection to the machine is not open"));
        return NULL;
    }
    GPtrArray* argv = ssh_session_argv(t, 0);
    g_ptr_array_add(argv, g_strdup(command != NULL ? command : "true"));
    GSubprocess* p = spawn(argv, flags, error);
    g_ptr_array_unref(argv);
    return p;
}

#ifdef HAVE_LIBSSH
/* The machine's libssh link, or NULL (error set, the application told)
 * when libssh is in use but the link is not open: never an ssh program
 * logging in by itself, once per command */
static SshLink* command_link(const SshTarget* t, GError** error) {
    SshLink* link = ssh_link_lookup(t);
    if (link == NULL) {
        report_missing(t);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, _("the SSH connection to the machine is not open"));
    }
    return link;
}
#endif

SshProcess* ssh_spawn(const SshTarget* t, const gchar* command, GError** error) {
#ifdef HAVE_LIBSSH
    if (ssh_link_enabled()) {
        if (g_main_context_is_owner(g_main_context_default())) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK, "ssh_spawn() on the main thread");
            return NULL;
        }
        SshLink* link = command_link(t, error);
        if (link == NULL) return NULL;
        SshChannel* ch = ssh_link_open_channel_sync(link, SSH_CHANNEL_KIND_EXEC, command != NULL ? command : "true",
                                                    0, 0, NULL, error);
        ssh_link_unref(link);
        if (ch == NULL) return NULL;
        SshProcess* p = ssh_process_new_channel(G_IO_STREAM(ch));
        g_object_unref(ch);
        return p;
    }
#endif
    GSubprocess* sp = spawn_command(t, command, G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                                    G_SUBPROCESS_FLAGS_STDERR_SILENCE, error);
    if (sp == NULL) return NULL;
    SshProcess* p = ssh_process_new_subprocess(sp);
    g_object_unref(sp);
    return p;
}

gboolean ssh_exec_sync(const SshTarget* t, const gchar* command, GBytes* input, GCancellable* cancel, gint* status,
                       GBytes** out, GError** error) {
    *out = NULL;
#ifdef HAVE_LIBSSH
    if (ssh_link_enabled()) {
        SshLink* link = command_link(t, error);
        if (link == NULL) return FALSE;
        gboolean ok = ssh_link_exec(link, command != NULL ? command : "true", input, cancel, status, out, NULL, error);
        ssh_link_unref(link);
        return ok;
    }
#endif
    GSubprocess* p = spawn_command(t, command, G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                                   G_SUBPROCESS_FLAGS_STDERR_PIPE, error);
    if (p == NULL) return FALSE;
    GBytes* err = NULL;
    /* A pipe needs its bytes, even none: stdin then closed at once */
    GBytes* in = input != NULL ? g_bytes_ref(input) : g_bytes_new(NULL, 0);
    gboolean ok = g_subprocess_communicate(p, in, cancel, out, &err, error);
    g_bytes_unref(in);
    if (ok) {
        *status = g_subprocess_get_if_exited(p) ? g_subprocess_get_exit_status(p) : -1;
        if (*status == 255 || *status < 0) { /* ssh itself failed */
            gchar* why = failure_text(err, *status);
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, why);
            g_free(why);
            g_clear_pointer(out, g_bytes_unref);
            ok = FALSE;
        }
    } else {
        g_subprocess_force_exit(p);
        g_clear_pointer(out, g_bytes_unref);
    }
    if (err != NULL) g_bytes_unref(err);
    g_object_unref(p);
    return ok;
}

#ifdef HAVE_LIBSSH
typedef struct {
    SshLink* link;
    gchar* command;
    GBytes* input;
    gint status;
    GBytes* out;
    GBytes* err;
    SshExecFunc done;
    gpointer data;
} LinkExec;

static void link_exec_free(gpointer data) {
    LinkExec* x = data;
    ssh_link_unref(x->link);
    g_free(x->command);
    g_bytes_unref(x->input);
    if (x->out != NULL) g_bytes_unref(x->out);
    if (x->err != NULL) g_bytes_unref(x->err);
    g_free(x);
}

static void link_exec_thread(GTask* task, gpointer source, gpointer data, GCancellable* cancel) {
    (void)source;
    LinkExec* x = data;
    GError* error = NULL;
    if (ssh_link_exec(x->link, x->command, x->input, cancel, &x->status, &x->out, &x->err, &error)) {
        g_task_return_boolean(task, TRUE);
    } else {
        g_task_return_error(task, error);
    }
}

static void on_link_exec_done(GObject* source, GAsyncResult* res, gpointer data) {
    (void)source;
    (void)data;
    LinkExec* x = g_task_get_task_data(G_TASK(res));
    GError* error = NULL;
    gboolean ok = g_task_propagate_boolean(G_TASK(res), &error);
    gchar* out_text = bytes_text(ok ? x->out : NULL);
    gchar* err_text = bytes_text(ok ? x->err : NULL);
    if (x->done != NULL) x->done(ok ? x->status : -1, out_text, err_text, error, x->data);
    g_free(out_text);
    g_free(err_text);
    g_clear_error(&error);
}

/* ssh_exec_async() on the link's exec channel, run by a worker thread */
static void link_exec_async(const SshTarget* t, const gchar* command, const gchar* input, GCancellable* cancel,
                            SshExecFunc done, gpointer data) {
    GError* error = NULL;
    SshLink* link = command_link(t, &error);
    if (link == NULL) {
        ExecRun* run = g_new0(ExecRun, 1);
        run->done = done;
        run->data = data;
        run->error = error;
        g_idle_add(on_exec_spawn_failed, run);
        return;
    }
    LinkExec* x = g_new0(LinkExec, 1);
    x->link = link;
    x->command = g_strdup(command != NULL ? command : "true");
    x->input = g_bytes_new(input, input != NULL ? strlen(input) : 0);
    x->done = done;
    x->data = data;
    GTask* task = g_task_new(NULL, cancel, on_link_exec_done, NULL);
    g_task_set_task_data(task, x, link_exec_free);
    g_task_run_in_thread(task, link_exec_thread);
    g_object_unref(task);
}
#endif

void ssh_exec_async(const SshTarget* t, const gchar* command, const gchar* input,
                    GCancellable* cancel, SshExecFunc done, gpointer data) {
#ifdef HAVE_LIBSSH
    if (ssh_link_enabled()) {
        link_exec_async(t, command, input, cancel, done, data);
        return;
    }
#endif
    GPtrArray* argv = ssh_session_argv(t, 0); /* asks for the master when it is not open */
    g_ptr_array_add(argv, g_strdup(command != NULL ? command : "true"));
    GError* error = NULL;
    GSubprocess* p = NULL;
    gchar* sock = ssh_session_control_path(t);
    if (sock != NULL && !g_file_test(sock, G_FILE_TEST_EXISTS)) {
        /* Not a connection of its own: one failed login a second (a command
         * read every second) would get the address banned (fail2ban) */
        g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED,
                            _("the SSH connection to the machine is not open"));
    } else {
        p = spawn(argv, G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                            G_SUBPROCESS_FLAGS_STDERR_PIPE, &error);
    }
    g_free(sock);
    g_ptr_array_unref(argv);
    ExecRun* run = g_new0(ExecRun, 1);
    run->done = done;
    run->data = data;
    if (p == NULL) {
        run->error = error;
        g_idle_add(on_exec_spawn_failed, run);
        return;
    }
    GBytes* in = g_bytes_new(input, input != NULL ? strlen(input) : 0);
    g_subprocess_communicate_async(p, in, cancel, on_exec_done, run);
    g_bytes_unref(in);
}
