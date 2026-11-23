/* A build or test task run on a remote machine (see task_remote.h) */
#include "tasks/task_remote.h"
#include "core/i18n.h"
#include "core/ssh_process.h"
#include "core/ssh_session.h"
#include "core/vfs.h"
#include <string.h>

#define PID_MARK "@@lookinfiles-task "
/* Output read at a time */
#define READ_BYTES 65536
/* The first line is the process number: never longer than this */
#define MAX_FIRST_LINE 256

struct TaskRemote {
    gint refs;      /* atomic: the owner, the thread, the events posted */
    gint cancelled; /* atomic: task_remote_free() was called */
    TaskDef* def;
    TaskRemoteFuncs funcs;
    gpointer data;
    GMutex lock;      /* proc and pid */
    SshProcess* proc; /* while it runs */
    gint pid;         /* the command's process on the machine, 0: not known yet */
    gchar* mount;       /* the folder the machine is seen at (set before ready) */
    gchar* remote_root; /* its absolute folder on the machine */
};

static TaskRemote* remote_ref(TaskRemote* t) {
    g_atomic_int_inc(&t->refs);
    return t;
}

static void remote_unref(TaskRemote* t) {
    if (!g_atomic_int_dec_and_test(&t->refs)) return;
    task_def_free(t->def);
    g_mutex_clear(&t->lock);
    g_free(t->mount);
    g_free(t->remote_root);
    g_free(t);
}

/* ------------------------------------------------------------------------- */
/* Parts                                                                     */
/* ------------------------------------------------------------------------- */

gchar* task_remote_command_line(const TaskDef* def) {
    GString* s = g_string_new("exec 2>&1; echo \"" PID_MARK "$$\"; exec");
    if (def->env != NULL && def->env[0] != NULL) {
        g_string_append(s, " env");
        for (gchar** e = def->env; *e != NULL; e++) {
            gchar* q = ssh_session_quote(*e);
            g_string_append_printf(s, " %s", q);
            g_free(q);
        }
    }
    if (def->shell && def->detail != NULL) {
        gchar* q = ssh_session_quote(def->detail);
        g_string_append_printf(s, " sh -c %s", q);
        g_free(q);
    } else {
        for (gchar** a = def->argv; a != NULL && *a != NULL; a++) {
            gchar* q = ssh_session_quote(*a);
            g_string_append_printf(s, " %s", q);
            g_free(q);
        }
    }
    return g_string_free(s, FALSE);
}

gint task_remote_parse_pid(const gchar* line) {
    if (line == NULL || !g_str_has_prefix(line, PID_MARK)) return 0;
    gint64 pid = g_ascii_strtoll(line + strlen(PID_MARK), NULL, 10);
    return pid > 0 && pid < G_MAXINT ? (gint)pid : 0;
}

/* path with "/" separators (the paths of the machine read through the
 * parser have the local ones on Windows) */
static gchar* slashes(const gchar* path) {
    gchar* s = g_strdup(path);
    for (gchar* c = s; *c != '\0'; c++) {
        if (*c == '\\') *c = '/';
    }
    return s;
}

gchar* task_remote_map_path(const gchar* remote_root, const gchar* mount, const gchar* path) {
    if (remote_root == NULL || mount == NULL || path == NULL) return NULL;
    gchar* p = slashes(path);
    gchar* root = slashes(remote_root);
    gsize n = strlen(root);
    while (n > 1 && root[n - 1] == '/') root[--n] = '\0';
    gchar* out = NULL;
    if (g_strcmp0(root, "/") == 0 && p[0] == '/') {
        out = p[1] != '\0' ? g_build_filename(mount, p + 1, NULL) : g_strdup(mount);
    } else if (strncmp(p, root, n) == 0 && (p[n] == '/' || p[n] == '\0')) {
        out = p[n] == '/' && p[n + 1] != '\0' ? g_build_filename(mount, p + n + 1, NULL) : g_strdup(mount);
    }
    g_free(root);
    g_free(p);
    return out;
}

