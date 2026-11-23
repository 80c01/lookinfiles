/* Unit tests for src/core/ssh_link.c (and SFTP over SshLink via
 * sftp_client_spawn()): host key verification, password / pubkey /
 * keyboard-interactive auth, registry, keep-alive, connection loss, SFTP. */
#include "core/sftp_client.h"
#include "core/ssh_link.h"

#define SSH_TEST_SERVER_WITH_SFTP
#include "ssh_test_server.h"

#include <glib/gstdio.h>
#include <string.h>

typedef struct {
    gboolean accept_hostkey;
    const gchar* secret;
    const gchar* secret2;
    gboolean cancel_after_first_secret;
    guint confirm_count;
    guint secret_count;
    gchar* last_confirm_prompt;
    gchar* last_secret_prompt;
} AskFixture;

static void ask_fixture_clear(AskFixture* f) {
    g_clear_pointer(&f->last_confirm_prompt, g_free);
    g_clear_pointer(&f->last_secret_prompt, g_free);
}

static void on_test_ask(SshLinkAsk* ask, SshLinkAskKind kind, const gchar* prompt, gpointer data) {
    AskFixture* f = data;
    /* Must always arrive on the default main context (main thread) */
    g_assert_true(g_main_context_is_owner(g_main_context_default()));
    if (kind == SSH_LINK_ASK_NOTICE) {
        g_assert_null(ask);
        return;
    }
    if (kind == SSH_LINK_ASK_CONFIRM) {
        f->confirm_count++;
        g_free(f->last_confirm_prompt);
        f->last_confirm_prompt = g_strdup(prompt);
        ssh_link_reply(ask, f->accept_hostkey ? "yes" : NULL);
        return;
    }
    f->secret_count++;
    g_free(f->last_secret_prompt);
    f->last_secret_prompt = g_strdup(prompt);
    if (f->cancel_after_first_secret && f->secret_count > 1) {
        ssh_link_reply(ask, NULL);
    } else if (f->secret_count == 2 && f->secret2 != NULL) {
        ssh_link_reply(ask, f->secret2);
    } else {
        ssh_link_reply(ask, f->secret);
    }
}

typedef struct {
    gboolean done;
    gboolean ok;
    gchar* err;
    GMainLoop* loop;
} ConnectWait;

static void on_ready(SshLink* link, gboolean ok, const gchar* error_text, gpointer data) {
    (void)link;
    g_assert_true(g_main_context_is_owner(g_main_context_default()));
    ConnectWait* w = data;
    w->done = TRUE;
    w->ok = ok;
    g_free(w->err);
    w->err = g_strdup(error_text);
    g_main_loop_quit(w->loop);
}

static SshLink* connect_wait(const SshTarget* t, AskFixture* ask, gboolean* out_ok, gchar** out_err) {
    ConnectWait w = { .loop = g_main_loop_new(NULL, FALSE) };
    SshLink* link = ssh_link_connect(t, on_test_ask, ask, on_ready, &w);
    while (!w.done) g_main_loop_run(w.loop);
    g_main_loop_unref(w.loop);
    if (out_ok != NULL) *out_ok = w.ok;
    if (out_err != NULL) {
        *out_err = w.err;
    } else {
        g_free(w.err);
    }
    return link;
}

static void on_async_done(GObject* src, GAsyncResult* res, gpointer data) {
    (void)src;
    *(GAsyncResult**)data = g_object_ref(res);
}

static GAsyncResult* wait_async(GAsyncResult** slot) {
    while (*slot == NULL) g_main_context_iteration(NULL, TRUE);
    GAsyncResult* r = *slot;
    *slot = NULL;
    return r;
}

static void rm_rf(const gchar* path) {
    const gchar* argv[] = { "rm", "-rf", path, NULL };
    g_spawn_sync(NULL, (gchar**)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, NULL);
}

/* ------------------------------------------------------------------------- */
/* Tests                                                                     */
/* ------------------------------------------------------------------------- */

