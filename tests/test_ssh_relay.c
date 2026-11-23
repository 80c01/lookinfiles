/* Unit tests for src/core/ssh_relay.c and src/core/ssh_relay_helper.c
 * against the in-process libssh test server (tests/ssh_test_server.h).
 * Also runs this test binary itself in --ssh-channel helper mode so every
 * end-to-end test exercises real subprocess spawning, ticket file handling,
 * framing, flow control, and exit status propagation. */
#include "core/app_paths.h"
#include "core/ssh_link.h"
#include "core/ssh_relay.h"
#include "core/ssh_session.h"

#include "ssh_test_server.h"

#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>

static gchar* self_exe;
static gchar* test_runtime_root;

typedef struct {
    gboolean accept_hostkey;
    const gchar* secret;
} AskCtx;

static void on_ask(SshLinkAsk* ask, SshLinkAskKind kind, const gchar* prompt, gpointer data) {
    (void)prompt;
    AskCtx* ctx = data;
    if (kind == SSH_LINK_ASK_NOTICE) return;
    if (kind == SSH_LINK_ASK_CONFIRM) {
        ssh_link_reply(ask, ctx->accept_hostkey ? "yes" : NULL);
    } else {
        ssh_link_reply(ask, ctx->secret);
    }
}

typedef struct {
    gboolean done;
    gboolean ok;
    GMainLoop* loop;
} ConnectWait;

static void on_ready(SshLink* link, gboolean ok, const gchar* error_text, gpointer data) {
    (void)link;
    (void)error_text;
    ConnectWait* w = data;
    w->done = TRUE;
    w->ok = ok;
    g_main_loop_quit(w->loop);
}

static SshLink* connect_link(const SshTarget* t, const gchar* secret) {
    AskCtx ask = { .accept_hostkey = TRUE, .secret = secret };
    ConnectWait w = { .loop = g_main_loop_new(NULL, FALSE) };
    SshLink* link = ssh_link_connect(t, on_ask, &ask, on_ready, &w);
    while (!w.done) g_main_loop_run(w.loop);
    g_main_loop_unref(w.loop);
    g_assert_true(w.ok);
    return link;
}

static void rm_rf(const gchar* path) {
    const gchar* argv[] = { "rm", "-rf", path, NULL };
    g_spawn_sync(NULL, (gchar**)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, NULL);
}

static guint32 read_be32(const guint8* p) {
    return ((guint32)p[0] << 24) | ((guint32)p[1] << 16) | ((guint32)p[2] << 8) | (guint32)p[3];
}

static void write_be16(guint8* p, guint16 v) {
    p[0] = (guint8)((v >> 8) & 0xffu);
    p[1] = (guint8)(v & 0xffu);
}

static void write_be32(guint8* p, guint32 v) {
    p[0] = (guint8)((v >> 24) & 0xffu);
    p[1] = (guint8)((v >> 16) & 0xffu);
    p[2] = (guint8)((v >> 8) & 0xffu);
    p[3] = (guint8)(v & 0xffu);
}

static gboolean has_arg(GPtrArray* argv, const gchar* arg) {
    for (guint i = 0; i < argv->len; i++) {
        if (g_strcmp0(argv->pdata[i], arg) == 0) return TRUE;
    }
    return FALSE;
}

typedef struct {
    gchar* tmp;
    gchar* kh;
    SshTestServer* srv;
    SshTarget target;
    SshLink* link;
} Fixture;

static void fixture_setup(Fixture* f, const gchar* user, const gchar* pw) {
    memset(f, 0, sizeof *f);
    f->tmp = g_dir_make_tmp("ssh-relay-test-XXXXXX", NULL);
    f->kh = g_build_filename(f->tmp, "known_hosts", NULL);

    ssh_link_set_test_options("", f->kh, FALSE, FALSE);
    ssh_link_set_keepalive_ms(0);
    ssh_relay_set_ticket_ttl_ms(0);
    ssh_relay_set_program(self_exe);
    GError* err = NULL;
    g_assert_true(ssh_relay_serve(&err));
    g_assert_no_error(err);

    f->srv = ssh_test_server_start(user, pw);
    f->target.host = "127.0.0.1";
    f->target.user = user;
    f->target.port = f->srv->port;
    f->link = connect_link(&f->target, pw);
}

