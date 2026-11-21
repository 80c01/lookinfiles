/* Unit tests for src/core/sysstat_remote.c: the command run by the local sh
 * (this machine is a Linux), and a made-up output. */
#include "core/sysstat_remote.h"

#include <string.h>
#include <unistd.h>

static gchar* run_sh(const gchar* cmd) {
    const gchar* argv[] = { "/bin/sh", "-c", cmd, NULL };
    gchar* out = NULL;
    gint status = -1;
    g_assert_true(g_spawn_sync(NULL, (gchar**)argv, NULL, 0, NULL, NULL, &out, NULL, &status, NULL));
    return out;
}

static void test_local_run(void) {
    if (!g_file_test("/proc/stat", G_FILE_TEST_EXISTS)) {
        g_test_skip("no /proc");
        return;
    }
    SysSampleFlags flags = SYSSTAT_PROCS | SYSSTAT_MOUNTS;
    gchar* cmd = sysstat_remote_command(flags);
    gchar* out = run_sh(cmd);
    gchar* host = NULL;
    gchar* os = NULL;
    guint64 page = 0;
    SysSample* s = sysstat_remote_parse(out, flags, &host, &os, &page);
    g_assert_nonnull(s);
    g_assert_cmpuint(s->cpu->len, >=, 2); /* all CPUs, then each */
    g_assert_cmpuint(s->mem.total, >, 0);
    g_assert_cmpfloat(s->uptime, >, 0);
    g_assert_cmpint(s->time_us, ==, (gint64)(s->uptime * G_USEC_PER_SEC));
    g_assert_nonnull(host);
    g_assert_true(g_str_has_prefix(os, "Linux "));
    g_assert_cmpuint(page, ==, (guint64)sysconf(_SC_PAGESIZE));
    g_assert_nonnull(s->procs);
    g_assert_cmpuint(s->procs->len, >, 0);
    gboolean found_self = FALSE;
    for (guint i = 0; i < s->procs->len; i++) {
        SysProc* p = g_ptr_array_index(s->procs, i);
        if (p->pid == (gint)getpid()) {
            found_self = TRUE;
            g_assert_cmpuint(p->uid, ==, (guint)getuid());
            g_assert_nonnull(p->user);
        }
    }
    g_assert_true(found_self);
    g_assert_nonnull(s->mounts); /* sizes read with df */
    for (guint i = 0; i < s->mounts->len; i++) {
        SysMount* m = g_ptr_array_index(s->mounts, i);
        g_assert_cmpuint(m->total, >, 0);
    }

    /* Two readings: rates from the machine's clock */
    gchar* out2 = run_sh(cmd);
    SysSample* s2 = sysstat_remote_parse(out2, flags, NULL, NULL, NULL);
    sysstat_sample_rates(s2, s);
    g_assert_cmpfloat(s2->cpu_percent, >=, 0);
    g_assert_cmpfloat(s2->cpu_percent, <=, 100);
    sysstat_sample_free(s2);
    g_free(out2);

    sysstat_sample_free(s);
    g_free(host);
    g_free(os);
    g_free(out);
    g_free(cmd);
}

