/* Shared non-blocking libssh connection per remote machine (see ssh_link.h). */
#include "core/ssh_link.h"
#include "core/i18n.h"
#include "core/ssh_watch.h"

#include <libssh/callbacks.h>
#include <libssh/server.h> /* ssh_send_keepalive() */
#include <string.h>

#define CONNECT_TIMEOUT_MS 20000u
#define KEEPALIVE_DEFAULT_MS 15000u
#define KEEPALIVE_MAX_MISSES 3u
#define MAX_PASSWORD_TRIES 3
#define MAX_PASSPHRASE_TRIES 3

typedef enum { LINK_CONNECTING, LINK_HOSTKEY, LINK_AUTH, LINK_WAIT_ASK, LINK_READY, LINK_CLOSED } LinkState;

typedef enum {
    AUTH_NONE, AUTH_AGENT, AUTH_PUBKEY_NEXT, AUTH_PUBKEY_TRY, AUTH_PUBKEY_LOAD,
    AUTH_PUBKEY_SIGN, AUTH_KBDINT, AUTH_PASSWORD_ASK, AUTH_PASSWORD_SEND, AUTH_EXHAUSTED,
} AuthStep;

typedef enum { ASK_STAGE_NONE, ASK_STAGE_HOSTKEY, ASK_STAGE_PASSPHRASE, ASK_STAGE_KBDINT, ASK_STAGE_PASSWORD } AskStage;

struct SshLinkAsk {
    SshLink* link;
    guint64 seq;
    gchar* answer;
    gboolean replied;
};

struct SshLink {
    gint refs; /* atomic */
    gchar *host, *user, *identity_file, *reg_key;
    gint port;

    SshLinkAskFunc ask_func;
    gpointer ask_data;
    SshLinkReadyFunc ready_func;
    gpointer ready_data;

    gchar *opt_config_file, *opt_known_hosts;
    gboolean opt_try_agent, opt_try_default_keys;
    guint keepalive_ms;

    GMutex state_mu;
    GThread* thread;
    GMainContext* ctx;
    GMainLoop* loop;
    gint up, closing, ready_reported; /* atomic booleans */

    /* Link-thread-only state */
    LinkState state;
    AuthStep auth_step;
    int auth_methods;
    ssh_session session;
    ssh_event event;
    struct ssh_counter_struct sc, rc;

    socket_t watched_fd;
    GSource *sock_src, *connect_timer, *retry_timer, *keepalive_timer;
    GIOCondition sock_cond;

    gboolean keepalive_sent;
    guint keepalive_misses;
    uint64_t keepalive_in_packets;

    guint64 ask_seq;
    AskStage ask_stage;

    GPtrArray* key_paths;
    guint key_idx;
    ssh_key cur_pubkey, cur_privkey;
    gint pass_tries, kbdint_idx, kbdint_n, pw_tries;
    gchar* pending_secret;

    GPtrArray* channels; /* SshChannelCore* */
};

/* Global initialisation, registry, and test overrides */

static GMutex reg_mu;
static GHashTable* registry; /* reg_key -> SshLink* (unowned; removed on close) */

static GMutex opt_mu;
static gchar* test_config_file;
static gchar* test_known_hosts;
static gboolean test_try_agent = TRUE;
static gboolean test_try_default_keys = TRUE;
static guint default_keepalive_ms = KEEPALIVE_DEFAULT_MS;

static void ensure_libssh_init(void) {
    static gsize once = 0;
    if (g_once_init_enter(&once)) {
        ssh_threads_set_callbacks(ssh_threads_get_default());
        ssh_init();
        g_once_init_leave(&once, 1);
    }
}

static void wipe_string(gchar* s) {
    if (s == NULL) return;
    volatile gchar* p = s;
    while (*p != '\0') *p++ = '\0';
    g_free(s);
}

static gchar* target_key(const SshTarget* t) {
    const gchar* user = (t != NULL && t->user != NULL) ? t->user : "";
    const gchar* host = (t != NULL && t->host != NULL) ? t->host : "";
    gint port = (t != NULL && t->port > 0) ? t->port : 22;
    return g_strdup_printf("%s@%s:%d", user, host, port);
}

static void registry_insert(SshLink* link) {
    g_mutex_lock(&reg_mu);
    if (!g_atomic_int_get(&link->closing)) {
        if (registry == NULL) registry = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        g_hash_table_replace(registry, g_strdup(link->reg_key), link);
    }
    g_mutex_unlock(&reg_mu);
}

static void registry_remove(SshLink* link) {
    g_mutex_lock(&reg_mu);
    if (registry != NULL && g_hash_table_lookup(registry, link->reg_key) == link) {
        g_hash_table_remove(registry, link->reg_key);
    }
    g_mutex_unlock(&reg_mu);
}

SshLink* ssh_link_lookup(const SshTarget* target) {
    if (target == NULL || target->host == NULL || target->host[0] == '\0') return NULL;
    gchar* key = target_key(target);
    g_mutex_lock(&reg_mu);
    SshLink* link = registry != NULL ? g_hash_table_lookup(registry, key) : NULL;
    if (link != NULL && g_atomic_int_get(&link->up) && !g_atomic_int_get(&link->closing)) {
        ssh_link_ref(link);
    } else {
        link = NULL;
    }
    g_mutex_unlock(&reg_mu);
    g_free(key);
    return link;
}

gboolean ssh_link_enabled(void) {
    const gchar* env = g_getenv("LOOKINFILES_SSH_LIBSSH");
#ifdef G_OS_WIN32
    return env == NULL || strcmp(env, "0") != 0;
#else
    return env != NULL && env[0] != '\0' && strcmp(env, "0") != 0;
#endif
}

void ssh_link_set_test_options(const gchar* config_file, const gchar* known_hosts,
                               gboolean try_agent, gboolean try_default_keys) {
    g_mutex_lock(&opt_mu);
    g_free(test_config_file);
    test_config_file = g_strdup(config_file);
    g_free(test_known_hosts);
    test_known_hosts = g_strdup(known_hosts);
    test_try_agent = try_agent;
    test_try_default_keys = try_default_keys;
    g_mutex_unlock(&opt_mu);
}