static void test_unknown_then_known_host_key(void) {
    gchar* tmp = g_dir_make_tmp("ssh-link-kh-XXXXXX", NULL);
    gchar* kh = g_build_filename(tmp, "known_hosts", NULL);
    ssh_link_set_test_options("", kh, FALSE, FALSE);
    ssh_link_set_keepalive_ms(0);

    SshTestServer* srv = ssh_test_server_start("alice", "pw123");
    SshTarget t = { .host = "127.0.0.1", .user = "alice", .port = srv->port };

    /* 1. Refuse unknown host key -> fails, not saved */
    AskFixture ask = { .accept_hostkey = FALSE, .secret = "pw123" };
    gboolean ok = TRUE;
    gchar* err = NULL;
    SshLink* l0 = connect_wait(&t, &ask, &ok, &err);
    g_assert_false(ok);
    g_assert_nonnull(err);
    g_assert_cmpuint(ask.confirm_count, ==, 1);
    g_assert_nonnull(strstr(ask.last_confirm_prompt, "SHA256:"));
    g_assert_cmpuint(ask.secret_count, ==, 0);
    g_assert_false(ssh_link_is_up(l0));
    g_assert_null(ssh_link_lookup(&t));
    ssh_link_close(l0);
    ssh_link_unref(l0);
    g_clear_pointer(&err, g_free);
    ask_fixture_clear(&ask);

    /* 2. Accept unknown host key -> succeeds and writes known_hosts */
    memset(&ask, 0, sizeof ask);
    ask.accept_hostkey = TRUE;
    ask.secret = "pw123";
    SshLink* l1 = connect_wait(&t, &ask, &ok, &err);
    g_assert_true(ok);
    g_assert_null(err);
    g_assert_cmpuint(ask.confirm_count, ==, 1);
    g_assert_cmpuint(ask.secret_count, ==, 1);
    g_assert_true(ssh_link_is_up(l1));
    g_assert_true(g_file_test(kh, G_FILE_TEST_IS_REGULAR));
    ssh_link_close(l1);
    ssh_link_unref(l1);
    ask_fixture_clear(&ask);

    /* 3. Reconnect -> host is now known, ZERO host key confirmations */
    memset(&ask, 0, sizeof ask);
    ask.accept_hostkey = FALSE; /* would fail if asked */
    ask.secret = "pw123";
    SshLink* l2 = connect_wait(&t, &ask, &ok, &err);
    g_assert_true(ok);
    g_assert_null(err);
    g_assert_cmpuint(ask.confirm_count, ==, 0);
    g_assert_cmpuint(ask.secret_count, ==, 1);
    ssh_link_close(l2);
    ssh_link_unref(l2);
    ask_fixture_clear(&ask);

    ssh_test_server_stop(srv);
    ssh_link_set_test_options(NULL, NULL, TRUE, TRUE);
    rm_rf(tmp);
    g_free(kh);
    g_free(tmp);
}

static void test_changed_host_key_refused(void) {
    gchar* tmp = g_dir_make_tmp("ssh-link-chg-XXXXXX", NULL);
    gchar* kh = g_build_filename(tmp, "known_hosts", NULL);
    ssh_link_set_test_options("", kh, FALSE, FALSE);
    ssh_link_set_keepalive_ms(0);

    SshTestServer* srv = ssh_test_server_start("alice", "pw123");
    SshTarget t = { .host = "127.0.0.1", .user = "alice", .port = srv->port };

    AskFixture ask = { .accept_hostkey = TRUE, .secret = "pw123" };
    gboolean ok = FALSE;
    SshLink* l1 = connect_wait(&t, &ask, &ok, NULL);
    g_assert_true(ok);
    ssh_link_close(l1);
    ssh_link_unref(l1);
    ask_fixture_clear(&ask);

    /* Rotate server host key on the same port -> MUST refuse without prompting */
    ssh_test_server_rotate_host_key(srv);
    memset(&ask, 0, sizeof ask);
    ask.accept_hostkey = TRUE;
    ask.secret = "pw123";
    gchar* err = NULL;
    SshLink* l2 = connect_wait(&t, &ask, &ok, &err);
    g_assert_false(ok);
    g_assert_nonnull(err);
    g_assert_nonnull(strstr(err, "CHANGED"));
    g_assert_cmpuint(ask.confirm_count, ==, 0);
    g_assert_cmpuint(ask.secret_count, ==, 0);
    g_assert_false(ssh_link_is_up(l2));
    ssh_link_close(l2);
    ssh_link_unref(l2);
    g_free(err);
    ask_fixture_clear(&ask);

    ssh_test_server_stop(srv);
    ssh_link_set_test_options(NULL, NULL, TRUE, TRUE);
    rm_rf(tmp);
    g_free(kh);
    g_free(tmp);
}

