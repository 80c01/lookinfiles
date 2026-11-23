/* Unit tests for src/core/ssh_channel.c: stdout/stderr/exit status, large
 * bidirectional echo, slow reader backpressure isolation across channels,
 * cancellation, worker-thread sync API, and PTY shell resize. */
#include "core/ssh_channel.h"
#include "core/ssh_link.h"

#include "ssh_test_server.h"

#include <glib/gstdio.h>
#include <string.h>

typedef struct {
    SshTestServer* srv;
    SshLink* link;
    gchar* tmp;
    gchar* kh;
} ChannelFixture;

static void on_ask_auto(SshLinkAsk* ask, SshLinkAskKind kind, const gchar* prompt, gpointer data) {
    (void)prompt;
    (void)data;
    if (kind == SSH_LINK_ASK_NOTICE) return;
    ssh_link_reply(ask, kind == SSH_LINK_ASK_CONFIRM ? "yes" : "chan-pw");
}

typedef struct {
    gboolean done;
    gboolean ok;
    GMainLoop* loop;
} ReadyWait;

static void on_ready_auto(SshLink* link, gboolean ok, const gchar* err, gpointer data) {
    (void)link;
    (void)err;
    ReadyWait* w = data;
    w->done = TRUE;
    w->ok = ok;
    g_main_loop_quit(w->loop);
}

static void on_done(GObject* src, GAsyncResult* res, gpointer data) {
    (void)src;
    *(GAsyncResult**)data = g_object_ref(res);
}

static GAsyncResult* wait_res(GAsyncResult** slot) {
    while (*slot == NULL) g_main_context_iteration(NULL, TRUE);
    GAsyncResult* r = *slot;
    *slot = NULL;
    return r;
}

static void rm_rf(const gchar* path) {
    const gchar* argv[] = { "rm", "-rf", path, NULL };
    g_spawn_sync(NULL, (gchar**)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, NULL);
}

static ChannelFixture* fixture_new(void) {
    ChannelFixture* f = g_new0(ChannelFixture, 1);
    f->tmp = g_dir_make_tmp("ssh-chan-XXXXXX", NULL);
    f->kh = g_build_filename(f->tmp, "known_hosts", NULL);
    ssh_link_set_test_options("", f->kh, FALSE, FALSE);
    ssh_link_set_keepalive_ms(0);
    f->srv = ssh_test_server_start("tester", "chan-pw");
    SshTarget t = { .host = "127.0.0.1", .user = "tester", .port = f->srv->port };
    ReadyWait w = { .loop = g_main_loop_new(NULL, FALSE) };
    f->link = ssh_link_connect(&t, on_ask_auto, NULL, on_ready_auto, &w);
    while (!w.done) g_main_loop_run(w.loop);
    g_main_loop_unref(w.loop);
    g_assert_true(w.ok);
    return f;
}

static void fixture_free(ChannelFixture* f) {
    ssh_link_close(f->link);
    ssh_link_unref(f->link);
    ssh_test_server_stop(f->srv);
    ssh_link_set_test_options(NULL, NULL, TRUE, TRUE);
    rm_rf(f->tmp);
    g_free(f->kh);
    g_free(f->tmp);
    g_free(f);
}

static SshChannel* open_exec(SshLink* link, const gchar* cmd) {
    GAsyncResult* slot = NULL;
    ssh_link_open_channel_async(link, SSH_CHANNEL_KIND_EXEC, cmd, 0, 0, NULL, on_done, &slot);
    GAsyncResult* r = wait_res(&slot);
    GError* err = NULL;
    SshChannel* ch = ssh_link_open_channel_finish(link, r, &err);
    g_object_unref(r);
    g_assert_no_error(err);
    g_assert_nonnull(ch);
    return ch;
}

/* Read an entire GInputStream asynchronously until EOF */
typedef struct {
    GInputStream* in;
    GByteArray* buf;
    guint8 chunk[16384];
    gboolean done;
    GError* err;
} DrainState;

static void drain_next(DrainState* d);

static void on_drain_chunk(GObject* src, GAsyncResult* res, gpointer data) {
    DrainState* d = data;
    gssize n = g_input_stream_read_finish(G_INPUT_STREAM(src), res, &d->err);
    if (n > 0) {
        g_byte_array_append(d->buf, d->chunk, (guint)n);
        drain_next(d);
    } else {
        d->done = TRUE;
    }
}

static void drain_next(DrainState* d) {
    g_input_stream_read_async(d->in, d->chunk, sizeof d->chunk, G_PRIORITY_DEFAULT, NULL, on_drain_chunk, d);
}

/* ------------------------------------------------------------------------- */
/* Tests                                                                     */
/* ------------------------------------------------------------------------- */