void ssh_link_set_keepalive_ms(guint interval_ms) {
    g_mutex_lock(&opt_mu);
    default_keepalive_ms = interval_ms > 0 ? interval_ms : KEEPALIVE_DEFAULT_MS;
    g_mutex_unlock(&opt_mu);
}

/* Socket polling (non-blocking, level-triggered: ssh_watch.h) */

static void clear_source(GSource** src) {
    if (*src != NULL) {
        g_source_destroy(*src);
        g_source_unref(*src);
        *src = NULL;
    }
}

static void drop_socket_watch(SshLink* link) {
    clear_source(&link->sock_src);
    link->watched_fd = SSH_INVALID_SOCKET;
    link->sock_cond = 0;
}

static gboolean on_socket_ready(gpointer data);
static gboolean on_retry_timer(gpointer data);

static void update_socket_watch(SshLink* link) {
    if (link->state == LINK_CLOSED || link->session == NULL) {
        drop_socket_watch(link);
        clear_source(&link->retry_timer);
        return;
    }
    socket_t fd = ssh_get_fd(link->session);
    if (fd == SSH_INVALID_SOCKET) {
        drop_socket_watch(link);
        if (link->state == LINK_CONNECTING && link->retry_timer == NULL) {
            link->retry_timer = g_timeout_source_new(10);
            g_source_set_callback(link->retry_timer, on_retry_timer, link, NULL);
            g_source_attach(link->retry_timer, link->ctx);
        }
        return;
    }
    clear_source(&link->retry_timer);
    if (fd != link->watched_fd) {
        drop_socket_watch(link);
        link->watched_fd = fd;
    }

    GIOCondition cond = G_IO_IN | G_IO_HUP | G_IO_ERR;
    int flags = ssh_get_poll_flags(link->session);
    /* Writable is asked for only when libssh has bytes to send, or while the
     * TCP connection is still being made (nothing received yet): a connected
     * socket is always writable, so asking more would spin */
    gboolean tcp_pending = link->state == LINK_CONNECTING && flags == 0 && link->rc.in_bytes == 0;
    if ((flags & SSH_WRITE_PENDING) != 0 || tcp_pending) cond |= G_IO_OUT;
    if (link->sock_src != NULL && link->sock_cond == cond) return;

    clear_source(&link->sock_src);
    link->sock_src = ssh_watch_new((gint64)fd, cond);
    if (link->sock_src == NULL) {
        link->sock_cond = 0;
        return;
    }
    g_source_set_callback(link->sock_src, on_socket_ready, link, NULL);
    g_source_attach(link->sock_src, link->ctx);
    link->sock_cond = cond;
}

/* Ready reporting & prompts (always on the default main context) */

typedef struct {
    SshLink* link;
    gboolean ok;
    gchar* error_text;
} ReadyReport;

static gboolean on_ready_main(gpointer data) {
    ReadyReport* rep = data;
    if (rep->link->ready_func != NULL && !g_atomic_int_get(&rep->link->closing)) {
        rep->link->ready_func(rep->link, rep->ok, rep->error_text, rep->link->ready_data);
    }
    g_free(rep->error_text);
    ssh_link_unref(rep->link);
    g_free(rep);
    return G_SOURCE_REMOVE;
}

static void report_ready(SshLink* link, gboolean ok, const gchar* error_text) {
    if (!g_atomic_int_compare_and_exchange(&link->ready_reported, 0, 1)) return;
    if (g_atomic_int_get(&link->closing) || link->ready_func == NULL) return;
    ReadyReport* rep = g_new0(ReadyReport, 1);
    rep->link = ssh_link_ref(link);
    rep->ok = ok;
    rep->error_text = ok ? NULL : g_strdup(error_text != NULL ? error_text : "");
    g_main_context_invoke(NULL, on_ready_main, rep);
}

static void ask_free(gpointer data) {
    SshLinkAsk* ask = data;
    if (ask == NULL) return;
    wipe_string(ask->answer);
    if (ask->link != NULL) ssh_link_unref(ask->link);
    g_free(ask);
}

typedef struct {
    SshLinkAsk* ask;
    SshLink* link;
    SshLinkAskKind kind;
    gchar* prompt;
} AskDispatch;

static gboolean on_ask_main(gpointer data) {
    AskDispatch* d = data;
    if (!g_atomic_int_get(&d->link->closing) && d->link->ask_func != NULL) {
        d->link->ask_func(d->ask, d->kind, d->prompt, d->link->ask_data);
        d->ask = NULL;
    } else if (d->ask != NULL) {
        ssh_link_reply(d->ask, NULL);
        d->ask = NULL;
    }
    g_free(d->prompt);
    ssh_link_unref(d->link);
    g_free(d);
    return G_SOURCE_REMOVE;
}

static void send_notice(SshLink* link, const gchar* text) {
    if (link->ask_func == NULL || text == NULL || text[0] == '\0') return;
    AskDispatch* d = g_new0(AskDispatch, 1);
    d->link = ssh_link_ref(link);
    d->kind = SSH_LINK_ASK_NOTICE;
    d->prompt = g_utf8_make_valid(text, -1);
    g_main_context_invoke(NULL, on_ask_main, d);
}

static void send_ask(SshLink* link, SshLinkAskKind kind, AskStage stage, const gchar* prompt) {
    /* Pause socket polling while waiting for user input so a remote HUP
     * cannot busy-spin the link loop. */
    clear_source(&link->sock_src);
    link->sock_cond = 0;
    link->state = LINK_WAIT_ASK;
    link->ask_stage = stage;
    link->ask_seq++;

    SshLinkAsk* ask = g_new0(SshLinkAsk, 1);
    ask->link = ssh_link_ref(link);
    ask->seq = link->ask_seq;
    AskDispatch* d = g_new0(AskDispatch, 1);
    d->ask = ask;
    d->link = ssh_link_ref(link);
    d->kind = kind;
    d->prompt = g_utf8_make_valid(prompt != NULL ? prompt : "", -1);
    g_main_context_invoke(NULL, on_ask_main, d);
}

static gboolean on_ask_replied_on_link(gpointer data);

void ssh_link_reply(SshLinkAsk* ask, const gchar* answer) {
    if (ask == NULL) return;
    g_return_if_fail(!ask->replied);
    ask->replied = TRUE;
    ask->answer = answer != NULL ? g_strdup(answer) : NULL;
    if (g_atomic_int_get(&ask->link->closing)) { ask_free(ask); return; }
    g_main_context_invoke_full(ask->link->ctx, G_PRIORITY_DEFAULT, on_ask_replied_on_link, ask, ask_free);
}