static void test_password_once_many_channels(void) {
    gchar* tmp = g_dir_make_tmp("ssh-link-pw-XXXXXX", NULL);
    gchar* kh = g_build_filename(tmp, "known_hosts", NULL);
    ssh_link_set_test_options("", kh, FALSE, FALSE);
    ssh_link_set_keepalive_ms(0);

    SshTestServer* srv = ssh_test_server_start("bob", "hunter2");
    SshTarget t = { .host = "127.0.0.1", .user = "bob", .port = srv->port };
    AskFixture ask = { .accept_hostkey = TRUE, .secret = "hunter2" };
    gboolean ok = FALSE;
    SshLink* link = connect_wait(&t, &ask, &ok, NULL);
    g_assert_true(ok);
    g_assert_cmpuint(ask.secret_count, ==, 1);

    /* Registry lookup finds the link */
    SshLink* found = ssh_link_lookup(&t);
    g_assert_true(found == link);
    ssh_link_unref(found);

    /* Open 5 exec channels on the same link -> password still asked only once */
    for (int i = 0; i < 5; i++) {
        GAsyncResult* slot = NULL;
        ssh_link_open_channel_async(link, SSH_CHANNEL_KIND_EXEC, "hello", 0, 0, NULL, on_async_done, &slot);
        GAsyncResult* r = wait_async(&slot);
        GError* err = NULL;
        SshChannel* ch = ssh_link_open_channel_finish(link, r, &err);
        g_object_unref(r);
        g_assert_no_error(err);
        g_assert_nonnull(ch);

        gchar buf[64] = { 0 };
        g_input_stream_read_all_async(g_io_stream_get_input_stream(G_IO_STREAM(ch)), buf, sizeof buf - 1,
                                      G_PRIORITY_DEFAULT, NULL, on_async_done, &slot);
        r = wait_async(&slot);
        gsize n = 0;
        g_assert_true(g_input_stream_read_all_finish(g_io_stream_get_input_stream(G_IO_STREAM(ch)), r, &n, &err));
        g_object_unref(r);
        g_assert_no_error(err);
        g_assert_cmpstr(buf, ==, "hello stdout\n");
        g_object_unref(ch);
    }
    g_assert_cmpuint(ask.secret_count, ==, 1);
    g_assert_cmpuint(srv->auth_success_count, ==, 1);

    /* Closed link leaves the registry at once */
    ssh_link_close(link);
    g_assert_false(ssh_link_is_up(link));
    g_assert_null(ssh_link_lookup(&t));
    ssh_link_unref(link);
    ask_fixture_clear(&ask);

    ssh_test_server_stop(srv);
    ssh_link_set_test_options(NULL, NULL, TRUE, TRUE);
    rm_rf(tmp);
    g_free(kh);
    g_free(tmp);
}