static void test_exec_stdout_stderr_exit(void) {
    ChannelFixture* f = fixture_new();

    /* "hello" -> stdout, stderr, exit 0 */
    SshChannel* ch = open_exec(f->link, "hello");
    DrainState out = { .in = g_io_stream_get_input_stream(G_IO_STREAM(ch)), .buf = g_byte_array_new() };
    DrainState err_d = { .in = ssh_channel_get_stderr(ch), .buf = g_byte_array_new() };
    GAsyncResult* exit_slot = NULL;
    drain_next(&out);
    drain_next(&err_d);
    ssh_channel_wait_exit_async(ch, NULL, on_done, &exit_slot);
    while (!out.done || !err_d.done || exit_slot == NULL) g_main_context_iteration(NULL, TRUE);
    g_assert_no_error(out.err);
    g_assert_no_error(err_d.err);
    g_assert_cmpmem(out.buf->data, out.buf->len, "hello stdout\n", 13);
    g_assert_cmpmem(err_d.buf->data, err_d.buf->len, "hello stderr\n", 13);
    GError* err = NULL;
    gint status = ssh_channel_wait_exit_finish(ch, exit_slot, &err);
    g_object_unref(exit_slot);
    g_assert_no_error(err);
    g_assert_cmpint(status, ==, 0);
    gint st2 = -1;
    g_assert_true(ssh_channel_exit_status(ch, &st2));
    g_assert_cmpint(st2, ==, 0);
    g_byte_array_unref(out.buf);
    g_byte_array_unref(err_d.buf);
    g_object_unref(ch);

    /* "exit42" -> stdout "bye", exit 42 */
    ch = open_exec(f->link, "exit42");
    DrainState out2 = { .in = g_io_stream_get_input_stream(G_IO_STREAM(ch)), .buf = g_byte_array_new() };
    exit_slot = NULL;
    drain_next(&out2);
    ssh_channel_wait_exit_async(ch, NULL, on_done, &exit_slot);
    while (!out2.done || exit_slot == NULL) g_main_context_iteration(NULL, TRUE);
    g_assert_no_error(out2.err);
    g_assert_cmpmem(out2.buf->data, out2.buf->len, "bye", 3);
    status = ssh_channel_wait_exit_finish(ch, exit_slot, &err);
    g_object_unref(exit_slot);
    g_assert_no_error(err);
    g_assert_cmpint(status, ==, 42);
    g_byte_array_unref(out2.buf);
    g_object_unref(ch);

    fixture_free(f);
}

typedef struct {
    GOutputStream* out;
    const guint8* data;
    gsize len;
    gsize off;
    gboolean done;
    GError* err;
} WriteAllState;

static void write_next(WriteAllState* w);

static void on_close_stdin(GObject* src, GAsyncResult* res, gpointer data) {
    WriteAllState* w = data;
    g_output_stream_close_finish(G_OUTPUT_STREAM(src), res, &w->err);
    w->done = TRUE;
}

static void on_write_chunk(GObject* src, GAsyncResult* res, gpointer data) {
    WriteAllState* w = data;
    gssize n = g_output_stream_write_finish(G_OUTPUT_STREAM(src), res, &w->err);
    if (n < 0) {
        w->done = TRUE;
        return;
    }
    w->off += (gsize)n;
    write_next(w);
}

static void write_next(WriteAllState* w) {
    if (w->off >= w->len) {
        g_output_stream_close_async(w->out, G_PRIORITY_DEFAULT, NULL, on_close_stdin, w);
        return;
    }
    g_output_stream_write_async(w->out, w->data + w->off, w->len - w->off, G_PRIORITY_DEFAULT, NULL,
                                on_write_chunk, w);
}

static void test_stdin_echo_large(void) {
    ChannelFixture* f = fixture_new();
    SshChannel* ch = open_exec(f->link, "echo");

    gsize total = 256 * 1024 + 37;
    guint8* payload = g_malloc(total);
    for (gsize i = 0; i < total; i++) payload[i] = (guint8)((i * 29 + i / 131) & 0xff);

    WriteAllState wr = { .out = g_io_stream_get_output_stream(G_IO_STREAM(ch)), .data = payload, .len = total };
    DrainState rd = { .in = g_io_stream_get_input_stream(G_IO_STREAM(ch)), .buf = g_byte_array_new() };
    GAsyncResult* exit_slot = NULL;
    write_next(&wr);
    drain_next(&rd);
    ssh_channel_wait_exit_async(ch, NULL, on_done, &exit_slot);

    while (!wr.done || !rd.done || exit_slot == NULL) g_main_context_iteration(NULL, TRUE);
    g_assert_no_error(wr.err);
    g_assert_no_error(rd.err);
    g_assert_cmpmem(rd.buf->data, rd.buf->len, payload, total);
    GError* err = NULL;
    gint status = ssh_channel_wait_exit_finish(ch, exit_slot, &err);
    g_object_unref(exit_slot);
    g_assert_no_error(err);
    g_assert_cmpint(status, ==, 0);

    g_byte_array_unref(rd.buf);
    g_free(payload);
    g_object_unref(ch);
    fixture_free(f);
}