/* Teardown & failure (link thread) */

static void clear_auth_scratch(SshLink* link) {
    g_clear_pointer(&link->key_paths, g_ptr_array_unref);
    if (link->cur_pubkey != NULL) { ssh_key_free(link->cur_pubkey); link->cur_pubkey = NULL; }
    if (link->cur_privkey != NULL) { ssh_key_free(link->cur_privkey); link->cur_privkey = NULL; }
    wipe_string(g_steal_pointer(&link->pending_secret));
}

static void link_cleanup_on_thread(SshLink* link, const GError* channel_err) {
    g_atomic_int_set(&link->up, 0);
    registry_remove(link);
    link->state = LINK_CLOSED;
    clear_source(&link->connect_timer);
    clear_source(&link->retry_timer);
    clear_source(&link->keepalive_timer);
    clear_auth_scratch(link);

    if (link->channels != NULL) {
        GError* err = channel_err != NULL
                          ? g_error_copy(channel_err)
                          : g_error_new_literal(G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED,
                                                _("the SSH connection to the remote machine was closed"));
        for (guint i = 0; i < link->channels->len; i++) ssh_channel_fail(link->channels->pdata[i], err);
        g_error_free(err);
        g_ptr_array_set_size(link->channels, 0);
    }

    drop_socket_watch(link);
    if (link->event != NULL) {
        if (link->session != NULL) ssh_event_remove_session(link->event, link->session);
        ssh_event_free(link->event);
        link->event = NULL;
    }
    if (link->session != NULL) {
        if (ssh_is_connected(link->session)) ssh_disconnect(link->session);
        ssh_free(link->session);
        link->session = NULL;
    }
}

static void link_fail(SshLink* link, const gchar* message) {
    gchar* msg = g_strdup(message != NULL ? message : _("SSH connection failed"));
    GError* err = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED, msg);
    link_cleanup_on_thread(link, err);
    g_error_free(err);
    report_ready(link, FALSE, msg);
    g_free(msg);
    if (link->loop != NULL) g_main_loop_quit(link->loop);
}

static void link_fail_session(SshLink* link, const gchar* fallback) {
    const char* msg = link->session != NULL ? ssh_get_error(link->session) : NULL;
    gchar* text = (msg != NULL && msg[0] != '\0') ? g_utf8_make_valid(msg, -1) : g_strdup(fallback);
    link_fail(link, text);
    g_free(text);
}

/* Keep-alive & channel pumping (LINK_READY, link thread) */

static void prune_dead_channels(SshLink* link) {
    if (link->channels == NULL) return;
    for (guint i = link->channels->len; i > 0; i--) {
        if (ssh_channel_is_dead(link->channels->pdata[i - 1])) g_ptr_array_remove_index(link->channels, i - 1);
    }
}

static void pump_channels(SshLink* link) {
    if (link->channels == NULL) return;
    for (guint i = 0; i < link->channels->len; i++) ssh_channel_pump(link->channels->pdata[i]);
    prune_dead_channels(link);
}

static gboolean session_is_lost(ssh_session s, int poll_rc) {
    return poll_rc == SSH_ERROR || !ssh_is_connected(s) ||
           (ssh_get_status(s) & (SSH_CLOSED | SSH_CLOSED_ERROR)) != 0;
}

static void link_pump_ready(SshLink* link) {
    if (link->state != LINK_READY || link->session == NULL) return;

    uint64_t pkts_before = link->rc.in_packets;
    int poll_rc = link->event != NULL ? ssh_event_dopoll(link->event, 0) : SSH_OK;
    if (link->rc.in_packets != pkts_before) link->keepalive_misses = 0;

    gboolean lost = session_is_lost(link->session, poll_rc);
    if (lost) { /* down before the channels hear of it: none may find the link up */
        g_atomic_int_set(&link->up, 0);
        registry_remove(link);
    }
    pump_channels(link); /* the last bytes received still reach them */
    if (lost) {
        link_fail_session(link, _("the connection to the remote machine was closed"));
        return;
    }

    /* Flush any outgoing bytes channels just queued without blocking */
    if ((ssh_get_poll_flags(link->session) & SSH_WRITE_PENDING) != 0 && link->event != NULL) {
        uint64_t pkts2 = link->rc.in_packets;
        int rc2 = ssh_event_dopoll(link->event, 0);
        gboolean lost2 = session_is_lost(link->session, rc2);
        if (lost2) {
            g_atomic_int_set(&link->up, 0);
            registry_remove(link);
        }
        if (link->rc.in_packets != pkts2) {
            link->keepalive_misses = 0;
            pump_channels(link);
        }
        if (lost2) {
            link_fail_session(link, _("the connection to the remote machine was closed"));
            return;
        }
    }
    update_socket_watch(link);
}