static void test_wrong_password(void) {
    gchar* tmp = g_dir_make_tmp("ssh-link-badpw-XXXXXX", NULL);
    gchar* kh = g_build_filename(tmp, "known_hosts", NULL);
    ssh_link_set_test_options("", kh, FALSE, FALSE);
    ssh_link_set_keepalive_ms(0);

    SshTestServer* srv = ssh_test_server_start("bob", "correct");
    SshTarget t = { .host = "127.0.0.1", .user = "bob", .port = srv->port };
    AskFixture ask = { .accept_hostkey = TRUE, .secret = "wrong" };
    gboolean ok = TRUE;
    gchar* err = NULL;
    SshLink* link = connect_wait(&t, &ask, &ok, &err);
    g_assert_false(ok);
    g_assert_nonnull(err);
    g_assert_cmpuint(ask.secret_count, ==, 3);
    g_assert_false(ssh_link_is_up(link));
    g_assert_null(ssh_link_lookup(&t));
    ssh_link_close(link);
    ssh_link_unref(link);
    g_free(err);
    ask_fixture_clear(&ask);

    ssh_test_server_stop(srv);
    ssh_link_set_test_options(NULL, NULL, TRUE, TRUE);
    rm_rf(tmp);
    g_free(kh);
    g_free(tmp);
}

static void test_pubkey_auth(void) {
    gchar* tmp = g_dir_make_tmp("ssh-link-pk-XXXXXX", NULL);
    gchar* kh = g_build_filename(tmp, "known_hosts", NULL);
    gchar* key_plain = g_build_filename(tmp, "id_plain", NULL);
    gchar* key_plain_pub = g_build_filename(tmp, "id_plain.pub", NULL);
    gchar* key_enc = g_build_filename(tmp, "id_enc", NULL);
    gchar* key_enc_pub = g_build_filename(tmp, "id_enc.pub", NULL);

    ssh_key k = NULL;
    g_assert_cmpint(ssh_pki_generate(SSH_KEYTYPE_ED25519, 0, &k), ==, SSH_OK);
    g_assert_cmpint(ssh_pki_export_privkey_file(k, NULL, NULL, NULL, key_plain), ==, SSH_OK);
    g_assert_cmpint(ssh_pki_export_pubkey_file(k, key_plain_pub), ==, SSH_OK);
    g_assert_cmpint(ssh_pki_export_privkey_file(k, "my-passphrase", NULL, NULL, key_enc), ==, SSH_OK);
    g_assert_cmpint(ssh_pki_export_pubkey_file(k, key_enc_pub), ==, SSH_OK);

    ssh_link_set_test_options("", kh, FALSE, FALSE);
    ssh_link_set_keepalive_ms(0);
    SshTestServer* srv = ssh_test_server_start("carol", NULL);
    ssh_test_server_set_pubkey(srv, k);
    ssh_key_free(k);

    /* 1. Unencrypted key -> 0 secret prompts */
    SshTarget t1 = { .host = "127.0.0.1", .user = "carol", .port = srv->port, .identity_file = key_plain };
    AskFixture ask = { .accept_hostkey = TRUE };
    gboolean ok = FALSE;
    SshLink* l1 = connect_wait(&t1, &ask, &ok, NULL);
    g_assert_true(ok);
    g_assert_cmpuint(ask.secret_count, ==, 0);
    ssh_link_close(l1);
    ssh_link_unref(l1);
    ask_fixture_clear(&ask);

    /* 2. Encrypted key -> asks passphrase via SSH_LINK_ASK_SECRET */
    SshTarget t2 = { .host = "127.0.0.1", .user = "carol", .port = srv->port, .identity_file = key_enc };
    memset(&ask, 0, sizeof ask);
    ask.secret = "my-passphrase";
    SshLink* l2 = connect_wait(&t2, &ask, &ok, NULL);
    g_assert_true(ok);
    g_assert_cmpuint(ask.secret_count, ==, 1);
    g_assert_nonnull(strstr(ask.last_secret_prompt, "passphrase"));
    ssh_link_close(l2);
    ssh_link_unref(l2);
    ask_fixture_clear(&ask);

    ssh_test_server_stop(srv);
    ssh_link_set_test_options(NULL, NULL, TRUE, TRUE);
    rm_rf(tmp);
    g_free(key_plain);
    g_free(key_plain_pub);
    g_free(key_enc);
    g_free(key_enc_pub);
    g_free(kh);
    g_free(tmp);
}

