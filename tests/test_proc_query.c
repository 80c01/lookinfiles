/* Unit tests for src/core/proc_query.c: the process list filter. */
#include "core/proc_query.h"

static ProcQueryRow row(const gchar* name, const gchar* user, gint pid, gdouble cpu, guint64 rss, guint threads) {
    ProcQueryRow r = {name, user, pid, cpu, rss, threads};
    return r;
}

/* Words and values both match */
static gboolean matches(const gchar* text, const ProcQueryRow* r) {
    ProcQuery* q = proc_query_parse(text);
    gboolean hit = proc_query_match_words(q, r) && proc_query_match_values(q, r);
    proc_query_free(q);
    return hit;
}

static gboolean has_conditions(const gchar* text) {
    ProcQuery* q = proc_query_parse(text);
    gboolean c = proc_query_has_conditions(q);
    proc_query_free(q);
    return c;
}

static void test_empty(void) {
    g_assert_null(proc_query_parse(NULL));
    g_assert_null(proc_query_parse(""));
    g_assert_null(proc_query_parse("  \t "));
    g_assert_null(proc_query_parse("cpu>")); /* being typed: left out */
    ProcQueryRow r = row("bash", "dev", 12, 0.0, 0, 1);
    g_assert_true(proc_query_match_words(NULL, &r));
    g_assert_true(proc_query_match_values(NULL, &r));
    g_assert_false(proc_query_has_conditions(NULL));
}

static void test_words(void) {
    ProcQueryRow r = row("Firefox", "dev", 4321, 3.0, 500000000, 80);
    g_assert_true(matches("fire", &r));
    g_assert_true(matches("FIREFOX dev", &r));
    g_assert_true(matches("432", &r));
    g_assert_false(matches("firefox root", &r));
    g_assert_false(matches("chrome", &r));
    g_assert_false(has_conditions("firefox dev"));
}

static void test_cpu(void) {
    ProcQueryRow busy = row("cc1", "dev", 1, 55.5, 0, 1);
    ProcQueryRow idle = row("sshd", "root", 2, 0.0, 0, 1);
    g_assert_true(matches("cpu>10", &busy));
    g_assert_false(matches("cpu>10", &idle));
    g_assert_true(matches("cpu>10%", &busy));
    g_assert_true(matches("CPU>=55.5", &busy));
    g_assert_false(matches("cpu>55.5", &busy));
    g_assert_true(matches("cpu<1", &idle));
    g_assert_true(matches("cpu<=0", &idle));
    g_assert_true(matches("cpu>55,4", &busy)); /* decimal comma */
    g_assert_true(has_conditions("cpu>10"));
}

static void test_mem(void) {
    ProcQueryRow r = row("java", "dev", 3, 0.0, 150000000, 40); /* 150 MB */
    g_assert_true(matches("mem>100M", &r));
    g_assert_true(matches("mem>100", &r)); /* no unit: MB */
    g_assert_false(matches("mem>200", &r));
    g_assert_true(matches("mem>100mb", &r));
    g_assert_true(matches("mem>100Mo", &r));
    g_assert_true(matches("mem<1G", &r));
    g_assert_false(matches("mem>0.2g", &r));
    g_assert_true(matches("mem>1,5k", &r));
    g_assert_true(matches("mem<1T", &r));
    g_assert_true(matches("mem>149999999b", &r));
}

static void test_threads(void) {
    ProcQueryRow r = row("java", "dev", 3, 0.0, 0, 40);
    g_assert_true(matches("threads>=40", &r));
    g_assert_false(matches("threads>40", &r));
    g_assert_true(matches("threads<41", &r));
}

static void test_mixed(void) {
    ProcQueryRow fx = row("firefox", "dev", 10, 12.0, 800000000, 90);
    ProcQueryRow small = row("firefox", "dev", 11, 0.5, 50000000, 20);
    g_assert_true(matches("firefox mem>500M", &fx));
    g_assert_false(matches("firefox mem>500M", &small));
    g_assert_true(matches("cpu>10 mem>500", &fx));
    g_assert_false(matches("cpu>10 mem>900", &fx));
    /* A condition being typed is left out, the rest still filters */
    g_assert_true(matches("firefox mem>", &small));
    g_assert_true(matches("firefox mem>1x", &small));
    g_assert_false(matches("firefox cpu>1.", &small)); /* cpu>1. reads as 1 */
    /* Not a condition: a word */
    g_assert_false(matches("cpus", &fx));
    g_assert_false(matches("memory", &fx));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/proc_query/empty", test_empty);
    g_test_add_func("/proc_query/words", test_words);
    g_test_add_func("/proc_query/cpu", test_cpu);
    g_test_add_func("/proc_query/mem", test_mem);
    g_test_add_func("/proc_query/threads", test_threads);
    g_test_add_func("/proc_query/mixed", test_mixed);
    return g_test_run();
}
