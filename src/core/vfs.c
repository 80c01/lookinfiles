/* Remote files over SFTP on a thread of their own (see vfs.h). */
#include "core/vfs.h"
#include "core/i18n.h"
#include "core/sftp_client.h"

#include <string.h>

#define TIMEOUT_US (60 * G_USEC_PER_SEC) /* a call gives up after that */
#define POLL_US (100 * 1000)             /* how often a waiting call looks at its cancellable */

/* A machine's folder. Its client is only touched on the thread. */
typedef struct {
    gint refs; /* the list, the jobs */
    gchar* local;
    gchar* host;
    gchar* user;
    gint port;
    gchar* identity;
    gchar* root;
    gboolean closed;    /* unmounted: no new connection (thread) */
    SftpClient* client; /* NULL until the first call (thread) */
} Mount;

static GMutex lock;     /* mounts, and starting the thread */
static GPtrArray* mounts; /* Mount* */
static GThread* thread;
static GMainContext* context;
static GMainLoop* loop;

static Mount* mount_ref(Mount* m) {
    g_atomic_int_inc(&m->refs);
    return m;
}

static void mount_unref(Mount* m) {
    if (!g_atomic_int_dec_and_test(&m->refs)) return;
    sftp_client_free(m->client); /* NULL once closed on the thread */
    g_free(m->local);
    g_free(m->host);
    g_free(m->user);
    g_free(m->identity);
    g_free(m->root);
    g_free(m);
}

static SshTarget mount_target(const Mount* m) {
    SshTarget t = { .host = m->host, .user = m->user, .port = m->port, .identity_file = m->identity };
    return t;
}

static gpointer thread_main(gpointer data) {
    (void)data;
    g_main_context_push_thread_default(context);
    g_main_loop_run(loop);
    g_main_context_pop_thread_default(context);
    return NULL;
}

/* The thread's context, the thread started the first time. lock held. */
static GMainContext* thread_context(void) {
    if (thread == NULL) {
        context = g_main_context_new();
        loop = g_main_loop_new(context, FALSE);
        thread = g_thread_new("vfs", thread_main, NULL);
    }
    return context;
}

/* path without its trailing '/' (but "/"). Caller frees. */
static gchar* strip_slashes(const gchar* path) {
    gchar* p = g_strdup(path != NULL ? path : "");
    gsize n = strlen(p);
    while (n > 1 && p[n - 1] == '/') p[--n] = '\0';
    return p;
}

static gboolean under(const gchar* path, const gchar* prefix, const gchar** rest) {
    gsize n = strlen(prefix);
    if (strncmp(path, prefix, n) != 0 || (path[n] != '\0' && path[n] != '/')) return FALSE;
    if (rest != NULL) {
        const gchar* r = path + n;
        while (*r == '/') r++;
        *rest = r;
    }
    return TRUE;
}

/* The mount path is in (a ref) and *rel what follows it. lock held. */
static Mount* find_mount(const gchar* path, gchar** rel) {
    if (mounts == NULL || path == NULL) return NULL;
    for (guint i = 0; i < mounts->len; i++) {
        Mount* m = mounts->pdata[i];
        const gchar* rest = NULL;
        if (under(path, m->local, &rest)) {
            if (rel != NULL) *rel = strip_slashes(rest);
            return mount_ref(m);
        }
    }
    return NULL;
}

/* On the thread: the client is closed; the last ref may go */
static gboolean close_mount(gpointer data) {
    Mount* m = data;
    m->closed = TRUE;
    g_clear_pointer(&m->client, sftp_client_free);
    mount_unref(m);
    return G_SOURCE_REMOVE;
}

/* lock held */
static void drop_mount(guint index) {
    Mount* m = g_ptr_array_steal_index(mounts, index);
    g_main_context_invoke(thread_context(), close_mount, m); /* the list's ref */
}

void vfs_mount(const gchar* local, const SshTarget* target, const gchar* remote_root) {
    if (local == NULL || target == NULL || target->host == NULL) return;
    Mount* m = g_new0(Mount, 1);
    m->refs = 1;
    m->local = strip_slashes(local);
    m->host = g_strdup(target->host);
    m->user = g_strdup(target->user);
    m->port = target->port;
    m->identity = g_strdup(target->identity_file);
    m->root = g_strdup(remote_root);
    g_mutex_lock(&lock);
    if (mounts == NULL) mounts = g_ptr_array_new();
    for (guint i = 0; i < mounts->len; i++) {
        Mount* old = mounts->pdata[i];
        if (strcmp(old->local, m->local) != 0) continue;
        if (g_strcmp0(old->host, m->host) == 0 && g_strcmp0(old->user, m->user) == 0 && old->port == m->port &&
            g_strcmp0(old->identity, m->identity) == 0 && g_strcmp0(old->root, m->root) == 0) {
            g_mutex_unlock(&lock); /* the same: its connection goes on */
            mount_unref(m);
            return;
        }
        drop_mount(i);
        break;
    }
    g_ptr_array_add(mounts, m);
    g_mutex_unlock(&lock);
}