gchar* task_remote_local_path(TaskRemote* t, const gchar* path) {
    return t != NULL ? task_remote_map_path(t->remote_root, t->mount, path) : NULL;
}

/* ------------------------------------------------------------------------- */
/* Events, on the main thread                                                */
/* ------------------------------------------------------------------------- */

typedef enum { EV_READY, EV_OUTPUT, EV_DONE } EventKind;

typedef struct {
    TaskRemote* t; /* a reference */
    EventKind kind;
    gchar* text;   /* EV_READY: the folder; EV_DONE: the error, or NULL */
    GBytes* bytes; /* EV_OUTPUT */
    gint status;   /* EV_DONE */
} Event;

static void event_free(gpointer p) {
    Event* ev = p;
    g_free(ev->text);
    if (ev->bytes != NULL) g_bytes_unref(ev->bytes);
    remote_unref(ev->t);
    g_free(ev);
}

static gboolean on_event(gpointer p) {
    Event* ev = p;
    TaskRemote* t = ev->t;
    if (g_atomic_int_get(&t->cancelled)) return G_SOURCE_REMOVE;
    switch (ev->kind) {
    case EV_READY:
        if (t->funcs.ready != NULL) t->funcs.ready(t->data, ev->text);
        break;
    case EV_OUTPUT: {
        gsize len = 0;
        const gchar* data = g_bytes_get_data(ev->bytes, &len);
        if (t->funcs.output != NULL && len > 0) t->funcs.output(t->data, data, len);
        break;
    }
    case EV_DONE:
        if (t->funcs.done != NULL) t->funcs.done(t->data, ev->status, ev->text);
        break;
    }
    return G_SOURCE_REMOVE;
}

/* Hand an event to the main thread (in order: the same priority) */
static void post(TaskRemote* t, EventKind kind, const gchar* text, const gchar* bytes, gsize len, gint status) {
    Event* ev = g_new0(Event, 1);
    ev->t = remote_ref(t);
    ev->kind = kind;
    ev->text = g_strdup(text);
    if (bytes != NULL) ev->bytes = g_bytes_new(bytes, len);
    ev->status = status;
    g_main_context_invoke_full(NULL, G_PRIORITY_DEFAULT, on_event, ev, event_free);
}

/* ------------------------------------------------------------------------- */
/* The thread                                                                */
/* ------------------------------------------------------------------------- */

/* The output read: the first line (the process number) kept back */
static void take_output(TaskRemote* t, GString* first, gboolean* first_done, const gchar* buf, gsize n) {
    if (*first_done) {
        post(t, EV_OUTPUT, NULL, buf, n, 0);
        return;
    }
    g_string_append_len(first, buf, (gssize)n);
    const gchar* nl = memchr(first->str, '\n', first->len);
    if (nl == NULL && first->len < MAX_FIRST_LINE) return;
    *first_done = TRUE;
    gsize skip = 0;
    if (nl != NULL) {
        gchar* line = g_strndup(first->str, (gsize)(nl - first->str));
        gint pid = task_remote_parse_pid(line);
        g_free(line);
        if (pid > 0) {
            g_mutex_lock(&t->lock);
            t->pid = pid;
            g_mutex_unlock(&t->lock);
            skip = (gsize)(nl - first->str) + 1;
        }
    }
    if (first->len > skip) post(t, EV_OUTPUT, NULL, first->str + skip, first->len - skip, 0);
    g_string_truncate(first, 0);
}

