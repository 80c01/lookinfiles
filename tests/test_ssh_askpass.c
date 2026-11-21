/* Unit tests for src/core/ssh_askpass.c: prompts of the helper relayed to
 * the instance over loopback TCP. The helper side runs in a thread, the
 * instance in the main loop. */
#include "core/ssh_askpass.h"

#include <string.h>

typedef struct {
    gchar* address;
    SshAskpassKind kind;
    const gchar* prompt;
    gboolean ok;
    gchar* answer;
    GMainLoop* loop;
} Ask;

static gpointer helper_thread(gpointer data) {
    Ask* a = data;
    a->ok = ssh_askpass_request(a->address, a->kind, a->prompt, &a->answer, NULL);
    g_main_loop_quit(a->loop); /* thread-safe */
    return NULL;
}

/* Run a request against the instance; the answer is what the ask function gives */
static void run(Ask* a) {
    a->loop = g_main_loop_new(NULL, FALSE);
    GThread* th = g_thread_new("askpass-helper", helper_thread, a);
    g_main_loop_run(a->loop);
    g_thread_join(th);
    g_main_loop_unref(a->loop);
}

static const gchar* next_answer; /* NULL: cancel */
static gchar* last_prompt;
static SshAskpassKind last_kind;
static gboolean last_request_null;

static void on_ask(SshAskpassRequest* req, SshAskpassKind kind, const gchar* prompt, gpointer data) {
    (void)data;
    g_free(last_prompt);
    last_prompt = g_strdup(prompt);
    last_kind = kind;
    last_request_null = req == NULL;
    if (req != NULL) ssh_askpass_reply(req, next_answer);
}

static void test_roundtrip(void) {
    g_assert_true(ssh_askpass_serve("/opt/lookinfiles", on_ask, NULL, NULL));
    const gchar* addr = ssh_askpass_address();
    g_assert_nonnull(addr);
    g_assert_nonnull(strchr(addr, ':'));

    Ask a = { g_strdup(addr), SSH_ASKPASS_SECRET, "(me@srv) Password: «é»", FALSE, NULL, NULL };
    next_answer = "pa ss\\word é";
    run(&a);
    g_assert_true(a.ok);
    g_assert_cmpstr(a.answer, ==, "pa ss\\word é");
    g_assert_cmpstr(last_prompt, ==, "(me@srv) Password: «é»");
    g_assert_cmpint(last_kind, ==, SSH_ASKPASS_SECRET);
    g_clear_pointer(&a.answer, g_free);

    /* Cancelled */
    next_answer = NULL;
    a.kind = SSH_ASKPASS_CONFIRM;
    run(&a);
    g_assert_false(a.ok);
    g_assert_null(a.answer);
    g_assert_cmpint(last_kind, ==, SSH_ASKPASS_CONFIRM);

    /* A notice is answered at once */
    a.kind = SSH_ASKPASS_NOTICE;
    a.prompt = "Confirm user presence";
    run(&a);
    g_assert_true(a.ok);
    g_assert_true(last_request_null);
    g_clear_pointer(&a.answer, g_free);

    /* A wrong token gets nothing, and the ask function is not called */
    g_clear_pointer(&last_prompt, g_free);
    gchar* port = g_strndup(addr, (gsize)(strchr(addr, ':') - addr));
    g_free(a.address);
    a.address = g_strconcat(port, ":0000", NULL);
    a.kind = SSH_ASKPASS_SECRET;
    next_answer = "secret";
    run(&a);
    g_assert_false(a.ok);
    g_assert_null(last_prompt);
    g_free(port);
    g_free(a.address);

    ssh_askpass_stop();
    g_assert_null(ssh_askpass_address());
}

static void test_is_helper(void) {
    char* args[] = { (char*)"lookinfiles", (char*)"Password:", NULL };
    g_unsetenv(SSH_ASKPASS_ENV);
    g_assert_false(ssh_askpass_is_helper(2, args));
    g_setenv(SSH_ASKPASS_ENV, "1234:abcd", TRUE);
    g_assert_true(ssh_askpass_is_helper(2, args));
    g_assert_false(ssh_askpass_is_helper(1, args));
    g_unsetenv(SSH_ASKPASS_ENV);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ssh_askpass/is_helper", test_is_helper); /* first: sets variables before any thread */
    g_test_add_func("/ssh_askpass/roundtrip", test_roundtrip);
    gint rc = g_test_run();
    g_free(last_prompt);
    return rc;
}