void vfs_unmount(const gchar* local) {
    gchar* l = strip_slashes(local);
    g_mutex_lock(&lock);
    for (guint i = 0; mounts != NULL && i < mounts->len; i++) {
        if (strcmp(((Mount*)mounts->pdata[i])->local, l) == 0) {
            drop_mount(i);
            break;
        }
    }
    g_mutex_unlock(&lock);
    g_free(l);
}

gboolean vfs_is_remote(const gchar* path) {
    g_mutex_lock(&lock);
    Mount* m = find_mount(path, NULL);
    g_mutex_unlock(&lock);
    if (m == NULL) return FALSE;
    mount_unref(m);
    return TRUE;
}

static gboolean quit_loop(gpointer data) {
    (void)data;
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

void vfs_shutdown(void) {
    g_mutex_lock(&lock);
    while (mounts != NULL && mounts->len > 0) drop_mount(mounts->len - 1);
    g_clear_pointer(&mounts, g_ptr_array_unref);
    GThread* t = g_steal_pointer(&thread);
    g_mutex_unlock(&lock);
    if (t == NULL) return;
    g_main_context_invoke(context, quit_loop, NULL); /* after the closes */
    g_thread_join(t);
    g_clear_pointer(&loop, g_main_loop_unref);
    g_clear_pointer(&context, g_main_context_unref);
}

gchar* vfs_remote_path(const gchar* remote_root, const gchar* rel) {
    gchar* root = strip_slashes(remote_root);
    const gchar* base = root;
    if (strcmp(base, "~") == 0) {
        base = "";
    } else if (g_str_has_prefix(base, "~/")) {
        base += 2;
    }
    gchar* r = strip_slashes(rel);
    const gchar* sub = r;
    while (*sub == '/') sub++;
    gchar* out;
    if (base[0] == '\0') {
        out = g_strdup(sub[0] != '\0' ? sub : "."); /* the server starts in the home folder */
    } else if (sub[0] == '\0') {
        out = g_strdup(base);
    } else if (strcmp(base, "/") == 0) {
        out = g_strconcat("/", sub, NULL);
    } else {
        out = g_strconcat(base, "/", sub, NULL);
    }
    g_free(r);
    g_free(root);
    return out;
}

void vfs_entry_free(gpointer entry) {
    VfsEntry* e = entry;
    if (e == NULL) return;
    g_free(e->name);
    g_free(e);
}

/* ------------------------------------------------------------------------- */
/* Calls: a job run on the thread, waited for by the caller                  */
/* ------------------------------------------------------------------------- */

typedef struct {
    gint refs; /* the caller, the thread */
    Mount* m;
    gchar* remote;
    GMutex mu;
    GCond cond;
    gboolean done;
    GPtrArray* entries; /* list: VfsEntry */
    GError* error;
    guint waiting; /* link targets being looked at */
} Job;

static void job_unref(Job* j) {
    if (!g_atomic_int_dec_and_test(&j->refs)) return;
    mount_unref(j->m);
    g_free(j->remote);
    if (j->entries != NULL) g_ptr_array_unref(j->entries);
    g_clear_error(&j->error);
    g_mutex_clear(&j->mu);
    g_cond_clear(&j->cond);
    g_free(j);
}

/* On the thread: the job is over (error taken); the caller wakes */
static void job_done(Job* j, GError* error) {
    g_mutex_lock(&j->mu);
    if (j->error == NULL) {
        j->error = error;
    } else if (error != NULL) {
        g_error_free(error);
    }
    j->done = TRUE;
    g_cond_signal(&j->cond);
    g_mutex_unlock(&j->mu);
    job_unref(j);
}

/* On the thread: the machine's client, connected the first time or after
 * the connection was lost */
static SftpClient* mount_client(Mount* m, GError** error) {
    if (m->closed) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, _("the machine is disconnected"));
        return NULL;
    }
    if (m->client != NULL && !sftp_client_is_open(m->client)) g_clear_pointer(&m->client, sftp_client_free);
    if (m->client == NULL) {
        SshTarget t = mount_target(m);
        m->client = sftp_client_spawn(&t, error);
    }
    return m->client;
}

/* The path of path, or NULL (not remote: error set), with the job made */
static Job* job_new(const gchar* path, GError** error) {
    gchar* rel = NULL;
    g_mutex_lock(&lock);
    Mount* m = find_mount(path, &rel);
    g_mutex_unlock(&lock);
    if (m == NULL) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("not a folder of a remote machine"));
        return NULL;
    }
    Job* j = g_new0(Job, 1);
    j->refs = 1;
    j->m = m;
    j->remote = vfs_remote_path(m->root, rel);
    g_mutex_init(&j->mu);
    g_cond_init(&j->cond);
    g_free(rel);
    return j;
}