static void fixture_teardown(Fixture* f) {
    ssh_relay_stop();
    ssh_relay_set_program(NULL);
    ssh_relay_set_ticket_ttl_ms(0);
    if (f->link != NULL) {
        ssh_link_close(f->link);
        ssh_link_unref(f->link);
    }
    if (f->srv != NULL) ssh_test_server_stop(f->srv);
    ssh_link_set_test_options(NULL, NULL, TRUE, TRUE);
    rm_rf(f->tmp);
    g_free(f->kh);
    g_free(f->tmp);
}

/* Spawn a relay command via ssh_session_argv(), communicate input, return status/out/err */
static gint run_relay_cmd(const SshTarget* t, const gchar* const* extra_args, GBytes* input,
                          GBytes** out_stdout, GBytes** out_stderr) {
    GPtrArray* argv = ssh_session_argv(t, 0);
    g_assert_cmpuint(argv->len, ==, 4);
    g_assert_cmpstr(argv->pdata[0], ==, self_exe);
    g_assert_cmpstr(argv->pdata[1], ==, "--ssh-channel");
    for (gsize i = 0; extra_args != NULL && extra_args[i] != NULL; i++) {
        g_ptr_array_add(argv, g_strdup(extra_args[i]));
    }
    g_ptr_array_add(argv, NULL);

    GError* err = NULL;
    GSubprocessLauncher* l = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDIN_PIPE |
                                                       G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                                       G_SUBPROCESS_FLAGS_STDERR_PIPE);
    GSubprocess* proc = g_subprocess_launcher_spawnv(l, (const gchar* const*)argv->pdata, &err);
    g_object_unref(l);
    g_ptr_array_unref(argv);
    g_assert_no_error(err);
    g_assert_nonnull(proc);

    GBytes* in = input != NULL ? g_bytes_ref(input) : g_bytes_new(NULL, 0);
    gboolean ok = g_subprocess_communicate(proc, in, NULL, out_stdout, out_stderr, &err);
    g_bytes_unref(in);
    g_assert_no_error(err);
    g_assert_true(ok);
    g_assert_true(g_subprocess_get_if_exited(proc));
    gint status = g_subprocess_get_exit_status(proc);
    g_object_unref(proc);
    return status;
}

/* ------------------------------------------------------------------------- */
/* Tests                                                                     */
/* ------------------------------------------------------------------------- */

static void test_is_helper(void) {
    char* ok1[] = { (char*)"lookinfiles", (char*)"--ssh-channel", (char*)"1234", (char*)"1-1", NULL };
    char* ok2[] = { (char*)"lookinfiles", (char*)"--ssh-channel", (char*)"1234", (char*)"1-1", (char*)"ls", NULL };
    char* bad1[] = { (char*)"lookinfiles", NULL };
    char* bad2[] = { (char*)"lookinfiles", (char*)"--ssh-channel", (char*)"1234", NULL };
    char* bad3[] = { (char*)"lookinfiles", (char*)"--version", (char*)"1234", (char*)"1-1", NULL };
    g_assert_true(ssh_relay_is_helper(4, ok1));
    g_assert_true(ssh_relay_is_helper(5, ok2));
    g_assert_false(ssh_relay_is_helper(1, bad1));
    g_assert_false(ssh_relay_is_helper(3, bad2));
    g_assert_false(ssh_relay_is_helper(4, bad3));
}

