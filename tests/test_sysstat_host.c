/* Unit tests for src/core/sysstat_host.c: statistics of the running machine. */
#include "core/sysstat_host.h"

#ifdef G_OS_WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

static void test_os(void) {
    gchar* os = sysstat_host_os();
    g_assert_nonnull(os);
    g_assert_cmpstr(os, !=, "");
#ifdef G_OS_WIN32
    g_assert_true(g_str_has_prefix(os, "Windows"));
#else
    if (g_file_test("/proc/sys/kernel/osrelease", G_FILE_TEST_IS_REGULAR)) {
        g_assert_true(g_str_has_prefix(os, "Linux "));
    }
#endif
    g_free(os);
}

/* Two snapshots: CPUs, memory, processes with this one among them */
static void test_sample(void) {
    if (!sysstat_host_available()) {
        g_test_skip("no system statistics on this system");
        return;
    }
    SysCpuTimes all;
    guint ncpu = 0;
    g_assert_true(sysstat_host_cpu(&all, &ncpu));
    g_assert_cmpuint(ncpu, >=, 1);
    SysMemInfo mem;
    g_assert_true(sysstat_host_mem(&mem));
    g_assert_cmpuint(mem.total, >, 0);
    g_assert_cmpuint(sysstat_mem_used(&mem), <=, mem.total);
    gdouble load[3];
    g_assert_cmpint(sysstat_host_load(load), ==, sysstat_host_has_load());

    SysSample* a = sysstat_host_sample(SYSSTAT_PROCS | SYSSTAT_MOUNTS);
    g_usleep(50000);
    SysSample* b = sysstat_host_sample(SYSSTAT_PROCS | SYSSTAT_MOUNTS);
    sysstat_sample_rates(b, a);
    g_assert_cmpuint(b->cpu->len, ==, ncpu + 1);
    g_assert_cmpuint(b->core_percent->len, ==, ncpu);
    g_assert_cmpfloat(b->cpu_percent, >=, 0.0);
    g_assert_cmpfloat(b->cpu_percent, <=, 100.0);
    g_assert_cmpuint(b->mem.total, >, 0);
    g_assert_cmpfloat(b->uptime, >, 0.0);
    g_assert_nonnull(b->net);
    g_assert_nonnull(b->disks);
    g_assert_nonnull(b->mounts);

    gboolean self = FALSE;
    for (guint i = 0; i < b->procs->len; i++) {
        SysProc* p = g_ptr_array_index(b->procs, i);
        g_assert_nonnull(p->name);
        g_assert_nonnull(p->user);
        if (p->pid == (gint)getpid()) self = TRUE;
    }
    g_assert_true(self);
    sysstat_sample_free(a);
    sysstat_sample_free(b);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/sysstat_host/os", test_os);
    g_test_add_func("/sysstat_host/sample", test_sample);
    return g_test_run();
}