static gboolean on_keepalive_tick(gpointer data) {
    SshLink* link = data;
    if (link->state != LINK_READY || link->session == NULL) return G_SOURCE_REMOVE;

    if (link->keepalive_sent) {
        if (link->rc.in_packets != link->keepalive_in_packets) {
            link->keepalive_misses = 0;
        } else if (++link->keepalive_misses >= KEEPALIVE_MAX_MISSES) {
            link_fail(link, _("the SSH connection timed out"));
            return G_SOURCE_REMOVE;
        }
    }

    link->keepalive_in_packets = link->rc.in_packets;
    link->keepalive_sent = TRUE;

    /* In non-blocking mode libssh leaves global_req_state in DENIED/ACCEPTED
     * after the previous keepalive reply arrives; the first ssh_send_keepalive()
     * clears it without sending (out_packets unchanged), so a second call sends. */
    uint64_t out_before = link->sc.out_packets;
    ssh_send_keepalive(link->session);
    if (link->sc.out_packets == out_before && ssh_is_connected(link->session)) {
        ssh_send_keepalive(link->session);
    }

    link_pump_ready(link);
    return link->state == LINK_READY ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

static void enter_ready(SshLink* link) {
    clear_auth_scratch(link);
    link->state = LINK_READY;
    link->event = ssh_event_new();
    ssh_event_add_session(link->event, link->session);

    link->keepalive_sent = FALSE;
    link->keepalive_misses = 0;
    link->keepalive_in_packets = link->rc.in_packets;
    link->keepalive_timer = g_timeout_source_new(link->keepalive_ms);
    g_source_set_callback(link->keepalive_timer, on_keepalive_tick, link, NULL);
    g_source_attach(link->keepalive_timer, link->ctx);

    g_atomic_int_set(&link->up, 1);
    registry_insert(link);
    report_ready(link, TRUE, NULL);
    update_socket_watch(link);
}

/* Non-blocking authentication state machine (link thread) */

static gchar* expand_tilde(const gchar* path) {
    if (path != NULL && path[0] == '~' && (path[1] == '/' || path[1] == '\0')) {
        const gchar* home = g_get_home_dir();
        if (home != NULL) return g_strconcat(home, path + 1, NULL);
    }
    return g_strdup(path);
}

static void add_key_candidate(GPtrArray* list, const gchar* path) {
    if (path == NULL || path[0] == '\0' || !g_file_test(path, G_FILE_TEST_IS_REGULAR)) return;
    for (guint i = 0; i < list->len; i++) {
        if (strcmp(list->pdata[i], path) == 0) return;
    }
    g_ptr_array_add(list, g_strdup(path));
}

static void build_key_candidates(SshLink* link) {
    g_clear_pointer(&link->key_paths, g_ptr_array_unref);
    link->key_paths = g_ptr_array_new_with_free_func(g_free);
    link->key_idx = 0;

    if (link->identity_file != NULL && link->identity_file[0] != '\0') {
        gchar* exp = expand_tilde(link->identity_file);
        add_key_candidate(link->key_paths, exp);
        g_free(exp);
    }
    if (link->opt_try_default_keys) {
        const gchar* home = g_get_home_dir();
        static const gchar* const NAMES[] = { "id_ed25519", "id_ecdsa", "id_rsa" };
        for (guint i = 0; home != NULL && i < G_N_ELEMENTS(NAMES); i++) {
            gchar* p = g_build_filename(home, ".ssh", NAMES[i], NULL);
            add_key_candidate(link->key_paths, p);
            g_free(p);
        }
    }
}

static gchar* prompt_target_label(SshLink* link) {
    char* cfg_user = NULL;
    const gchar* u = link->user;
    if ((u == NULL || u[0] == '\0') && link->session != NULL &&
        ssh_options_get(link->session, SSH_OPTIONS_USER, &cfg_user) == SSH_OK && cfg_user != NULL &&
        cfg_user[0] != '\0') {
        u = cfg_user;
    }
    gchar* label = (u != NULL && u[0] != '\0') ? g_strdup_printf("%s@%s", u, link->host) : g_strdup(link->host);
    if (cfg_user != NULL) ssh_string_free_char(cfg_user);
    return label;
}

static void advance_auth_after_methods(SshLink* link, AuthStep from) {
    int m = link->auth_methods;
    if (from < AUTH_AGENT && (m & SSH_AUTH_METHOD_PUBLICKEY) && link->opt_try_agent) {
        link->auth_step = AUTH_AGENT;
    } else if (from < AUTH_PUBKEY_NEXT && (m & SSH_AUTH_METHOD_PUBLICKEY)) {
        build_key_candidates(link);
        if (link->key_paths->len > 0) link->auth_step = AUTH_PUBKEY_NEXT;
        else advance_auth_after_methods(link, AUTH_PUBKEY_NEXT);
    } else if (from < AUTH_KBDINT && (m & SSH_AUTH_METHOD_INTERACTIVE)) {
        link->auth_step = AUTH_KBDINT;
    } else if (from < AUTH_PASSWORD_ASK && (m & SSH_AUTH_METHOD_PASSWORD)) {
        link->auth_step = AUTH_PASSWORD_ASK;
    } else {
        link->auth_step = AUTH_EXHAUSTED;
    }
}

static void ask_kbdint_prompt(SshLink* link) {
    const char* instruction = ssh_userauth_kbdint_getinstruction(link->session);
    char echo = 0;
    const char* p = ssh_userauth_kbdint_getprompt(link->session, (unsigned int)link->kbdint_idx, &echo);
    gchar* text = (link->kbdint_idx == 0 && instruction != NULL && instruction[0] != '\0')
                      ? g_strdup_printf("%s\n%s", instruction, p != NULL ? p : "")
                      : g_strdup(p != NULL ? p : _("Verification code:"));
    send_ask(link, SSH_LINK_ASK_SECRET, ASK_STAGE_KBDINT, text);
    g_free(text);
}

static void auth_prepare_next_key(SshLink* link) {
    if (link->cur_pubkey != NULL) { ssh_key_free(link->cur_pubkey); link->cur_pubkey = NULL; }
    if (link->cur_privkey != NULL) { ssh_key_free(link->cur_privkey); link->cur_privkey = NULL; }
    link->pass_tries = 0;
    if (link->key_paths == NULL || link->key_idx >= link->key_paths->len) {
        advance_auth_after_methods(link, AUTH_PUBKEY_NEXT);
        return;
    }
    const gchar* path = link->key_paths->pdata[link->key_idx];
    gchar* pub_path = g_strconcat(path, ".pub", NULL);
    if (ssh_pki_import_pubkey_file(pub_path, &link->cur_pubkey) == SSH_OK) {
        g_free(pub_path);
        link->auth_step = AUTH_PUBKEY_TRY;
        return;
    }
    g_free(pub_path);
    if (ssh_pki_import_privkey_file(path, "", NULL, NULL, &link->cur_privkey) == SSH_OK) {
        link->auth_step = ssh_pki_export_privkey_to_pubkey(link->cur_privkey, &link->cur_pubkey) == SSH_OK
                              ? AUTH_PUBKEY_TRY
                              : AUTH_PUBKEY_SIGN;
    } else {
        link->auth_step = AUTH_PUBKEY_LOAD;
    }
}

static void drive_auth(SshLink* link) {
    while (link->state == LINK_AUTH) {
        int rc;
        switch (link->auth_step) {
        case AUTH_NONE:
            rc = ssh_userauth_none(link->session, NULL);
            if (rc == SSH_AUTH_AGAIN) { update_socket_watch(link); return; }
            if (rc == SSH_AUTH_SUCCESS) { enter_ready(link); return; }
            if (rc == SSH_AUTH_ERROR) { link_fail_session(link, _("SSH authentication failed")); return; }
            {
                char* banner = ssh_get_issue_banner(link->session);
                if (banner != NULL) {
                    g_strstrip(banner);
                    if (banner[0] != '\0') send_notice(link, banner);
                    ssh_string_free_char(banner);
                }
            }
            link->auth_methods = ssh_userauth_list(link->session, NULL);
            advance_auth_after_methods(link, AUTH_NONE);
            break;

        case AUTH_AGENT:
            rc = ssh_userauth_agent(link->session, NULL);
            if (rc == SSH_AUTH_AGAIN) { update_socket_watch(link); return; }
            if (rc == SSH_AUTH_SUCCESS) { enter_ready(link); return; }
            if (rc == SSH_AUTH_ERROR && !ssh_is_connected(link->session)) {
                link_fail_session(link, _("SSH authentication failed"));
                return;
            }
            if (rc == SSH_AUTH_PARTIAL) link->auth_methods = ssh_userauth_list(link->session, NULL);
            advance_auth_after_methods(link, AUTH_AGENT);
            break;

        case AUTH_PUBKEY_NEXT:
            auth_prepare_next_key(link);
            break;

        case AUTH_PUBKEY_TRY:
            rc = ssh_userauth_try_publickey(link->session, NULL, link->cur_pubkey);
            if (rc == SSH_AUTH_AGAIN) { update_socket_watch(link); return; }
            if (rc == SSH_AUTH_SUCCESS) {
                link->auth_step = link->cur_privkey != NULL ? AUTH_PUBKEY_SIGN : AUTH_PUBKEY_LOAD;
            } else if (rc == SSH_AUTH_ERROR) {
                link_fail_session(link, _("SSH public-key authentication failed"));
                return;
            } else {
                link->key_idx++;
                link->auth_step = AUTH_PUBKEY_NEXT;
            }
            break;

        case AUTH_PUBKEY_LOAD: {
            const gchar* path = link->key_paths->pdata[link->key_idx];
            if (link->pass_tries == 0 &&
                ssh_pki_import_privkey_file(path, "", NULL, NULL, &link->cur_privkey) == SSH_OK) {
                link->auth_step = AUTH_PUBKEY_SIGN;
                break;
            }
            if (link->pass_tries >= MAX_PASSPHRASE_TRIES || link->ask_func == NULL) {
                link->key_idx++;
                link->auth_step = AUTH_PUBKEY_NEXT;
                break;
            }
            gchar* prompt = g_strdup_printf(_("Enter passphrase for key '%s':"), path);
            send_ask(link, SSH_LINK_ASK_SECRET, ASK_STAGE_PASSPHRASE, prompt);
            g_free(prompt);
            return;
        }

        case AUTH_PUBKEY_SIGN:
            rc = ssh_userauth_publickey(link->session, NULL, link->cur_privkey);
            if (rc == SSH_AUTH_AGAIN) { update_socket_watch(link); return; }
            ssh_key_free(link->cur_privkey);
            link->cur_privkey = NULL;
            if (rc == SSH_AUTH_SUCCESS) { enter_ready(link); return; }
            if (rc == SSH_AUTH_ERROR) { link_fail_session(link, _("SSH public-key authentication failed")); return; }
            if (rc == SSH_AUTH_PARTIAL) {
                link->auth_methods = ssh_userauth_list(link->session, NULL);
                advance_auth_after_methods(link, AUTH_PUBKEY_NEXT);
            } else {
                link->key_idx++;
                link->auth_step = AUTH_PUBKEY_NEXT;
            }
            break;

        case AUTH_KBDINT:
            rc = ssh_userauth_kbdint(link->session, NULL, NULL);
            if (rc == SSH_AUTH_AGAIN) { update_socket_watch(link); return; }
            if (rc == SSH_AUTH_SUCCESS) { enter_ready(link); return; }
            if (rc == SSH_AUTH_INFO) {
                int np = ssh_userauth_kbdint_getnprompts(link->session);
                if (np <= 0) {
                    const char* instr = ssh_userauth_kbdint_getinstruction(link->session);
                    if (instr != NULL && instr[0] != '\0') send_notice(link, instr);
                    continue;
                }
                if (link->ask_func == NULL) { link_fail(link, _("Authentication cancelled.")); return; }
                link->kbdint_n = np;
                link->kbdint_idx = 0;
                ask_kbdint_prompt(link);
                return;
            }
            if (rc == SSH_AUTH_ERROR) { link_fail_session(link, _("SSH authentication failed")); return; }
            if (rc == SSH_AUTH_PARTIAL) link->auth_methods = ssh_userauth_list(link->session, NULL);
            advance_auth_after_methods(link, AUTH_KBDINT);
            break;

        case AUTH_PASSWORD_ASK:
            if (link->pw_tries >= MAX_PASSWORD_TRIES) { link_fail(link, _("Permission denied.")); return; }
            if (link->ask_func == NULL) { link_fail(link, _("Permission denied (password required).")); return; }
            {
                gchar* label = prompt_target_label(link);
                gchar* prompt = link->pw_tries > 0
                                    ? g_strdup_printf(_("Permission denied, please try again.\n%s's password:"), label)
                                    : g_strdup_printf(_("%s's password:"), label);
                send_ask(link, SSH_LINK_ASK_SECRET, ASK_STAGE_PASSWORD, prompt);
                g_free(prompt);
                g_free(label);
            }
            return;

        case AUTH_PASSWORD_SEND:
            rc = ssh_userauth_password(link->session, NULL, link->pending_secret);
            if (rc == SSH_AUTH_AGAIN) { update_socket_watch(link); return; }
            wipe_string(g_steal_pointer(&link->pending_secret));
            if (rc == SSH_AUTH_SUCCESS) { enter_ready(link); return; }
            if (rc == SSH_AUTH_ERROR) { link_fail_session(link, _("SSH authentication failed")); return; }
            link->pw_tries++;
            if (rc == SSH_AUTH_PARTIAL) {
                link->auth_methods = ssh_userauth_list(link->session, NULL);
                advance_auth_after_methods(link, AUTH_NONE);
            } else {
                link->auth_step = AUTH_PASSWORD_ASK;
            }
            break;

        case AUTH_EXHAUSTED:
            link_fail(link, _("Permission denied."));
            return;
        }
    }
}

/* Host key check & connect state machine (link thread) */

static void ensure_known_hosts_dir(SshLink* link) {
    gchar* path = (link->opt_known_hosts != NULL && link->opt_known_hosts[0] != '\0')
                      ? g_strdup(link->opt_known_hosts)
                      : g_build_filename(g_get_home_dir(), ".ssh", "known_hosts", NULL);
    gchar* dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    g_free(path);
}

static void check_host_key(SshLink* link) {
    link->state = LINK_HOSTKEY;
    switch (ssh_session_is_known_server(link->session)) {
    case SSH_KNOWN_HOSTS_OK:
        link->state = LINK_AUTH;
        link->auth_step = AUTH_NONE;
        drive_auth(link);
        return;

    case SSH_KNOWN_HOSTS_UNKNOWN:
    case SSH_KNOWN_HOSTS_NOT_FOUND: {
        ssh_key srv_key = NULL;
        unsigned char* hash = NULL;
        size_t hlen = 0;
        if (ssh_get_server_publickey(link->session, &srv_key) != SSH_OK ||
            ssh_get_publickey_hash(srv_key, SSH_PUBLICKEY_HASH_SHA256, &hash, &hlen) != 0) {
            if (srv_key != NULL) ssh_key_free(srv_key);
            link_fail(link, _("Could not read the server's host key."));
            return;
        }
        char* fp = ssh_get_fingerprint_hash(SSH_PUBLICKEY_HASH_SHA256, hash, hlen);
        const char* ktype = ssh_key_type_to_char(ssh_key_type(srv_key));
        ssh_clean_pubkey_hash(&hash);
        ssh_key_free(srv_key);

        if (link->ask_func == NULL) {
            if (fp != NULL) ssh_string_free_char(fp);
            link_fail(link, _("Host key verification failed."));
            return;
        }
        gchar* prompt = g_strdup_printf(
            _("The authenticity of host '%s' can't be established.\n"
              "%s key fingerprint is %s.\n"
              "Are you sure you want to continue connecting?"),
            link->host, ktype != NULL ? ktype : "SSH", fp != NULL ? fp : "");
        if (fp != NULL) ssh_string_free_char(fp);
        send_ask(link, SSH_LINK_ASK_CONFIRM, ASK_STAGE_HOSTKEY, prompt);
        g_free(prompt);
        return;
    }

    case SSH_KNOWN_HOSTS_CHANGED:
    case SSH_KNOWN_HOSTS_OTHER:
        link_fail(link,
                  _("REMOTE HOST IDENTIFICATION HAS CHANGED! "
                    "The host key for this server has changed; the connection was refused."));
        return;

    default:
        link_fail_session(link, _("Host key verification failed."));
        return;
    }
}

static gboolean on_ask_replied_on_link(gpointer data) {
    SshLinkAsk* ask = data;
    SshLink* link = ask->link;
    if (link->state != LINK_WAIT_ASK || ask->seq != link->ask_seq) return G_SOURCE_REMOVE;

    AskStage stage = link->ask_stage;
    link->ask_stage = ASK_STAGE_NONE;
    gchar* answer = g_steal_pointer(&ask->answer);

    if (stage == ASK_STAGE_HOSTKEY) {
        if (answer == NULL) { link_fail(link, _("Host key verification failed.")); return G_SOURCE_REMOVE; }
        g_free(answer);
        ensure_known_hosts_dir(link);
        if (ssh_session_update_known_hosts(link->session) != SSH_OK) {
            link_fail_session(link, _("Could not save the host key to known_hosts."));
            return G_SOURCE_REMOVE;
        }
        link->state = LINK_AUTH;
        link->auth_step = AUTH_NONE;
        drive_auth(link);
    } else if (stage == ASK_STAGE_PASSPHRASE) {
        if (answer == NULL) {
            link->key_idx++;
            link->auth_step = AUTH_PUBKEY_NEXT;
        } else {
            const gchar* path = link->key_paths->pdata[link->key_idx];
            int rc = ssh_pki_import_privkey_file(path, answer, NULL, NULL, &link->cur_privkey);
            wipe_string(answer);
            link->pass_tries++;
            link->auth_step = (rc == SSH_OK) ? AUTH_PUBKEY_SIGN : AUTH_PUBKEY_LOAD;
        }
        link->state = LINK_AUTH;
        drive_auth(link);
    } else if (stage == ASK_STAGE_KBDINT) {
        if (answer == NULL) { link_fail(link, _("Authentication cancelled.")); return G_SOURCE_REMOVE; }
        ssh_userauth_kbdint_setanswer(link->session, (unsigned int)link->kbdint_idx, answer);
        wipe_string(answer);
        if (++link->kbdint_idx < link->kbdint_n) {
            ask_kbdint_prompt(link);
        } else {
            link->state = LINK_AUTH;
            link->auth_step = AUTH_KBDINT;
            drive_auth(link);
        }
    } else if (stage == ASK_STAGE_PASSWORD) {
        if (answer == NULL) {
            link_fail(link, link->pw_tries > 0 ? _("Permission denied.") : _("Authentication cancelled."));
            return G_SOURCE_REMOVE;
        }
        link->pending_secret = answer;
        link->state = LINK_AUTH;
        link->auth_step = AUTH_PASSWORD_SEND;
        drive_auth(link);
    } else {
        wipe_string(answer);
    }
    return G_SOURCE_REMOVE;
}

static void drive_connect(SshLink* link) {
    if (link->state != LINK_CONNECTING || link->session == NULL) return;
    int rc = ssh_connect(link->session);
    if (rc == SSH_AGAIN) { update_socket_watch(link); return; }
    clear_source(&link->connect_timer);
    clear_source(&link->retry_timer);
    if (rc != SSH_OK) { link_fail_session(link, _("Could not connect to the remote machine.")); return; }
    update_socket_watch(link);
    check_host_key(link);
}

static gboolean on_connect_timeout(gpointer data) {
    SshLink* link = data;
    if (link->state == LINK_CONNECTING) link_fail(link, _("Timed out while connecting to the remote machine."));
    return G_SOURCE_REMOVE;
}

static gboolean on_retry_timer(gpointer data) {
    SshLink* link = data;
    clear_source(&link->retry_timer);
    if (link->state == LINK_CONNECTING) drive_connect(link);
    return G_SOURCE_REMOVE;
}

static gboolean on_socket_ready(gpointer data) {
    SshLink* link = data;
    if (link->state == LINK_CONNECTING) drive_connect(link);
    else if (link->state == LINK_AUTH) drive_auth(link);
    else if (link->state == LINK_READY) link_pump_ready(link);
    return G_SOURCE_CONTINUE;
}

static gboolean on_start_on_link(gpointer data) {
    SshLink* link = data;
    if (g_atomic_int_get(&link->closing)) {
        link_cleanup_on_thread(link, NULL);
        g_main_loop_quit(link->loop);
        return G_SOURCE_REMOVE;
    }
    ssh_session s = ssh_new();
    if (s == NULL) { link_fail(link, _("Could not initialise libssh session.")); return G_SOURCE_REMOVE; }
    link->session = s;

    int no = 0, yes = 1;
    long timeout = CONNECT_TIMEOUT_MS / 1000;
    ssh_options_set(s, SSH_OPTIONS_HOST, link->host);
    ssh_options_set(s, SSH_OPTIONS_PROCESS_CONFIG, &no);

    if (link->opt_config_file == NULL) ssh_options_parse_config(s, NULL);
    else if (link->opt_config_file[0] != '\0') ssh_options_parse_config(s, link->opt_config_file);

    if (link->user != NULL && link->user[0] != '\0') ssh_options_set(s, SSH_OPTIONS_USER, link->user);
    if (link->port > 0 && link->port != 22) {
        unsigned int p = (unsigned int)link->port;
        ssh_options_set(s, SSH_OPTIONS_PORT, &p);
    }
    if (link->identity_file != NULL && link->identity_file[0] != '\0') {
        gchar* exp = expand_tilde(link->identity_file);
        ssh_options_set(s, SSH_OPTIONS_IDENTITY, exp);
        g_free(exp);
    }
    if (link->opt_known_hosts != NULL) {
        ssh_options_set(s, SSH_OPTIONS_KNOWNHOSTS, link->opt_known_hosts);
        ssh_options_set(s, SSH_OPTIONS_GLOBAL_KNOWNHOSTS, "/dev/null");
    }
    ssh_options_set(s, SSH_OPTIONS_TIMEOUT, &timeout);
    ssh_options_set(s, SSH_OPTIONS_NODELAY, &yes);

    memset(&link->sc, 0, sizeof link->sc);
    memset(&link->rc, 0, sizeof link->rc);
    ssh_set_counters(s, &link->sc, &link->rc);
    ssh_set_blocking(s, 0);

    link->connect_timer = g_timeout_source_new(CONNECT_TIMEOUT_MS);
    g_source_set_callback(link->connect_timer, on_connect_timeout, link, NULL);
    g_source_attach(link->connect_timer, link->ctx);

    link->state = LINK_CONNECTING;
    drive_connect(link);
    return G_SOURCE_REMOVE;
}

static gboolean on_close_on_link(gpointer data) {
    SshLink* link = data;
    link_cleanup_on_thread(link, NULL);
    if (link->loop != NULL) g_main_loop_quit(link->loop);
    ssh_link_unref(link);
    return G_SOURCE_REMOVE;
}

static gpointer link_thread_main(gpointer data) {
    SshLink* link = data;
    g_main_context_push_thread_default(link->ctx);
    g_main_context_invoke(link->ctx, on_start_on_link, link);
    g_main_loop_run(link->loop);
    while (g_main_context_iteration(link->ctx, FALSE)) {}
    link_cleanup_on_thread(link, NULL);
    g_main_context_pop_thread_default(link->ctx);
    ssh_link_unref(link);
    return NULL;
}

/* Public lifecycle */

SshLink* ssh_link_connect(const SshTarget* target, SshLinkAskFunc ask, gpointer ask_data,
                          SshLinkReadyFunc ready, gpointer ready_data) {
    g_return_val_if_fail(target != NULL && target->host != NULL && target->host[0] != '\0', NULL);
    ensure_libssh_init();

    SshLink* link = g_new0(SshLink, 1);
    link->refs = 2; /* caller + link thread */
    link->host = g_strdup(target->host);
    link->user = g_strdup(target->user);
    link->port = target->port;
    link->identity_file = g_strdup(target->identity_file);
    link->reg_key = target_key(target);
    link->ask_func = ask; link->ask_data = ask_data;
    link->ready_func = ready; link->ready_data = ready_data;
    link->watched_fd = SSH_INVALID_SOCKET;
    link->channels = g_ptr_array_new_with_free_func((GDestroyNotify)ssh_channel_core_unref);
    g_mutex_init(&link->state_mu);

    g_mutex_lock(&opt_mu);
    link->opt_config_file = g_strdup(test_config_file);
    link->opt_known_hosts = g_strdup(test_known_hosts);
    link->opt_try_agent = test_try_agent;
    link->opt_try_default_keys = test_try_default_keys;
    link->keepalive_ms = default_keepalive_ms;
    g_mutex_unlock(&opt_mu);

    link->ctx = g_main_context_new();
    link->loop = g_main_loop_new(link->ctx, FALSE);
    link->thread = g_thread_new("ssh-link", link_thread_main, link);
    return link;
}

SshLink* ssh_link_ref(SshLink* link) {
    g_return_val_if_fail(link != NULL, NULL);
    g_atomic_int_inc(&link->refs);
    return link;
}

void ssh_link_close(SshLink* link) {
    if (link == NULL) return;
    g_atomic_int_set(&link->up, 0);
    gboolean first = g_atomic_int_compare_and_exchange(&link->closing, 0, 1);
    registry_remove(link);
    if (first) g_main_context_invoke(link->ctx, on_close_on_link, ssh_link_ref(link));
    g_mutex_lock(&link->state_mu);
    GThread* t = (link->thread != NULL && link->thread != g_thread_self()) ? g_steal_pointer(&link->thread) : NULL;
    g_mutex_unlock(&link->state_mu);
    if (t != NULL) g_thread_join(t);
}

void ssh_link_unref(SshLink* link) {
    if (link == NULL || !g_atomic_int_dec_and_test(&link->refs)) return;
    g_mutex_lock(&link->state_mu);
    GThread* t = g_steal_pointer(&link->thread);
    g_mutex_unlock(&link->state_mu);
    if (t != NULL) {
        if (t != g_thread_self()) g_thread_join(t);
        else g_thread_unref(t);
    }
    g_clear_pointer(&link->channels, g_ptr_array_unref);
    g_clear_pointer(&link->loop, g_main_loop_unref);
    g_clear_pointer(&link->ctx, g_main_context_unref);
    g_mutex_clear(&link->state_mu);
    g_free(link->host); g_free(link->user); g_free(link->identity_file); g_free(link->reg_key);
    g_free(link->opt_config_file); g_free(link->opt_known_hosts);
    g_free(link);
}

gboolean ssh_link_is_up(const SshLink* link) {
    return link != NULL && g_atomic_int_get(&link->up) != 0 && g_atomic_int_get(&link->closing) == 0;
}

static void driver_link_ref(gpointer data) { ssh_link_ref(data); }
static void driver_link_unref(gpointer data) { ssh_link_unref(data); }
static void driver_poke(gpointer data) {
    SshLink* link = data;
    if (link->state == LINK_READY) link_pump_ready(link);
    else prune_dead_channels(link);
}

typedef struct {
    SshLink* link;
    SshChannelCore* core;
} AttachCall;

static void link_fail_open_channel(SshChannelCore* core, GIOErrorEnum code, const gchar* msg) {
    GError* err = g_error_new_literal(G_IO_ERROR, code, msg);
    ssh_channel_fail(core, err);
    g_error_free(err);
    ssh_channel_core_unref(core);
}

static void attach_call_free(gpointer data) {
    AttachCall* ac = data;
    if (ac->core != NULL) {
        link_fail_open_channel(ac->core, G_IO_ERROR_CONNECTION_CLOSED, _("the SSH connection was closed"));
    }
    ssh_link_unref(ac->link);
    g_free(ac);
}

static gboolean on_attach_channel(gpointer data) {
    AttachCall* ac = data;
    SshLink* link = ac->link;
    SshChannelCore* core = g_steal_pointer(&ac->core);
    if (link->state != LINK_READY || link->session == NULL || !ssh_is_connected(link->session)) {
        link_fail_open_channel(core, G_IO_ERROR_NOT_CONNECTED, _("the SSH connection to the machine is not open"));
        return G_SOURCE_REMOVE;
    }
    ssh_channel raw = ssh_channel_new(link->session);
    if (raw == NULL) {
        link_fail_open_channel(core, G_IO_ERROR_FAILED, _("could not allocate an SSH channel"));
        return G_SOURCE_REMOVE;
    }
    g_ptr_array_add(link->channels, core);
    ssh_channel_attach(core, raw);
    link_pump_ready(link);
    return G_SOURCE_REMOVE;
}

static SshChannel* open_channel_common(SshLink* link, SshChannelKind kind, const gchar* arg,
                                       gint cols, gint rows, GTask* open_task, GError** error) {
    if (!ssh_link_is_up(link)) {
        GError* err = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED,
                                          _("the SSH connection to the machine is not open"));
        if (open_task != NULL) g_task_return_error(open_task, err);
        else g_propagate_error(error, err);
        return NULL;
    }
    SshChannelDriver driver = { link, link->ctx, driver_link_ref, driver_link_unref, driver_poke };
    SshChannelCore* core = NULL;
    SshChannel* ch = ssh_channel_create(&driver, kind, arg, cols, rows, open_task, &core);

    AttachCall* ac = g_new0(AttachCall, 1);
    ac->link = ssh_link_ref(link);
    ac->core = core;
    g_main_context_invoke_full(link->ctx, G_PRIORITY_DEFAULT, on_attach_channel, ac, attach_call_free);
    return ch;
}