static void test_roundtrip_status_stderr_and_ticket_file(void) {
    Fixture f;
    fixture_setup(&f, "alice", "pw1");

    /* Verify ticket file permissions (0600) before spawn and deletion after use */
    GPtrArray* argv = ssh_session_argv(&f.target, 0);
    g_assert_cmpuint(argv->len, ==, 4);
    gchar* rdir = app_runtime_dir();
    gchar* tpath = g_build_filename(rdir, "relay", (const gchar*)argv->pdata[3], NULL);
    g_free(rdir);
    struct stat st;
    g_assert_cmpint(stat(tpath, &st), ==, 0);
    g_assert_cmpuint(st.st_mode & 0777u, ==, 0600u);

    g_ptr_array_add(argv, g_strdup("hello"));
    g_ptr_array_add(argv, NULL);
    GError* err = NULL;
    GSubprocessLauncher* l = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDIN_PIPE |
                                                       G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                                       G_SUBPROCESS_FLAGS_STDERR_PIPE);
    GSubprocess* proc = g_subprocess_launcher_spawnv(l, (const gchar* const*)argv->pdata, &err);
    g_object_unref(l);
    g_ptr_array_unref(argv);
    g_assert_no_error(err);

    GBytes* out = NULL;
    GBytes* serr = NULL;
    GBytes* empty = g_bytes_new(NULL, 0);
    g_assert_true(g_subprocess_communicate(proc, empty, NULL, &out, &serr, &err));
    g_bytes_unref(empty);
    g_assert_no_error(err);
    g_assert_cmpint(g_subprocess_get_exit_status(proc), ==, 0);
    g_object_unref(proc);

    /* Stdout and stderr kept apart */
    gsize on = 0, en = 0;
    const gchar* od = g_bytes_get_data(out, &on);
    const gchar* ed = g_bytes_get_data(serr, &en);
    g_assert_cmpmem(od, on, "hello stdout\n", 13);
    g_assert_cmpmem(ed, en, "hello stderr\n", 13);
    g_bytes_unref(out);
    g_bytes_unref(serr);

    /* Ticket file deleted after use */
    g_assert_false(g_file_test(tpath, G_FILE_TEST_EXISTS));
    g_free(tpath);

    /* Non-zero exit status propagated */
    const gchar* exit_cmd[] = { "exit42", NULL };
    gint st42 = run_relay_cmd(&f.target, exit_cmd, NULL, &out, &serr);
    g_assert_cmpint(st42, ==, 42);
    od = g_bytes_get_data(out, &on);
    g_assert_cmpmem(od, on, "bye", 3);
    g_assert_cmpuint(g_bytes_get_size(serr), ==, 0);
    g_bytes_unref(out);
    g_bytes_unref(serr);

    /* Bidirectional multi-frame binary echo (> 64 KiB, with NUL bytes) */
    gsize payload_len = 80 * 1024;
    guint8* payload = g_malloc(payload_len);
    for (gsize i = 0; i < payload_len; i++) payload[i] = (guint8)(i % 251);
    GBytes* in = g_bytes_new_take(payload, payload_len);
    const gchar* echo_cmd[] = { "echo", NULL };
    gint est = run_relay_cmd(&f.target, echo_cmd, in, &out, &serr);
    g_assert_cmpint(est, ==, 0);
    g_assert_cmpuint(g_bytes_get_size(serr), ==, 0);
    g_assert_cmpmem(g_bytes_get_data(out, &on), on, g_bytes_get_data(in, NULL), payload_len);
    g_bytes_unref(in);
    g_bytes_unref(out);
    g_bytes_unref(serr);

    /* Multi-line & multi-word command (like ipynb_bridge.py + --list) */
    const gchar* multi_cmd[] = { "echo-cmd:line1\nline2 'q' \"dq\" \\bs", "--list", NULL };
    gint mst = run_relay_cmd(&f.target, multi_cmd, NULL, &out, &serr);
    g_assert_cmpint(mst, ==, 0);
    const gchar* expected = "line1\nline2 'q' \"dq\" \\bs --list";
    g_assert_cmpmem(g_bytes_get_data(out, &on), on, expected, strlen(expected));
    g_bytes_unref(out);
    g_bytes_unref(serr);

    fixture_teardown(&f);
}

