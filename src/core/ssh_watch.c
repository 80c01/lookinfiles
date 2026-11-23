/* Level-triggered socket source (see ssh_watch.h). */
#ifdef _WIN32 /* not G_OS_WIN32: GLib, which defines it, is not included yet */
#include <winsock2.h> /* before any header that may pull windows.h */
#endif
#include "core/ssh_watch.h"

#ifdef G_OS_WIN32
/* Waits for the socket on a thread of its own and wakes the source's
 * context. Shared by the source and the thread (refs). */
typedef struct {
    gint refs;
    SOCKET sock;
    gint want;  /* GIOCondition, atomic: G_IO_IN / G_IO_OUT */
    gint stop;  /* atomic */
    GMutex mu;  /* guards the fields below */
    GCond cond;
    gboolean armed;    /* the thread may wait for the socket again */
    gboolean fired;    /* it saw the socket ready since the last prepare */
    GMainContext* ctx; /* the source's, known from its first prepare */
} Waiter;

#define WAIT_SLICE_MS 100 /* how soon the thread sees a new condition or stop */
#endif

typedef struct {
    GSource source;
    GIOCondition want;
    GIOCondition state; /* last reported */
#ifdef G_OS_WIN32
    Waiter* waiter;
#else
    GPollFD pfd;
#endif
} SshWatch;

#ifdef G_OS_WIN32
static void waiter_unref(Waiter* w) {
    if (!g_atomic_int_dec_and_test(&w->refs)) return;
    if (w->ctx != NULL) g_main_context_unref(w->ctx);
    g_mutex_clear(&w->mu);
    g_cond_clear(&w->cond);
    g_free(w);
}

/* The socket's state (WSAPoll() does not wait with timeout 0). Not
 * WSAEventSelect(): it forces the socket non-blocking for as long as it is
 * registered, and libssh switches the socket back to blocking once
 * connected; that call then fails and the session never leaves the
 * "connecting" state. WSAPoll() leaves the socket's mode alone. */
static GIOCondition poll_socket(SOCKET sock, GIOCondition want, gint timeout_ms) {
    WSAPOLLFD p = {.fd = sock, .events = 0, .revents = 0};
    if (want & G_IO_IN) p.events |= POLLRDNORM;
    if (want & G_IO_OUT) p.events |= POLLWRNORM;
    int r = WSAPoll(&p, 1, timeout_ms);
    if (r == SOCKET_ERROR) return G_IO_ERR;
    if (r == 0) return 0;
    GIOCondition c = 0;
    if (p.revents & POLLRDNORM) c |= G_IO_IN;
    if (p.revents & POLLWRNORM) c |= G_IO_OUT;
    if (p.revents & POLLHUP) c |= G_IO_HUP;
    if (p.revents & (POLLERR | POLLNVAL)) c |= G_IO_ERR;
    return c;
}

static gpointer waiter_main(gpointer data) {
    Waiter* w = data;
    for (;;) {
        g_mutex_lock(&w->mu);
        while (!w->armed && !g_atomic_int_get(&w->stop)) g_cond_wait(&w->cond, &w->mu);
        g_mutex_unlock(&w->mu);
        if (g_atomic_int_get(&w->stop)) break;
        GIOCondition ready = poll_socket(w->sock, (GIOCondition)g_atomic_int_get(&w->want), WAIT_SLICE_MS);
        if (g_atomic_int_get(&w->stop)) break;
        if (ready == 0) continue;
        /* Once, until the source has looked: a socket nobody reads must not spin */
        g_mutex_lock(&w->mu);
        w->armed = FALSE;
        w->fired = TRUE;
        GMainContext* ctx = w->ctx != NULL ? g_main_context_ref(w->ctx) : NULL;
        g_mutex_unlock(&w->mu);
        if (ctx != NULL) {
            g_main_context_wakeup(ctx);
            g_main_context_unref(ctx);
        }
    }
    waiter_unref(w);
    return NULL;
}

