/* Unit tests for src/jupyter/ipynb_kernel.c, with a fake bridge
 * (tests/fixtures/fake_ipynb_bridge.py). */
#include "jupyter/ipynb_kernel.h"

#include <string.h>

static const gchar* const FAKE[] = { "python3", "tests/fixtures/fake_ipynb_bridge.py", NULL };

typedef struct {
    GString* log;        /* one line per callback */
    IpynbKernel* kernel;
    IpynbKernelState state;
    guint done;
    gboolean failed;
} Run;

static void on_state(IpynbKernelState state, gpointer data) {
    Run* r = data;
    r->state = state;
}

static void on_started(const gchar* spec, const gchar* info, const gchar* display, gpointer data) {
    Run* r = data;
    g_assert_nonnull(strstr(spec, "\"name\": \"fake\""));
    g_assert_nonnull(strstr(info, "\".py\""));
    g_string_append_printf(r->log, "started %s\n", display);
}

static void on_failed(const gchar* code, const gchar* message, gpointer data) {
    Run* r = data;
    r->failed = TRUE;
    g_string_append_printf(r->log, "failed %s %s\n", code, message);
}

static void on_input(guint cell, gint count, gpointer data) {
    Run* r = data;
    g_string_append_printf(r->log, "input %u %d\n", cell, count);
}

static void on_output(guint cell, const gchar* json, gpointer data) {
    Run* r = data;
    g_string_append_printf(r->log, "output %u %s\n", cell, json);
}

static void on_clear(guint cell, gboolean wait, gpointer data) {
    Run* r = data;
    g_string_append_printf(r->log, "clear %u %d\n", cell, wait);
}

static void on_done(guint cell, const gchar* status, gpointer data) {
    Run* r = data;
    r->done++;
    g_string_append_printf(r->log, "done %u %s\n", cell, status);
}

static void on_input_request(const gchar* prompt, gboolean password, gpointer data) {
    Run* r = data;
    g_string_append_printf(r->log, "ask %s%d\n", prompt, password);
    ipynb_kernel_input(r->kernel, "Ada");
}

static const IpynbKernelCallbacks CALLBACKS = { on_state, on_started, on_failed, on_input,
                                                on_output, on_clear, on_done, on_input_request };

/* Run the main loop until cond holds (or 10 s) */
static gboolean wait_for(Run* r, gboolean (*cond)(Run*)) {
    gint64 end = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
    while (!cond(r) && g_get_monotonic_time() < end) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    return cond(r);
}

static gboolean three_done(Run* r) {
    return r->done >= 3 && r->state == IPYNB_KERNEL_IDLE;
}

static gboolean has_failed(Run* r) {
    return r->failed;
}

static void test_execute(void) {
    Run r = { g_string_new(NULL), NULL, IPYNB_KERNEL_OFF, 0, FALSE };
    r.kernel = ipynb_kernel_new(&CALLBACKS, &r);
    g_assert_cmpint(ipynb_kernel_get_state(r.kernel), ==, IPYNB_KERNEL_OFF);
    g_assert_true(ipynb_kernel_start(r.kernel, FAKE, "fake", ".", NULL));
    g_assert_cmpint(r.state, ==, IPYNB_KERNEL_STARTING);
    /* Queued before the kernel runs: sent in order */
    ipynb_kernel_execute(r.kernel, 7, "print(\"é\\t\")\nx");
    ipynb_kernel_execute(r.kernel, 8, "fail");
    ipynb_kernel_execute(r.kernel, 9, "ask");
    g_assert_true(wait_for(&r, three_done));
    g_assert_cmpstr(r.log->str, ==,
                    "started Fake\n"
                    "input 7 1\n"
                    "clear 7 1\n"
                    "output 7 {\"name\": \"stdout\", \"output_type\": \"stream\", \"text\": \"print(\\\"\\u00e9\\\\t\\\")\\nx\\n\"}\n"
                    "done 7 ok\n"
                    "input 8 2\n"
                    "output 8 {\"ename\": \"ValueError\", \"evalue\": \"bad\", \"output_type\": \"error\", "
                    "\"traceback\": [\"ValueError: bad\"]}\n"
                    "done 8 error\n"
                    "input 9 3\n"
                    "ask Name? 0\n"
                    "output 9 {\"name\": \"stdout\", \"output_type\": \"stream\", \"text\": \"hello Ada\\n\"}\n"
                    "done 9 ok\n");
    g_assert_false(r.failed);

    /* The bridge exits: failed, off */
    ipynb_kernel_execute(r.kernel, 10, "quit");
    g_assert_true(wait_for(&r, has_failed));
    g_assert_cmpint(r.state, ==, IPYNB_KERNEL_OFF);
    g_assert_true(g_str_has_suffix(r.log->str, "input 10 4\nfailed exited \n"));
    ipynb_kernel_free(r.kernel);
    g_string_free(r.log, TRUE);
}