static gchar* raw_handshake(guint16 port, const gchar* text, GSocketConnection** out_conn,
                            GDataInputStream** out_in) {
    GSocketClient* client = g_socket_client_new();
    g_socket_client_set_enable_proxy(client, FALSE);
    GSocketConnection* conn = g_socket_client_connect_to_host(client, "127.0.0.1", port, NULL, NULL);
    g_object_unref(client);
    g_assert_nonnull(conn);
    GOutputStream* out = g_io_stream_get_output_stream(G_IO_STREAM(conn));
    g_assert_true(g_output_stream_write_all(out, text, strlen(text), NULL, NULL, NULL));
    GDataInputStream* in = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(conn)));
    g_data_input_stream_set_newline_type(in, G_DATA_STREAM_NEWLINE_TYPE_LF);
    gchar* line = g_data_input_stream_read_line(in, NULL, NULL, NULL);
    if (out_conn != NULL && out_in != NULL) {
        *out_conn = conn;
        *out_in = in;
    } else {
        g_object_unref(in);
        g_io_stream_close(G_IO_STREAM(conn), NULL, NULL);
        g_object_unref(conn);
    }
    return line;
}

static void test_ticket_refusals_and_cleanup(void) {
    Fixture f;
    fixture_setup(&f, "bob", "pw2");
    guint16 port = ssh_relay_port();
    g_assert_cmpuint(port, >, 0);

    /* 1. Wrong protocol line */
    gchar* r = raw_handshake(port, "NOT-RELAY 99\n", NULL, NULL);
    g_assert_nonnull(r);
    g_assert_true(g_str_has_prefix(r, "ERR "));
    g_free(r);

    /* 2. Wrong ticket */
    r = raw_handshake(port, "LOOKIN-RELAY 1\n00000000000000000000000000000000\n0 0\nhello\n", NULL, NULL);
    g_assert_nonnull(r);
    g_assert_true(g_str_has_prefix(r, "ERR "));
    g_free(r);

    /* 3. Ticket used twice: first OK, second ERR; ticket file deleted by instance */
    GPtrArray* argv = ssh_relay_argv(f.link, FALSE);
    g_assert_nonnull(argv);
    gchar* rdir = app_runtime_dir();
    gchar* tpath = g_build_filename(rdir, "relay", (const gchar*)argv->pdata[3], NULL);
    gchar* secret = NULL;
    g_assert_true(g_file_get_contents(tpath, &secret, NULL, NULL));
    g_strstrip(secret);
    g_ptr_array_unref(argv);

    gchar* hs = g_strdup_printf("LOOKIN-RELAY 1\n%s\n0 0\nhello\n", secret);
    r = raw_handshake(port, hs, NULL, NULL);
    g_assert_cmpstr(r, ==, "OK");
    g_free(r);
    g_assert_false(g_file_test(tpath, G_FILE_TEST_EXISTS));

    /* Replay of the same ticket must be refused */
    r = raw_handshake(port, hs, NULL, NULL);
    g_assert_nonnull(r);
    g_assert_true(g_str_has_prefix(r, "ERR "));
    g_free(r);
    g_free(hs);
    g_free(secret);
    g_free(tpath);

    /* 4. Expired ticket refused and file removed */
    ssh_relay_set_ticket_ttl_ms(40);
    argv = ssh_relay_argv(f.link, FALSE);
    g_assert_nonnull(argv);
    tpath = g_build_filename(rdir, "relay", (const gchar*)argv->pdata[3], NULL);
    g_assert_true(g_file_get_contents(tpath, &secret, NULL, NULL));
    g_strstrip(secret);
    g_ptr_array_unref(argv);

    g_usleep(80 * 1000);
    hs = g_strdup_printf("LOOKIN-RELAY 1\n%s\n0 0\nhello\n", secret);
    r = raw_handshake(port, hs, NULL, NULL);
    g_assert_nonnull(r);
    g_assert_true(g_str_has_prefix(r, "ERR "));
    g_free(r);
    g_assert_false(g_file_test(tpath, G_FILE_TEST_EXISTS));
    g_free(hs);
    g_free(secret);
    g_free(tpath);

    /* 5. Unused ticket file cleaned up at ssh_relay_stop() */
    ssh_relay_set_ticket_ttl_ms(0);
    argv = ssh_relay_argv(f.link, FALSE);
    g_assert_nonnull(argv);
    tpath = g_build_filename(rdir, "relay", (const gchar*)argv->pdata[3], NULL);
    g_ptr_array_unref(argv);
    g_assert_true(g_file_test(tpath, G_FILE_TEST_EXISTS));
    ssh_relay_stop();
    g_assert_false(g_file_test(tpath, G_FILE_TEST_EXISTS));
    g_free(tpath);
    g_free(rdir);

    fixture_teardown(&f);
}

