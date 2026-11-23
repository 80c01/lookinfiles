/* Level-triggered socket source (see ssh_watch.h). */
#ifdef G_OS_WIN32
#include <winsock2.h> /* before any header that may pull windows.h */
#endif
#include "core/ssh_watch.h"

typedef struct {
    GSource source;
    GPollFD pfd;
    GIOCondition want;
#ifdef G_OS_WIN32
    SOCKET sock;
    WSAEVENT event;
#endif
} SshWatch;

#ifdef G_OS_WIN32
/* socket -> event of its latest watch. A watch replaced from its own
 * callback is finalized after the new one registered: it must not undo
 * that registration. */
static GMutex current_mu;
static GHashTable* current;

static void set_current(SOCKET sock, WSAEVENT event) {
    g_mutex_lock(&current_mu);
    if (current == NULL) current = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, NULL);
    gint64* key = g_new(gint64, 1);
    *key = (gint64)sock;
    g_hash_table_replace(current, key, event);
    g_mutex_unlock(&current_mu);
}

/* Forget sock if event is still its latest: whether to unregister */
static gboolean drop_current(SOCKET sock, WSAEVENT event) {
    gint64 key = (gint64)sock;
    gboolean latest = FALSE;
    g_mutex_lock(&current_mu);
    if (current != NULL && g_hash_table_lookup(current, &key) == event) {
        g_hash_table_remove(current, &key);
        latest = TRUE;
    }
    g_mutex_unlock(&current_mu);
    return latest;
}

/* The socket's state now (WSAPoll() does not wait with 0) */
static GIOCondition socket_state(SshWatch* w) {
    WSAPOLLFD p = {.fd = w->sock, .events = 0, .revents = 0};
    if (w->want & G_IO_IN) p.events |= POLLRDNORM;
    if (w->want & G_IO_OUT) p.events |= POLLWRNORM;
    if (WSAPoll(&p, 1, 0) <= 0) return 0;
    GIOCondition c = 0;
    if (p.revents & POLLRDNORM) c |= G_IO_IN;
    if (p.revents & POLLWRNORM) c |= G_IO_OUT;
    if (p.revents & POLLHUP) c |= G_IO_HUP;
    if (p.revents & (POLLERR | POLLNVAL)) c |= G_IO_ERR;
    return c;
}

static gboolean watch_prepare(GSource* source, gint* timeout) {
    *timeout = -1;
    return socket_state((SshWatch*)source) != 0;
}

static gboolean watch_check(GSource* source) {
    SshWatch* w = (SshWatch*)source;
    WSANETWORKEVENTS events;
    WSAEnumNetworkEvents(w->sock, w->event, &events); /* resets the event */
    return socket_state(w) != 0;
}

static void watch_finalize(GSource* source) {
    SshWatch* w = (SshWatch*)source;
    if (drop_current(w->sock, w->event)) WSAEventSelect(w->sock, NULL, 0); /* stays non-blocking */
    WSACloseEvent(w->event);
}
#else
static gboolean watch_prepare(GSource* source, gint* timeout) {
    (void)source;
    *timeout = -1;
    return FALSE;
}

static gboolean watch_check(GSource* source) {
    return ((SshWatch*)source)->pfd.revents != 0;
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
#ifdef G_OS_WIN32
    WSAEVENT event = WSACreateEvent();
    if (event == WSA_INVALID_EVENT) return NULL;
    long mask = FD_CLOSE | (cond & G_IO_IN ? FD_READ : 0) | (cond & G_IO_OUT ? FD_WRITE | FD_CONNECT : 0);
    if (WSAEventSelect((SOCKET)fd, event, mask) != 0) {
        WSACloseEvent(event);
        return NULL;
    }
    set_current((SOCKET)fd, event);
#endif
    GSource* source = g_source_new(&watch_funcs, sizeof(SshWatch));
    SshWatch* w = (SshWatch*)source;
    w->want = cond & (G_IO_IN | G_IO_OUT);
#ifdef G_OS_WIN32
    w->sock = (SOCKET)fd;
    w->event = event;
    w->pfd.fd = (gintptr)event; /* GLib waits on the event handle */
    w->pfd.events = G_IO_IN;
#else
    w->pfd.fd = (gint)fd;
    w->pfd.events = (gushort)(w->want | G_IO_HUP | G_IO_ERR);
#endif
    g_source_add_poll(source, &w->pfd);
    g_source_set_name(source, "ssh_watch");
    return source;
}