static void test_failures(void) {
    Run r = { g_string_new(NULL), NULL, IPYNB_KERNEL_OFF, 0, FALSE };
    r.kernel = ipynb_kernel_new(&CALLBACKS, &r);
    /* A kernel that is not installed */
    g_assert_true(ipynb_kernel_start(r.kernel, FAKE, "missing", NULL, NULL));
    g_assert_true(wait_for(&r, has_failed));
    g_assert_cmpstr(r.log->str, ==, "failed no-kernel No such kernel named missing\n");
    g_assert_cmpint(r.state, ==, IPYNB_KERNEL_OFF);
    ipynb_kernel_free(r.kernel);

    /* A bridge that cannot run */
    static const gchar* const NONE[] = { "/nonexistent/python3", "-c", "", NULL };
    IpynbKernel* k = ipynb_kernel_new(NULL, NULL);
    GError* error = NULL;
    g_assert_false(ipynb_kernel_start(k, NONE, NULL, NULL, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    ipynb_kernel_free(k);

    /* Freed while running: no callback after, the bridge stops */
    Run q = { g_string_new(NULL), NULL, IPYNB_KERNEL_OFF, 0, FALSE };
    q.kernel = ipynb_kernel_new(&CALLBACKS, &q);
    g_assert_true(ipynb_kernel_start(q.kernel, FAKE, NULL, NULL, NULL));
    ipynb_kernel_execute(q.kernel, 1, "x");
    ipynb_kernel_free(q.kernel);
    for (gint i = 0; i < 200; i++) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    g_assert_cmpstr(q.log->str, ==, "");
    g_string_free(q.log, TRUE);
    g_string_free(r.log, TRUE);
}

typedef struct {
    GString* log;
    gboolean done;
} Listing;

static void on_listed(GPtrArray* specs, const gchar* code, const gchar* message, gpointer data) {
    Listing* l = data;
    l->done = TRUE;
    if (specs == NULL) {
        g_string_append_printf(l->log, "failed %s\n", code);
        g_assert_nonnull(message);
        return;
    }
    for (guint i = 0; i < specs->len; i++) {
        IpynbKernelSpec* spec = g_ptr_array_index(specs, i);
        gchar* json = ipynb_kernel_spec_to_json(spec);
        gchar* info = ipynb_kernel_spec_language_info(spec);
        g_string_append_printf(l->log, "%s %s\n", json, info);
        g_free(json);
        g_free(info);
    }
    g_ptr_array_unref(specs);
}

/* Run the main loop until l is done (or 10 s) */
static gboolean wait_listed(Listing* l) {
    gint64 end = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
    while (!l->done && g_get_monotonic_time() < end) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    return l->done;
}

static void test_list(void) {
    Listing l = { g_string_new(NULL), FALSE };
    ipynb_kernel_list(FAKE, NULL, on_listed, &l);
    g_assert_true(wait_listed(&l));
    g_assert_cmpstr(l.log->str, ==,
                    "{\"display_name\": \"Fake\", \"language\": \"python\", \"name\": \"fake\"} {\"name\": \"python\"}\n"
                    "{\"display_name\": \"R \\\"4\\\"\", \"language\": \"R\", \"name\": \"ir\"} {\"name\": \"R\"}\n");

    /* No language: no language_info */
    IpynbKernelSpec bare = { "x", "X", "" };
    g_assert_null(ipynb_kernel_spec_language_info(&bare));

    /* A bridge that says nothing, or cannot run */
    static const gchar* const SILENT[] = { "python3", "-c", "", NULL };
    static const gchar* const NONE[] = { "/nonexistent/python3", "-c", "", NULL };
    g_string_truncate(l.log, 0);
    l.done = FALSE;
    ipynb_kernel_list(SILENT, NULL, on_listed, &l);
    g_assert_true(wait_listed(&l));
    l.done = FALSE;
    ipynb_kernel_list(NONE, NULL, on_listed, &l);
    g_assert_false(l.done); /* in the main loop */
    g_assert_true(wait_listed(&l));
    g_assert_cmpstr(l.log->str, ==, "failed exited\nfailed start\n");

    /* Cancelled: never called */
    GCancellable* cancel = g_cancellable_new();
    l.done = FALSE;
    ipynb_kernel_list(FAKE, cancel, on_listed, &l);
    ipynb_kernel_list(NONE, cancel, on_listed, &l);
    g_cancellable_cancel(cancel);
    for (gint i = 0; i < 300; i++) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    g_assert_false(l.done);
    g_object_unref(cancel);
    g_string_free(l.log, TRUE);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ipynb_kernel/execute", test_execute);
    g_test_add_func("/ipynb_kernel/failures", test_failures);
    g_test_add_func("/ipynb_kernel/list", test_list);
    return g_test_run();
}