static void test_pty_shell_and_resize(void) {
    Fixture f;
    fixture_setup(&f, "carol", "pw3");
    guint16 port = ssh_relay_port();

    GPtrArray* argv = ssh_relay_argv(f.link, TRUE);
    g_assert_nonnull(argv);
    gchar* rdir = app_runtime_dir();
    gchar* tpath = g_build_filename(rdir, "relay", (const gchar*)argv->pdata[3], NULL);
    g_free(rdir);
    gchar* secret = NULL;
    g_assert_true(g_file_get_contents(tpath, &secret, NULL, NULL));
    g_strstrip(secret);
    g_free(tpath);
    g_ptr_array_unref(argv);

    /* Empty command -> login shell with PTY 30x100 */
    gchar* hs = g_strdup_printf("LOOKIN-RELAY 1\n%s\n30 100\n\n", secret);
    g_free(secret);
    GSocketConnection* conn = NULL;
    GDataInputStream* in = NULL;
    gchar* reply = raw_handshake(port, hs, &conn, &in);
    g_free(hs);
    g_assert_cmpstr(reply, ==, "OK tty");
    g_free(reply);

    /* Read 'O' frame with "pty:100x30\n" */
    guint8 hdr[5];
    gsize got = 0;
    g_assert_true(g_input_stream_read_all(G_INPUT_STREAM(in), hdr, 5, &got, NULL, NULL));
    g_assert_cmpuint(got, ==, 5);
    g_assert_cmpuint(hdr[0], ==, 'O');
    guint32 len = read_be32(hdr + 1);
    gchar* body = g_malloc0(len + 1);
    g_assert_true(g_input_stream_read_all(G_INPUT_STREAM(in), body, len, &got, NULL, NULL));
    g_assert_cmpstr(body, ==, "pty:100x30\n");
    g_free(body);

    /* Send 'W' frame: rows=40, cols=120 */
    guint8 wframe[9];
    wframe[0] = 'W';
    write_be32(wframe + 1, 4);
    write_be16(wframe + 5, 40);
    write_be16(wframe + 7, 120);
    GOutputStream* out = g_io_stream_get_output_stream(G_IO_STREAM(conn));
    g_assert_true(g_output_stream_write_all(out, wframe, sizeof wframe, NULL, NULL, NULL));

    /* Read 'O' frame with "resize:120x40\n" */
    g_assert_true(g_input_stream_read_all(G_INPUT_STREAM(in), hdr, 5, &got, NULL, NULL));
    g_assert_cmpuint(got, ==, 5);
    g_assert_cmpuint(hdr[0], ==, 'O');
    len = read_be32(hdr + 1);
    body = g_malloc0(len + 1);
    g_assert_true(g_input_stream_read_all(G_INPUT_STREAM(in), body, len, &got, NULL, NULL));
    g_assert_cmpstr(body, ==, "resize:120x40\n");
    g_free(body);

    g_object_unref(in);
    g_io_stream_close(G_IO_STREAM(conn), NULL, NULL);
    g_object_unref(conn);
    fixture_teardown(&f);
}