static gboolean watch_prepare(GSource* source, gint* timeout) {
    SshWatch* w = (SshWatch*)source;
    Waiter* t = w->waiter;
    g_mutex_lock(&t->mu);
    if (t->ctx == NULL) t->ctx = g_main_context_ref(g_source_get_context(source));
    if (t->fired) { /* seen: the thread may wait again */
        t->fired = FALSE;
        t->armed = TRUE;
        g_cond_signal(&t->cond);
    }
    g_mutex_unlock(&t->mu);
    *timeout = -1;
    w->state = poll_socket(t->sock, w->want, 0);
    return w->state != 0;
}

static gboolean watch_check(GSource* source) {
    SshWatch* w = (SshWatch*)source;
    w->state = poll_socket(w->waiter->sock, w->want, 0);
    return w->state != 0;
}

static void watch_finalize(GSource* source) {
    Waiter* t = ((SshWatch*)source)->waiter;
    g_atomic_int_set(&t->stop, 1);
    g_mutex_lock(&t->mu);
    g_cond_signal(&t->cond);
    g_mutex_unlock(&t->mu);
    waiter_unref(t); /* the thread drops its own ref within WAIT_SLICE_MS */
}
#else
static gboolean watch_prepare(GSource* source, gint* timeout) {
    (void)source;
    *timeout = -1;
    return FALSE;
}

static gboolean watch_check(GSource* source) {
    SshWatch* w = (SshWatch*)source;
    w->state = (GIOCondition)w->pfd.revents;
    return w->state != 0;
}

static void watch_finalize(GSource* source) {
    (void)source;
}
#endif

static gboolean watch_dispatch(GSource* source, GSourceFunc callback, gpointer data) {
    (void)source;
    return callback != NULL ? callback(data) : G_SOURCE_REMOVE;
}

static GSourceFuncs watch_funcs = {watch_prepare, watch_check, watch_dispatch, watch_finalize, NULL, NULL};

GSource* ssh_watch_new(gint64 fd, GIOCondition cond) {
    if (fd < 0) return NULL;
    GSource* source = g_source_new(&watch_funcs, sizeof(SshWatch));
    SshWatch* w = (SshWatch*)source;
    w->want = cond & (G_IO_IN | G_IO_OUT);
#ifdef G_OS_WIN32
    Waiter* t = g_new0(Waiter, 1);
    t->refs = 2; /* the source and the thread */
    t->sock = (SOCKET)fd;
    t->want = (gint)w->want;
    t->armed = TRUE;
    g_mutex_init(&t->mu);
    g_cond_init(&t->cond);
    w->waiter = t;
    GThread* thread = g_thread_try_new("ssh-watch", waiter_main, t, NULL);
    if (thread == NULL) {
        t->refs = 1;
        g_source_unref(source); /* finalize drops the last ref */
        return NULL;
    }
    g_thread_unref(thread);
#else
    w->pfd.fd = (gint)fd;
    w->pfd.events = (gushort)(w->want | G_IO_HUP | G_IO_ERR);
    g_source_add_poll(source, &w->pfd);
#endif
    g_source_set_name(source, "ssh_watch");
    return source;
}

void ssh_watch_set_condition(GSource* source, GIOCondition cond) {
    if (source == NULL) return;
    SshWatch* w = (SshWatch*)source;
    w->want = cond & (G_IO_IN | G_IO_OUT);
#ifdef G_OS_WIN32
    g_atomic_int_set(&w->waiter->want, (gint)w->want); /* prepare polls the new one at once */
#else
    w->pfd.events = (gushort)(w->want | G_IO_HUP | G_IO_ERR);
#endif
    GMainContext* ctx = g_source_get_context(source);
    if (ctx != NULL) g_main_context_wakeup(ctx); /* NULL: not attached yet */
}

GIOCondition ssh_watch_state(GSource* source) {
    return source != NULL ? ((SshWatch*)source)->state : 0;
}