static void test_kbdint_auth(void) {
    gchar* tmp = g_dir_make_tmp("ssh-link-ki-XXXXXX", NULL);
    gchar* kh = g_build_filename(tmp, "known_hosts", NULL);
    ssh_link_set_test_options("", kh, FALSE, FALSE);
    ssh_link_set_keepalive_ms(0);

    SshTestServer* srv = ssh_test_server_start("dave", NULL);
    ssh_test_server_set_kbdint(srv, "111111", "222222");
    SshTarget t = { .host = "127.0.0.1", .user = "dave", .port = srv->port };

    AskFixture ask = { .accept_hostkey = TRUE, .secret = "111111", .secret2 = "222222" };
    gboolean ok = FALSE;
    SshLink* link = connect_wait(&t, &ask, &ok, NULL);
    g_assert_true(ok);
    g_assert_cmpuint(ask.secret_count, ==, 2);
    ssh_link_close(link);
    ssh_link_unref(link);
    ask_fixture_clear(&ask);

    ssh_test_server_stop(srv);
    ssh_link_set_test_options(NULL, NULL, TRUE, TRUE);
    rm_rf(tmp);
    g_free(kh);
    g_free(tmp);
}

static void test_sftp_over_link(void) {
    gchar* tmp = g_dir_make_tmp("ssh-link-sftp-XXXXXX", NULL);
    gchar* kh = g_build_filename(tmp, "known_hosts", NULL);
    gchar* root = g_build_filename(tmp, "files", NULL);
    g_assert_cmpint(g_mkdir(root, 0755), ==, 0);
    ssh_link_set_test_options("", kh, FALSE, FALSE);
    ssh_link_set_keepalive_ms(0);

    SshTestServer* srv = ssh_test_server_start("eve", "sftp-pw");
    SshTarget t = { .host = "127.0.0.1", .user = "eve", .port = srv->port };
    AskFixture ask = { .accept_hostkey = TRUE, .secret = "sftp-pw" };
    gboolean ok = FALSE;
    SshLink* link = connect_wait(&t, &ask, &ok, NULL);
    g_assert_true(ok);
    g_assert_cmpuint(ask.secret_count, ==, 1);

    /* sftp_client_spawn() discovers the SshLink in the registry and opens
     * a subsystem sftp channel without spawning ssh or asking again */
    GError* err = NULL;
    SftpClient* c1 = sftp_client_spawn(&t, &err);
    g_assert_no_error(err);
    g_assert_nonnull(c1);

    gchar* file = g_build_filename(root, "payload.bin", NULL);
    gsize payload_len = 200 * 1024 + 13;
    guint8* raw = g_malloc(payload_len);
    for (gsize i = 0; i < payload_len; i++) raw[i] = (guint8)((i * 11 + 3) & 0xff);
    GBytes* payload = g_bytes_new_take(raw, payload_len);

    GAsyncResult* slot = NULL;
    sftp_client_write_async(c1, file, payload, 0644, NULL, on_async_done, &slot);
    GAsyncResult* r = wait_async(&slot);
    g_assert_true(sftp_client_finish(r, &err));
    g_object_unref(r);
    g_assert_no_error(err);

    /* Second SFTP client on the same SshLink reads back the file & lists folder */
    SftpClient* c2 = sftp_client_spawn(&t, &err);
    g_assert_no_error(err);
    g_assert_nonnull(c2);

    sftp_client_read_async(c2, file, G_MAXUINT64, NULL, on_async_done, &slot);
    r = wait_async(&slot);
    guint64 size = 0;
    GBytes* back = sftp_client_read_finish(r, &size, &err);
    g_object_unref(r);
    g_assert_no_error(err);
    g_assert_cmpuint(size, ==, payload_len);
    g_assert_true(g_bytes_equal(back, payload));
    g_bytes_unref(back);

    sftp_client_list_async(c2, root, NULL, on_async_done, &slot);
    r = wait_async(&slot);
    GPtrArray* list = sftp_client_list_finish(r, &err);
    g_object_unref(r);
    g_assert_no_error(err);
    g_assert_cmpuint(list->len, ==, 1);
    g_assert_cmpstr(((SftpEntry*)list->pdata[0])->name, ==, "payload.bin");
    g_ptr_array_unref(list);

    g_assert_cmpuint(ask.secret_count, ==, 1);
    g_assert_cmpuint(srv->auth_success_count, ==, 1);

    sftp_client_free(c1);
    sftp_client_free(c2);
    for (int i = 0; i < 20; i++) g_main_context_iteration(NULL, FALSE);
    ssh_link_close(link);
    ssh_link_unref(link);
    g_bytes_unref(payload);
    g_free(file);
    ask_fixture_clear(&ask);

    ssh_test_server_stop(srv);
    ssh_link_set_test_options(NULL, NULL, TRUE, TRUE);
    rm_rf(tmp);
    g_free(root);
    g_free(kh);
    g_free(tmp);
}