static void test_slow_reader_does_not_stall_sibling(void) {
    Fixture f;
    fixture_setup(&f, "dave", "pw4");

    /* Start a flood relay (2 MiB) and intentionally DO NOT read its stdout */
    GPtrArray* argv = ssh_session_argv(&f.target, 0);
    g_ptr_array_add(argv, g_strdup("flood"));
    g_ptr_array_add(argv, NULL);
    GError* err = NULL;
    GSubprocessLauncher* l = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDIN_PIPE |
                                                       G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                                       G_SUBPROCESS_FLAGS_STDERR_PIPE);
    GSubprocess* flood = g_subprocess_launcher_spawnv(l, (const gchar* const*)argv->pdata, &err);
    g_object_unref(l);
    g_ptr_array_unref(argv);
    g_assert_no_error(err);

    /* Give flood time to fill the pipe and relay outq (>= 256 KiB) */
    g_usleep(120 * 1000);

    /* Meanwhile, sibling commands on the SAME SshLink must complete promptly */
    for (int i = 0; i < 3; i++) {
        GBytes* out = NULL;
        GBytes* serr = NULL;
        const gchar* hello_cmd[] = { "hello", NULL };
        gint st = run_relay_cmd(&f.target, hello_cmd, NULL, &out, &serr);
        g_assert_cmpint(st, ==, 0);
        gsize n = 0;
        g_assert_cmpmem(g_bytes_get_data(out, &n), n, "hello stdout\n", 13);
        g_bytes_unref(out);
        g_bytes_unref(serr);
    }

    /* Now drain flood and verify every byte */
    GBytes* out = NULL;
    GBytes* serr = NULL;
    GBytes* empty = g_bytes_new(NULL, 0);
    g_assert_true(g_subprocess_communicate(flood, empty, NULL, &out, &serr, &err));
    g_bytes_unref(empty);
    g_assert_no_error(err);
    g_assert_cmpint(g_subprocess_get_exit_status(flood), ==, 0);
    g_object_unref(flood);

    gsize total = 0;
    const guint8* data = g_bytes_get_data(out, &total);
    g_assert_cmpuint(total, ==, FLOOD_TOTAL);
    for (guint i = 0; i < FLOOD_TOTAL; i++) {
        g_assert_cmpuint(data[i], ==, flood_byte(i));
    }
    g_bytes_unref(out);
    g_bytes_unref(serr);

    fixture_teardown(&f);
}

static void test_link_lost_exits_255(void) {
    Fixture f;
    fixture_setup(&f, "eve", "pw5");

    GPtrArray* argv = ssh_session_argv(&f.target, 0);
    g_ptr_array_add(argv, g_strdup("hang"));
    g_ptr_array_add(argv, NULL);
    GError* err = NULL;
    GSubprocessLauncher* l = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDIN_PIPE |
                                                       G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                                       G_SUBPROCESS_FLAGS_STDERR_PIPE);
    GSubprocess* proc = g_subprocess_launcher_spawnv(l, (const gchar* const*)argv->pdata, &err);
    g_object_unref(l);
    g_ptr_array_unref(argv);
    g_assert_no_error(err);

    /* Wait briefly for hang channel to be open, then drop the SSH connection */
    g_usleep(80 * 1000);
    ssh_test_server_drop_connection(f.srv);

    GBytes* out = NULL;
    GBytes* serr = NULL;
    GBytes* empty = g_bytes_new(NULL, 0);
    g_assert_true(g_subprocess_communicate(proc, empty, NULL, &out, &serr, &err));
    g_bytes_unref(empty);
    g_assert_no_error(err);
    g_assert_true(g_subprocess_get_if_exited(proc));
    g_assert_cmpint(g_subprocess_get_exit_status(proc), ==, 255);
    g_object_unref(proc);

    gsize en = 0;
    const gchar* ed = g_bytes_get_data(serr, &en);
    gchar* err_text = g_strndup(ed, en);
    g_assert_nonnull(strstr(err_text, "lookinfiles relay: connection lost"));
    g_free(err_text);
    g_bytes_unref(out);
    g_bytes_unref(serr);

    fixture_teardown(&f);
}

static guint missing_hits;

