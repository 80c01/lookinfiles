/* Unit tests for src/core/ssh_watch.c: the source is ready while the socket
 * is readable / writable, and quiet once someone else drained it. */
#include "core/ssh_watch.h"

#include <gio/gio.h>

typedef struct {
    gint calls;
} Count;

static gboolean on_ready(gpointer data) {
    ((Count*)data)->calls++;
    return G_SOURCE_CONTINUE;
}

/* Run the context without blocking a few times; how many dispatches */
static gint spin(GMainContext* ctx, Count* c) {
    c->calls = 0;
    for (gint i = 0; i < 5; i++) g_main_context_iteration(ctx, FALSE);
    return c->calls;
}

static void pair(GSocket** a, GSocket** b) {
    GError* error = NULL;
    GSocket* listener = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, &error);
    g_assert_no_error(error);
    GInetAddress* lo = g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4);
    GSocketAddress* any = g_inet_socket_address_new(lo, 0);
    g_assert_true(g_socket_bind(listener, any, TRUE, &error));
    g_assert_true(g_socket_listen(listener, &error));
    GSocketAddress* bound = g_socket_get_local_address(listener, &error);
    *a = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, &error);
    g_assert_true(g_socket_connect(*a, bound, NULL, &error));
    *b = g_socket_accept(listener, NULL, &error);
    g_assert_no_error(error);
    g_object_unref(bound);
    g_object_unref(any);
    g_object_unref(lo);
    g_object_unref(listener);
}

static void test_readable_until_drained(void) {
    GSocket *a, *b;
    pair(&a, &b);
    GMainContext* ctx = g_main_context_new();
    Count c = {0};
    GSource* src = ssh_watch_new(g_socket_get_fd(b), G_IO_IN);
    g_assert_nonnull(src);
    g_source_set_callback(src, on_ready, &c, NULL);
    g_source_attach(src, ctx);

    g_assert_cmpint(spin(ctx, &c), ==, 0); /* nothing to read */
    g_assert_cmpint(g_socket_send(a, "hello", 5, NULL, NULL), ==, 5);
    g_usleep(20000);
    g_assert_cmpint(spin(ctx, &c), >, 0); /* data waiting */
    gchar buf[16];
    g_assert_cmpint(g_socket_receive(b, buf, sizeof buf, NULL, NULL), ==, 5); /* another reader drains it */
    g_assert_cmpint(spin(ctx, &c), ==, 0); /* quiet again: no busy loop */

    g_source_destroy(src);
    g_source_unref(src);
    g_main_context_unref(ctx);
    g_object_unref(a);
    g_object_unref(b);
}

static void test_writable_and_hangup(void) {
    GSocket *a, *b;
    pair(&a, &b);
    GMainContext* ctx = g_main_context_new();
    Count c = {0};
    GSource* src = ssh_watch_new(g_socket_get_fd(b), G_IO_OUT);
    g_source_set_callback(src, on_ready, &c, NULL);
    g_source_attach(src, ctx);
    g_assert_cmpint(spin(ctx, &c), >, 0); /* an idle socket is writable */
    g_source_destroy(src);
    g_source_unref(src);

    src = ssh_watch_new(g_socket_get_fd(b), G_IO_IN);
    g_source_set_callback(src, on_ready, &c, NULL);
    g_source_attach(src, ctx);
    g_socket_close(a, NULL); /* the peer goes away */
    g_usleep(20000);
    g_assert_cmpint(spin(ctx, &c), >, 0);
    g_source_destroy(src);
    g_source_unref(src);

    g_assert_true(g_socket_get_fd(b) >= 0); /* never closed by the watch */
    g_main_context_unref(ctx);
    g_object_unref(a);
    g_object_unref(b);
}

static void test_change_condition(void) {
    GSocket *a, *b;
    pair(&a, &b);
    GMainContext* ctx = g_main_context_new();
    Count c = {0};
    GSource* src = ssh_watch_new(g_socket_get_fd(b), G_IO_IN);
    g_source_set_callback(src, on_ready, &c, NULL);
    g_source_attach(src, ctx);
    g_assert_cmpint(spin(ctx, &c), ==, 0);              /* idle, nothing to read */
    ssh_watch_set_condition(src, G_IO_IN | G_IO_OUT);
    g_assert_cmpint(spin(ctx, &c), >, 0);               /* writable now asked */
    g_assert_true(ssh_watch_state(src) & G_IO_OUT);
    ssh_watch_set_condition(src, G_IO_IN);
    g_assert_cmpint(spin(ctx, &c), ==, 0);              /* quiet again */
    g_source_destroy(src);
    g_source_unref(src);
    g_main_context_unref(ctx);
    g_object_unref(a);
    g_object_unref(b);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ssh_watch/readable_until_drained", test_readable_until_drained);
    g_test_add_func("/ssh_watch/writable_and_hangup", test_writable_and_hangup);
    g_test_add_func("/ssh_watch/change_condition", test_change_condition);
    return g_test_run();
}