/* Channel A receives 2 MiB while nobody reads it (slow reader); Channel B
 * echoes 128 KiB and Channel C runs "hello" without stalling. Then Channel A
 * drains all 2 MiB cleanly. */
static void test_slow_reader_does_not_stall_peer(void) {
    ChannelFixture* f = fixture_new();

    SshChannel* slow = open_exec(f->link, "flood");
    /* Let the server fill slow's SSH receive window completely */
    gint64 wait_until = g_get_monotonic_time() + 150 * 1000;
    while (g_get_monotonic_time() < wait_until) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(5 * 1000);
    }

    /* Channel B: 128 KiB round-trip echo must complete cleanly while slow sits unread */
    SshChannel* fast = open_exec(f->link, "echo");
    gsize fast_len = 128 * 1024 + 5;
    guint8* fast_data = g_malloc(fast_len);
    for (gsize i = 0; i < fast_len; i++) fast_data[i] = (guint8)((i * 5 + 1) & 0xff);

    WriteAllState wr = { .out = g_io_stream_get_output_stream(G_IO_STREAM(fast)), .data = fast_data, .len = fast_len };
    DrainState rd = { .in = g_io_stream_get_input_stream(G_IO_STREAM(fast)), .buf = g_byte_array_new() };
    write_next(&wr);
    drain_next(&rd);
    while (!wr.done || !rd.done) g_main_context_iteration(NULL, TRUE);
    g_assert_no_error(wr.err);
    g_assert_no_error(rd.err);
    g_assert_cmpmem(rd.buf->data, rd.buf->len, fast_data, fast_len);
    g_byte_array_unref(rd.buf);
    g_free(fast_data);
    g_object_unref(fast);

    /* Channel C: "hello" also completes while slow is still unread */
    SshChannel* hello = open_exec(f->link, "hello");
    DrainState hrd = { .in = g_io_stream_get_input_stream(G_IO_STREAM(hello)), .buf = g_byte_array_new() };
    drain_next(&hrd);
    while (!hrd.done) g_main_context_iteration(NULL, TRUE);
    g_assert_no_error(hrd.err);
    g_assert_cmpmem(hrd.buf->data, hrd.buf->len, "hello stdout\n", 13);
    g_byte_array_unref(hrd.buf);
    g_object_unref(hello);

    /* Now drain the slow channel: every byte of the 2 MiB flood must arrive intact */
    DrainState srd = { .in = g_io_stream_get_input_stream(G_IO_STREAM(slow)), .buf = g_byte_array_new() };
    drain_next(&srd);
    while (!srd.done) g_main_context_iteration(NULL, TRUE);
    g_assert_no_error(srd.err);
    g_assert_cmpuint(srd.buf->len, ==, FLOOD_TOTAL);
    for (guint i = 0; i < FLOOD_TOTAL; i++) {
        if (srd.buf->data[i] != flood_byte(i)) {
            g_assert_cmpuint(srd.buf->data[i], ==, flood_byte(i));
        }
    }
    g_byte_array_unref(srd.buf);
    g_object_unref(slow);
    fixture_free(f);
}

static void test_cancel_read(void) {
    ChannelFixture* f = fixture_new();
    SshChannel* ch = open_exec(f->link, "hang");
    GCancellable* cancel = g_cancellable_new();
    guint8 buf[32];
    GAsyncResult* slot = NULL;
    g_input_stream_read_async(g_io_stream_get_input_stream(G_IO_STREAM(ch)), buf, sizeof buf,
                              G_PRIORITY_DEFAULT, cancel, on_done, &slot);
    g_cancellable_cancel(cancel);
    GAsyncResult* r = wait_res(&slot);
    GError* err = NULL;
    gssize n = g_input_stream_read_finish(g_io_stream_get_input_stream(G_IO_STREAM(ch)), r, &err);
    g_object_unref(r);
    g_assert_cmpint(n, ==, -1);
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_clear_error(&err);
    g_object_unref(cancel);
    g_object_unref(ch);

    /* Link stays healthy after channel cancellation */
    SshChannel* ch2 = open_exec(f->link, "hello");
    DrainState rd = { .in = g_io_stream_get_input_stream(G_IO_STREAM(ch2)), .buf = g_byte_array_new() };
    drain_next(&rd);
    while (!rd.done) g_main_context_iteration(NULL, TRUE);
    g_assert_no_error(rd.err);
    g_assert_cmpmem(rd.buf->data, rd.buf->len, "hello stdout\n", 13);
    g_byte_array_unref(rd.buf);
    g_object_unref(ch2);
    fixture_free(f);
}