static void on_missing(const SshTarget* t, gpointer data) {
    (void)t;
    (void)data;
    missing_hits++;
}

static void test_session_argv_dispatch(void) {
    Fixture f;
    fixture_setup(&f, "frank", "pw6");

    /* 1. Open link + flags=0 -> relay argv */
    GPtrArray* argv = ssh_session_argv(&f.target, 0);
    g_assert_cmpuint(argv->len, ==, 4);
    g_assert_cmpstr(argv->pdata[0], ==, self_exe);
    g_assert_cmpstr(argv->pdata[1], ==, "--ssh-channel");
    g_ptr_array_unref(argv);

    /* 2. Terminal flags (SSH_ARGV_TTY | SSH_ARGV_NO_MUX) still use ssh in step 3 */
    argv = ssh_session_argv(&f.target, SSH_ARGV_TTY | SSH_ARGV_NO_MUX);
    g_assert_cmpstr(argv->pdata[0], ==, "ssh");
    g_assert_true(has_arg(argv, "-t"));
    g_ptr_array_unref(argv);

    /* 3. Missing link -> reports missing and returns ssh argv with BatchMode=yes */
    missing_hits = 0;
    ssh_session_on_master_missing(on_missing, NULL);
    SshTarget other = { .host = "no-such-host", .user = "frank", .port = 2222 };
    argv = ssh_session_argv(&other, 0);
    while (g_main_context_iteration(NULL, FALSE)) {}
    g_assert_cmpuint(missing_hits, ==, 1);
    g_assert_cmpstr(argv->pdata[0], ==, "ssh");
    g_assert_true(has_arg(argv, "BatchMode=yes"));
    g_ptr_array_unref(argv);
    ssh_session_on_master_missing(NULL, NULL);

    /* 4. Non-private runtime directory (app_runtime_dir() returns NULL) -> falls back to ssh */
    gchar* rdir = app_runtime_dir();
    g_assert_nonnull(rdir);
    g_assert_cmpint(chmod(rdir, 0755), ==, 0);
    g_assert_null(app_runtime_dir());
    argv = ssh_session_argv(&f.target, 0);
    g_assert_cmpstr(argv->pdata[0], ==, "ssh");
    g_ptr_array_unref(argv);
    g_assert_cmpint(chmod(rdir, 0700), ==, 0);
    g_free(rdir);

    fixture_teardown(&f);
}

int main(int argc, char** argv) {
    /* Helper mode: spawned by the tests above as the relay executable */
    if (ssh_relay_is_helper(argc, argv)) {
        return ssh_relay_helper_main(argc, argv);
    }
    self_exe = g_file_read_link("/proc/self/exe", NULL);
    if (self_exe == NULL) self_exe = g_canonicalize_filename(argv[0], NULL);
    test_runtime_root = g_dir_make_tmp("ssh-relay-run-XXXXXX", NULL);
    g_setenv("XDG_RUNTIME_DIR", test_runtime_root, TRUE);
    g_setenv("LOOKINFILES_SSH_LIBSSH", "1", TRUE);

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ssh_relay/is_helper", test_is_helper);
    g_test_add_func("/ssh_relay/roundtrip_status_stderr_and_ticket_file",
                    test_roundtrip_status_stderr_and_ticket_file);
    g_test_add_func("/ssh_relay/ticket_refusals_and_cleanup", test_ticket_refusals_and_cleanup);
    g_test_add_func("/ssh_relay/pty_shell_and_resize", test_pty_shell_and_resize);
    g_test_add_func("/ssh_relay/slow_reader_does_not_stall_sibling", test_slow_reader_does_not_stall_sibling);
    g_test_add_func("/ssh_relay/link_lost_exits_255", test_link_lost_exits_255);
    g_test_add_func("/ssh_relay/session_argv_dispatch", test_session_argv_dispatch);
    gint rc = g_test_run();
    rm_rf(test_runtime_root);
    g_free(test_runtime_root);
    g_free(self_exe);
    return rc;
}