static gpointer remote_thread(gpointer data) {
    TaskRemote* t = data;
    GError* error = NULL;
    gchar* mount = vfs_mount_of(t->def->dir);
    gchar* root = mount != NULL ? vfs_remote_absolute(mount, NULL, &error) : NULL;
    gchar* dir = root != NULL ? vfs_remote_absolute(t->def->dir, NULL, &error) : NULL;
    if (dir == NULL) {
        post(t, EV_DONE, error != NULL ? error->message : _("the folder is not on a remote machine"), NULL, 0, -1);
        g_clear_error(&error);
        g_free(root);
        g_free(mount);
        remote_unref(t);
        return NULL;
    }
    t->mount = mount; /* read by the main thread after ready only */
    t->remote_root = root;
    gchar* line = task_remote_command_line(t->def);
    SshProcess* proc = g_atomic_int_get(&t->cancelled) ? NULL : vfs_spawn(t->def->dir, line, &error);
    g_free(line);
    if (proc == NULL) {
        post(t, EV_DONE, error != NULL ? error->message : NULL, NULL, 0, -1);
        g_clear_error(&error);
        g_free(dir);
        remote_unref(t);
        return NULL;
    }
    g_mutex_lock(&t->lock);
    t->proc = proc;
    g_mutex_unlock(&t->lock);
    post(t, EV_READY, dir, NULL, 0, 0);
    g_free(dir);
    /* No input: a question asked would wait forever */
    g_output_stream_close(ssh_process_get_stdin(proc), NULL, NULL);

    GInputStream* in = ssh_process_get_stdout(proc);
    gchar* buf = g_malloc(READ_BYTES);
    GString* first = g_string_new(NULL);
    gboolean first_done = FALSE;
    gssize n;
    while ((n = g_input_stream_read(in, buf, READ_BYTES, NULL, NULL)) > 0) {
        take_output(t, first, &first_done, buf, (gsize)n);
    }
    if (first->len > 0) post(t, EV_OUTPUT, NULL, first->str, first->len, 0);
    g_string_free(first, TRUE);
    g_free(buf);
    gint status = ssh_process_wait(proc);
    g_mutex_lock(&t->lock);
    t->proc = NULL;
    g_mutex_unlock(&t->lock);
    ssh_process_free(proc);
    post(t, EV_DONE, NULL, NULL, 0, status);
    remote_unref(t);
    return NULL;
}

TaskRemote* task_remote_start(const TaskDef* def, const TaskRemoteFuncs* funcs, gpointer data) {
    TaskRemote* t = g_new0(TaskRemote, 1);
    t->refs = 2; /* the owner and the thread */
    t->def = task_def_copy(def);
    t->funcs = *funcs;
    t->data = data;
    g_mutex_init(&t->lock);
    GThread* thread = g_thread_new("task-remote", remote_thread, t);
    g_thread_unref(thread);
    return t;
}

/* ------------------------------------------------------------------------- */
/* Stop                                                                      */
/* ------------------------------------------------------------------------- */

typedef struct {
    gchar* dir;
    gchar* command;
} KillJob;

static gpointer kill_thread(gpointer data) {
    KillJob* job = data;
    gint status = 0;
    GBytes* out = NULL;
    vfs_exec(job->dir, job->command, NULL, NULL, &status, &out, NULL);
    if (out != NULL) g_bytes_unref(out);
    g_free(job->command);
    g_free(job->dir);
    g_free(job);
    return NULL;
}

void task_remote_stop(TaskRemote* t, gboolean kill_now) {
    if (t == NULL) return;
    g_mutex_lock(&t->lock);
    gint pid = t->pid;
    gboolean running = t->proc != NULL;
    if (running && (kill_now || pid == 0)) ssh_process_force_exit(t->proc);
    g_mutex_unlock(&t->lock);
    if (!running || pid == 0) return;
    /* The children first: once their parent is gone they cannot be found */
    const gchar* sig = kill_now ? "KILL" : "TERM";
    KillJob* job = g_new0(KillJob, 1);
    job->dir = g_strdup(t->def->dir);
    job->command = g_strdup_printf("pkill -%1$s -P %2$d 2>/dev/null; kill -%1$s %2$d 2>/dev/null; true", sig, pid);
    GThread* thread = g_thread_new("task-remote-stop", kill_thread, job);
    g_thread_unref(thread);
}

void task_remote_free(TaskRemote* t) {
    if (t == NULL) return;
    g_atomic_int_set(&t->cancelled, 1);
    task_remote_stop(t, TRUE);
    remote_unref(t);
}