static void test_keepalive_alive_and_lost(void) {
    gchar* tmp = g_dir_make_tmp("ssh-link-ka-XXXXXX", NULL);
    gchar* kh = g_build_filename(tmp, "known_hosts", NULL);
    ssh_link_set_test_options("", kh, FALSE, FALSE);
    ssh_link_set_keepalive_ms(80);

    SshTestServer* srv = ssh_test_server_start("frank", "pw");
    SshTarget t = { .host = "127.0.0.1", .user = "frank", .port = srv->port };
    AskFixture ask = { .accept_hostkey = TRUE, .secret = "pw" };
    gboolean ok = FALSE;
    SshLink* link = connect_wait(&t, &ask, &ok, NULL);
    g_assert_true(ok);

    /* Wait ~450 ms (> 5 keep-alive intervals) while server answers keepalives:
     * link MUST stay alive and keepalive_count MUST advance past 3. */
    gint64 until = g_get_monotonic_time() + 450 * 1000;
    while (g_get_monotonic_time() < until) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(10 * 1000);
    }
    g_assert_true(ssh_link_is_up(link));
    g_mutex_lock(&srv->mu);
    guint ka = srv->keepalive_count;
    srv->drop_keepalive = TRUE; /* now stop answering keepalives */
    g_mutex_unlock(&srv->mu);
    g_assert_cmpuint(ka, >=, 3);

    /* Open a channel that hangs; 3 missed keepalives must fail it */
    GAsyncResult* open_slot = NULL;
    ssh_link_open_channel_async(link, SSH_CHANNEL_KIND_EXEC, "hang", 0, 0, NULL, on_async_done, &open_slot);
    GAsyncResult* r = wait_async(&open_slot);
    GError* err = NULL;
    SshChannel* ch = ssh_link_open_channel_finish(link, r, &err);
    g_object_unref(r);
    g_assert_no_error(err);

    guint8 buf[32];
    GAsyncResult* read_slot = NULL;
    g_input_stream_read_async(g_io_stream_get_input_stream(G_IO_STREAM(ch)), buf, sizeof buf,
                              G_PRIORITY_DEFAULT, NULL, on_async_done, &read_slot);
    r = wait_async(&read_slot);
    gssize n = g_input_stream_read_finish(g_io_stream_get_input_stream(G_IO_STREAM(ch)), r, &err);
    g_object_unref(r);
    g_assert_cmpint(n, ==, -1);
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED);
    g_clear_error(&err);
    g_assert_false(ssh_link_is_up(link));
    g_assert_null(ssh_link_lookup(&t));

    g_object_unref(ch);
    ssh_link_close(link);
    ssh_link_unref(link);
    ask_fixture_clear(&ask);

    ssh_test_server_stop(srv);
    ssh_link_set_keepalive_ms(0);
    ssh_link_set_test_options(NULL, NULL, TRUE, TRUE);
    rm_rf(tmp);
    g_free(kh);
    g_free(tmp);
}