static const gchar* MADE_UP =
    "@@stat\n"
    "cpu  10 0 10 80 0 0 0 0\n"
    "cpu0 10 0 10 80 0 0 0 0\n"
    "@@meminfo\n"
    "MemTotal:       2048 kB\n"
    "MemAvailable:   1024 kB\n"
    "@@loadavg\n"
    "0.50 0.25 0.10 2/100 1234\n"
    "@@uptime\n"
    "100.50 90.00\n"
    "@@netdev\n"
    "Inter-|   Receive\n"
    " face |bytes\n"
    "  eth0: 1000 0 0 0 0 0 0 0 2000 0 0 0 0 0 0 0\n"
    "@@diskstats\n"
    "   8       0 sda 1 0 10 0 1 0 20 0 0 5 0\n"
    "   8       1 sda1 1 0 10 0 1 0 20 0 0 5 0\n"
    "   7       0 loop0 1 0 10 0 1 0 20 0 0 5 0\n"
    "@@block\n"
    "loop0\n"
    "sda\n"
    "@@host\n"
    "srv\n"
    "@@os\n"
    "Linux 6.1.0\n"
    "@@pagesize\n"
    "65536\n"
    "@@mounts\n"
    "/dev/sda1 / ext4 rw 0 0\n"
    "/dev/sdb1 /mnt/my\\040disk ext4 rw 0 0\n"
    "/dev/sdc1 /gone ext4 rw 0 0\n"
    "@@df\n"
    "Filesystem     1024-blocks  Used Available Capacity Mounted on\n"
    "/dev/sda1           1000   400       600      40% /\n"
    "/dev/sdb1           2000  1000      1000      50% /mnt/my disk\n"
    "@@procstat\n"
    "1 (init) S 0 1 1 0 -1 0 0 0 0 0 5 5 0 0 20 0 1 0 1 1000 50 0\n"
    "42 (my prog) R 1 42 42 0 -1 0 0 0 0 0 1 2 0 0 20 0 3 0 1 1000 70 0\n"
    "@@procuid\n"
    "0 /proc/1\n"
    "1000 /proc/42\n"
    "@@passwd\n"
    "root:0\n"
    "@@end\n";

static void test_made_up(void) {
    gchar* host = NULL;
    gchar* os = NULL;
    guint64 page = 0;
    SysSample* s = sysstat_remote_parse(MADE_UP, SYSSTAT_PROCS | SYSSTAT_MOUNTS, &host, &os, &page);
    g_assert_nonnull(s);
    g_assert_cmpstr(host, ==, "srv");
    g_assert_cmpstr(os, ==, "Linux 6.1.0");
    g_assert_cmpuint(page, ==, 65536);
    g_assert_cmpuint(s->cpu->len, ==, 2);
    g_assert_cmpuint(s->mem.total, ==, 2048 * 1024);
    g_assert_cmpfloat(s->load[0], ==, 0.5);
    g_assert_cmpint(s->time_us, ==, 100500000);
    g_assert_cmpuint(s->net->len, ==, 1);
    g_assert_cmpuint(s->disks->len, ==, 1); /* sda: not the partition, not loop0 */
    g_assert_cmpuint(s->mounts->len, ==, 2); /* /gone has no df line */
    SysMount* m = g_ptr_array_index(s->mounts, 1);
    g_assert_cmpstr(m->mount_point, ==, "/mnt/my disk");
    g_assert_cmpuint(m->total, ==, 2000 * 1024);
    g_assert_cmpuint(m->avail, ==, 1000 * 1024);
    g_assert_cmpuint(s->procs->len, ==, 2);
    SysProc* p = g_ptr_array_index(s->procs, 1);
    g_assert_cmpstr(p->name, ==, "my prog");
    g_assert_cmpuint(p->uid, ==, 1000);
    g_assert_cmpstr(p->user, ==, "1000"); /* not in passwd */
    g_assert_cmpstr(((SysProc*)g_ptr_array_index(s->procs, 0))->user, ==, "root");
    sysstat_sample_free(s);
    g_free(host);
    g_free(os);

    /* Without the processes and filesystems asked */
    s = sysstat_remote_parse(MADE_UP, 0, NULL, NULL, NULL);
    g_assert_null(s->procs);
    g_assert_null(s->mounts);
    sysstat_sample_free(s);

    g_assert_null(sysstat_remote_parse("ssh: connect to host srv: Connection refused\n", 0, NULL, NULL, NULL));
    g_assert_null(sysstat_remote_parse("", 0, NULL, NULL, NULL));
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/sysstat_remote/local_run", test_local_run);
    g_test_add_func("/sysstat_remote/made_up", test_made_up);
    return g_test_run();
}