static gpointer worker_sync_main(gpointer data) {
    SshLink* link = data;
    GError* err = NULL;
    SshChannel* ch = ssh_link_open_channel_sync(link, SSH_CHANNEL_KIND_EXEC, "echo", 0, 0, NULL, &err);
    g_assert_no_error(err);
    g_assert_nonnull(ch);

    const gchar* msg = "sync-worker-payload";
    gsize written = 0;
    gboolean ok = g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(ch)), msg, strlen(msg),
                                            &written, NULL, &err);
    g_assert_no_error(err);
    g_assert_true(ok);
    g_assert_cmpuint(written, ==, strlen(msg));
    ok = g_output_stream_close(g_io_stream_get_output_stream(G_IO_STREAM(ch)), NULL, &err);
    g_assert_no_error(err);
    g_assert_true(ok);

    gchar buf[64] = { 0 };
    gsize got = 0;
    ok = g_input_stream_read_all(g_io_stream_get_input_stream(G_IO_STREAM(ch)), buf, sizeof buf - 1, &got, NULL, &err);
    g_assert_no_error(err);
    g_assert_true(ok);
    g_assert_cmpuint(got, ==, strlen(msg));
    g_assert_cmpstr(buf, ==, msg);

    gint status = -1;
    ok = ssh_channel_wait_exit_sync(ch, NULL, &status, &err);
    g_assert_no_error(err);
    g_assert_true(ok);
    g_assert_cmpint(status, ==, 0);

    g_object_unref(ch);
    return NULL;
}

static void test_sync_worker_thread(void) {
    ChannelFixture* f = fixture_new();
    GThread* worker = g_thread_new("sync-worker", worker_sync_main, f->link);
    g_thread_join(worker);
    fixture_free(f);
}

static void test_shell_pty_resize(void) {
    ChannelFixture* f = fixture_new();
    GAsyncResult* slot = NULL;
    ssh_link_open_channel_async(f->link, SSH_CHANNEL_KIND_SHELL, "xterm-256color", 80, 24, NULL, on_done, &slot);
    GAsyncResult* r = wait_res(&slot);
    GError* err = NULL;
    SshChannel* ch = ssh_link_open_channel_finish(f->link, r, &err);
    g_object_unref(r);
    g_assert_no_error(err);

    gchar buf[64] = { 0 };
    g_input_stream_read_all_async(g_io_stream_get_input_stream(G_IO_STREAM(ch)), buf, 10,
                                  G_PRIORITY_DEFAULT, NULL, on_done, &slot);
    r = wait_res(&slot);
    gsize n = 0;
    g_assert_true(g_input_stream_read_all_finish(g_io_stream_get_input_stream(G_IO_STREAM(ch)), r, &n, &err));
    g_object_unref(r);
    g_assert_no_error(err);
    g_assert_cmpstr(buf, ==, "pty:80x24\n");

    ssh_channel_resize_pty(ch, 120, 40);
    memset(buf, 0, sizeof buf);
    g_input_stream_read_all_async(g_io_stream_get_input_stream(G_IO_STREAM(ch)), buf, 14,
                                  G_PRIORITY_DEFAULT, NULL, on_done, &slot);
    r = wait_res(&slot);
    g_assert_true(g_input_stream_read_all_finish(g_io_stream_get_input_stream(G_IO_STREAM(ch)), r, &n, &err));
    g_object_unref(r);
    g_assert_no_error(err);
    g_assert_cmpstr(buf, ==, "resize:120x40\n");

    ssh_channel_force_close(ch);
    g_object_unref(ch);
    fixture_free(f);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ssh_channel/exec_stdout_stderr_exit", test_exec_stdout_stderr_exit);
    g_test_add_func("/ssh_channel/stdin_echo_large", test_stdin_echo_large);
    g_test_add_func("/ssh_channel/slow_reader_does_not_stall_peer", test_slow_reader_does_not_stall_peer);
    g_test_add_func("/ssh_channel/cancel_read", test_cancel_read);
    g_test_add_func("/ssh_channel/sync_worker_thread", test_sync_worker_thread);
    g_test_add_func("/ssh_channel/shell_pty_resize", test_shell_pty_resize);
    return g_test_run();
}