/* Run func on the thread with j and wait (at most TIMEOUT_US). TRUE when the
 * job is done: its results are there. */
static gboolean job_run(Job* j, GSourceFunc func, GCancellable* cancel, GError** error) {
    if (g_thread_self() == thread) { /* it would wait for itself */
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK, "vfs call on its own thread");
        return FALSE;
    }
    g_atomic_int_inc(&j->refs);
    g_mutex_lock(&lock);
    GMainContext* ctx = thread_context();
    g_mutex_unlock(&lock);
    g_main_context_invoke(ctx, func, j);
    gint64 deadline = g_get_monotonic_time() + TIMEOUT_US;
    g_mutex_lock(&j->mu);
    while (!j->done) {
        gint64 now = g_get_monotonic_time();
        if (now >= deadline || g_cancellable_is_cancelled(cancel)) break;
        g_cond_wait_until(&j->cond, &j->mu, MIN(deadline, now + POLL_US));
    }
    gboolean done = j->done;
    GError* local = done ? g_steal_pointer(&j->error) : NULL;
    g_mutex_unlock(&j->mu);
    if (!done && !g_cancellable_set_error_if_cancelled(cancel, &local)) {
        g_set_error_literal(&local, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, _("the remote machine does not answer"));
    }
    if (local == NULL) return TRUE;
    g_propagate_error(error, local);
    return FALSE;
}

/* ------------------------------------------------------------------------- */
/* List                                                                      */
/* ------------------------------------------------------------------------- */

typedef struct {
    Job* j;
    VfsEntry* e;
} LinkStat;

static void on_link_stat(GObject* src, GAsyncResult* res, gpointer data) {
    (void)src;
    LinkStat* ls = data;
    SftpAttrs a;
    if (sftp_client_stat_finish(res, &a, NULL)) { /* a dangling link stays a file */
        ls->e->is_dir = sftp_attrs_is_dir(&a);
        if (a.flags & SFTP_ATTR_SIZE) ls->e->size = (gint64)a.size;
        if (a.flags & SFTP_ATTR_ACMODTIME) ls->e->mtime = a.mtime;
    }
    Job* j = ls->j;
    g_free(ls);
    if (--j->waiting == 0) job_done(j, NULL);
}

static void on_listed(GObject* src, GAsyncResult* res, gpointer data) {
    (void)src;
    Job* j = data;
    GError* error = NULL;
    GPtrArray* list = sftp_client_list_finish(res, &error);
    if (list == NULL) {
        job_done(j, error);
        return;
    }
    j->entries = g_ptr_array_new_full(list->len, vfs_entry_free);
    j->waiting = 1; /* this function */
    for (guint i = 0; i < list->len; i++) {
        const SftpEntry* s = list->pdata[i];
        VfsEntry* e = g_new0(VfsEntry, 1);
        e->name = g_strdup(s->name);
        e->is_dir = sftp_attrs_is_dir(&s->attrs);
        e->is_link = sftp_attrs_is_link(&s->attrs);
        e->size = (s->attrs.flags & SFTP_ATTR_SIZE) ? (gint64)s->attrs.size : 0;
        e->mtime = (s->attrs.flags & SFTP_ATTR_ACMODTIME) ? s->attrs.mtime : 0;
        g_ptr_array_add(j->entries, e);
        if (e->is_link && j->m->client != NULL) { /* a link to a folder opens as one */
            LinkStat* ls = g_new(LinkStat, 1);
            ls->j = j;
            ls->e = e;
            j->waiting++;
            gchar* full = strcmp(j->remote, ".") == 0 ? g_strdup(s->name)
                          : strcmp(j->remote, "/") == 0 ? g_strconcat("/", s->name, NULL)
                                                        : g_strconcat(j->remote, "/", s->name, NULL);
            sftp_client_stat_async(j->m->client, full, TRUE, NULL, on_link_stat, ls);
            g_free(full);
        }
    }
    g_ptr_array_unref(list);
    if (--j->waiting == 0) job_done(j, NULL);
}

static gboolean run_list(gpointer data) {
    Job* j = data;
    GError* error = NULL;
    SftpClient* c = mount_client(j->m, &error);
    if (c == NULL) {
        job_done(j, error);
    } else {
        sftp_client_list_async(c, j->remote, NULL, on_listed, j);
    }
    return G_SOURCE_REMOVE;
}

GPtrArray* vfs_list(const gchar* path, GCancellable* cancel, GError** error) {
    Job* j = job_new(path, error);
    if (j == NULL) return NULL;
    GPtrArray* out = NULL;
    if (job_run(j, run_list, cancel, error)) {
        g_mutex_lock(&j->mu);
        out = g_steal_pointer(&j->entries);
        g_mutex_unlock(&j->mu);
    }
    job_unref(j);
    return out;
}