static void test_connection_lost_fails_channels(void) {
    gchar* tmp = g_dir_make_tmp("ssh-link-drop-XXXXXX", NULL);
    gchar* kh = g_build_filename(tmp, "known_hosts", NULL);
    ssh_link_set_test_options("", kh, FALSE, FALSE);
    ssh_link_set_keepalive_ms(0);

    SshTestServer* srv = ssh_test_server_start("grace", "pw");
    SshTarget t = { .host = "127.0.0.1", .user = "grace", .port = srv->port };
    AskFixture ask = { .accept_hostkey = TRUE, .secret = "pw" };
    gboolean ok = FALSE;
    SshLink* link = connect_wait(&t, &ask, &ok, NULL);
    g_assert_true(ok);

    GAsyncResult* slot = NULL;
    GError* err = NULL;
    ssh_link_open_channel_async(link, SSH_CHANNEL_KIND_EXEC, "hang", 0, 0, NULL, on_async_done, &slot);
    GAsyncResult* r = wait_async(&slot);
    SshChannel* ch1 = ssh_link_open_channel_finish(link, r, &err);
    g_object_unref(r);
    g_assert_no_error(err);

    ssh_link_open_channel_async(link, SSH_CHANNEL_KIND_EXEC, "hang", 0, 0, NULL, on_async_done, &slot);
    r = wait_async(&slot);
    SshChannel* ch2 = ssh_link_open_channel_finish(link, r, &err);
    g_object_unref(r);
    g_assert_no_error(err);

    guint8 b1[16], b2[16];
    GAsyncResult *r1 = NULL, *r2 = NULL;
    g_input_stream_read_async(g_io_stream_get_input_stream(G_IO_STREAM(ch1)), b1, sizeof b1,
                              G_PRIORITY_DEFAULT, NULL, on_async_done, &r1);
    g_input_stream_read_async(g_io_stream_get_input_stream(G_IO_STREAM(ch2)), b2, sizeof b2,
                              G_PRIORITY_DEFAULT, NULL, on_async_done, &r2);

    ssh_test_server_drop_connection(srv);

    GAsyncResult* res1 = wait_async(&r1);
    GAsyncResult* res2 = wait_async(&r2);
    g_assert_cmpint(g_input_stream_read_finish(g_io_stream_get_input_stream(G_IO_STREAM(ch1)), res1, &err), ==, -1);
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED);
    g_clear_error(&err);
    g_assert_cmpint(g_input_stream_read_finish(g_io_stream_get_input_stream(G_IO_STREAM(ch2)), res2, &err), ==, -1);
    g_assert_error(err, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED);
    g_clear_error(&err);
    g_object_unref(res1);
    g_object_unref(res2);

    g_assert_false(ssh_link_is_up(link));
    g_assert_null(ssh_link_lookup(&t));

    g_object_unref(ch1);
    g_object_unref(ch2);
    ssh_link_close(link);
    ssh_link_unref(link);
    ask_fixture_clear(&ask);

    ssh_test_server_stop(srv);
    ssh_link_set_test_options(NULL, NULL, TRUE, TRUE);
    rm_rf(tmp);
    g_free(kh);
    g_free(tmp);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ssh_link/unknown_then_known_host_key", test_unknown_then_known_host_key);
    g_test_add_func("/ssh_link/changed_host_key_refused", test_changed_host_key_refused);
    g_test_add_func("/ssh_link/password_once_many_channels", test_password_once_many_channels);
    g_test_add_func("/ssh_link/wrong_password", test_wrong_password);
    g_test_add_func("/ssh_link/pubkey_auth", test_pubkey_auth);
    g_test_add_func("/ssh_link/kbdint_auth", test_kbdint_auth);
    g_test_add_func("/ssh_link/sftp_over_link", test_sftp_over_link);
    g_test_add_func("/ssh_link/keepalive_alive_and_lost", test_keepalive_alive_and_lost);
    g_test_add_func("/ssh_link/connection_lost_fails_channels", test_connection_lost_fails_channels);
    return g_test_run();
}