SshChannel* ssh_link_open_channel(SshLink* link, SshChannelKind kind, const gchar* arg,
                                  gint cols, gint rows, GError** error) {
    return open_channel_common(link, kind, arg, cols, rows, NULL, error);
}

void ssh_link_open_channel_async(SshLink* link, SshChannelKind kind, const gchar* arg,
                                 gint cols, gint rows, GCancellable* cancel,
                                 GAsyncReadyCallback done, gpointer data) {
    GTask* task = g_task_new(NULL, cancel, done, data);
    g_task_set_source_tag(task, ssh_link_open_channel_async);
    SshChannel* ch = open_channel_common(link, kind, arg, cols, rows, task, NULL);
    if (ch != NULL) g_object_unref(ch);
    g_object_unref(task);
}

SshChannel* ssh_link_open_channel_finish(SshLink* link, GAsyncResult* result, GError** error) {
    (void)link;
    return g_task_propagate_pointer(G_TASK(result), error);
}

typedef struct {
    gboolean done;
    SshChannel* ch;
    GError* err;
} SyncOpen;

static void on_sync_open_done(GObject* src, GAsyncResult* res, gpointer data) {
    (void)src;
    SyncOpen* so = data;
    so->ch = g_task_propagate_pointer(G_TASK(res), &so->err);
    so->done = TRUE;
}

SshChannel* ssh_link_open_channel_sync(SshLink* link, SshChannelKind kind, const gchar* arg,
                                       gint cols, gint rows, GCancellable* cancel, GError** error) {
    g_return_val_if_fail(!g_main_context_is_owner(g_main_context_default()), NULL);
    if (link != NULL) g_return_val_if_fail(!g_main_context_is_owner(link->ctx), NULL);
    if (g_cancellable_set_error_if_cancelled(cancel, error)) return NULL;
    if (!ssh_link_is_up(link)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED,
                            _("the SSH connection to the machine is not open"));
        return NULL;
    }
    GMainContext* tmp = g_main_context_new();
    g_main_context_push_thread_default(tmp);
    SyncOpen so = { 0 };
    ssh_link_open_channel_async(link, kind, arg, cols, rows, cancel, on_sync_open_done, &so);
    while (!so.done) g_main_context_iteration(tmp, TRUE);
    g_main_context_pop_thread_default(tmp);
    g_main_context_unref(tmp);
    if (so.err != NULL) {
        g_propagate_error(error, so.err);
        return NULL;
    }
    return so.ch;
}
