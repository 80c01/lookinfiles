/* Unit tests for src/core/sysstat.c: /proc parsers, snapshots and rates. */
#include "core/sysstat.h"

#include <glib/gstdio.h>

static const gchar* STAT_1 =
    "cpu  100 0 50 800 50 0 0 0 0 0\n"
    "cpu0 50 0 25 400 25 0 0 0 0 0\n"
    "cpu1 50 0 25 400 25 0 0 0 0 0\n"
    "intr 12345\n"
    "ctxt 999\n";
static const gchar* STAT_2 =
    "cpu  200 0 100 1100 100 0 0 0 0 0\n"
    "cpu0 150 0 75 500 25 0 0 0 0 0\n"
    "cpu1 50 0 25 600 75 0 0 0 0 0\n";

static void test_cpu(void) {
    GArray* a = g_array_new(FALSE, TRUE, sizeof(SysCpuTimes));
    GArray* b = g_array_new(FALSE, TRUE, sizeof(SysCpuTimes));
    g_assert_true(sysstat_parse_cpu(STAT_1, a));
    g_assert_true(sysstat_parse_cpu(STAT_2, b));
    g_assert_cmpuint(a->len, ==, 3);
    SysCpuTimes* t = &g_array_index(a, SysCpuTimes, 0);
    g_assert_cmpuint(t->user, ==, 100);
    g_assert_cmpuint(t->system, ==, 50);
    g_assert_cmpuint(t->idle, ==, 800);
    g_assert_cmpuint(t->iowait, ==, 50);
    /* 500 ticks passed, 350 idle or waiting: 30% busy */
    g_assert_cmpuint(sysstat_cpu_elapsed(t, &g_array_index(b, SysCpuTimes, 0)), ==, 500);
    g_assert_cmpfloat_with_epsilon(sysstat_cpu_percent(t, &g_array_index(b, SysCpuTimes, 0)), 30.0, 1e-9);
    /* cpu0: 150 of 250 ticks busy; cpu1: none */
    g_assert_cmpfloat_with_epsilon(
        sysstat_cpu_percent(&g_array_index(a, SysCpuTimes, 1), &g_array_index(b, SysCpuTimes, 1)), 60.0, 1e-9);
    g_assert_cmpfloat_with_epsilon(
        sysstat_cpu_percent(&g_array_index(a, SysCpuTimes, 2), &g_array_index(b, SysCpuTimes, 2)), 0.0, 1e-9);
    /* No time passed */
    g_assert_cmpfloat(sysstat_cpu_percent(t, t), ==, 0.0);
    g_array_set_size(a, 0);
    g_assert_false(sysstat_parse_cpu("intr 1\n", a));
    g_assert_false(sysstat_parse_cpu(NULL, a));
    g_array_unref(a);
    g_array_unref(b);
}

static void test_meminfo(void) {
    SysMemInfo m;
    g_assert_true(sysstat_parse_meminfo("MemTotal:       16000 kB\n"
                                        "MemFree:         2000 kB\n"
                                        "MemAvailable:    9000 kB\n"
                                        "Buffers:          500 kB\n"
                                        "Cached:          5000 kB\n"
                                        "SwapCached:       100 kB\n"
                                        "SReclaimable:     300 kB\n"
                                        "SwapTotal:       4000 kB\n"
                                        "SwapFree:        3000 kB\n",
                                        &m));
    g_assert_cmpuint(m.total, ==, 16000 * 1024);
    g_assert_cmpuint(m.available, ==, 9000 * 1024);
    g_assert_cmpuint(m.cached, ==, 5300 * 1024); /* SwapCached is not Cached */
    g_assert_cmpuint(m.swap_total, ==, 4000 * 1024);
    g_assert_cmpuint(m.swap_free, ==, 3000 * 1024);
    g_assert_cmpuint(sysstat_mem_used(&m), ==, 7000 * 1024);

    /* Old kernels: no MemAvailable */
    g_assert_true(sysstat_parse_meminfo("MemTotal: 1000 kB\nMemFree: 100 kB\nBuffers: 50 kB\nCached: 200 kB\n", &m));
    g_assert_cmpuint(m.available, ==, 350 * 1024);
    g_assert_false(sysstat_parse_meminfo("MemFree: 100 kB\n", &m));
}

static void test_loadavg(void) {
    gdouble load[3];
    guint running = 0, total = 0;
    g_assert_true(sysstat_parse_loadavg("0.52 1.25 2.00 3/812 45678\n", load, &running, &total));
    g_assert_cmpfloat_with_epsilon(load[0], 0.52, 1e-9);
    g_assert_cmpfloat_with_epsilon(load[1], 1.25, 1e-9);
    g_assert_cmpfloat_with_epsilon(load[2], 2.0, 1e-9);
    g_assert_cmpuint(running, ==, 3);
    g_assert_cmpuint(total, ==, 812);
    g_assert_false(sysstat_parse_loadavg("", load, NULL, NULL));
}

static void test_netdev(void) {
    GPtrArray* devs = sysstat_parse_netdev(
        "Inter-|   Receive                                                |  Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
        "    lo:  123456     100    0    0    0     0          0         0   123456     100    0    0    0     0       0          0\n"
        "  eth0: 1000000    2000    0    0    0     0          0         5   500000    1500    0    0    0     0       0          0\n"
        "wlan0:42 1 0 0 0 0 0 0 24 1 0 0 0 0 0 0\n");
    g_assert_cmpuint(devs->len, ==, 2);
    SysNetDev* d = g_ptr_array_index(devs, 0);
    g_assert_cmpstr(d->name, ==, "eth0");
    g_assert_cmpuint(d->rx_bytes, ==, 1000000);
    g_assert_cmpuint(d->tx_bytes, ==, 500000);
    d = g_ptr_array_index(devs, 1);
    g_assert_cmpstr(d->name, ==, "wlan0");
    g_assert_cmpuint(d->rx_bytes, ==, 42);
    g_assert_cmpuint(d->tx_bytes, ==, 24);
    g_ptr_array_unref(devs);
}

static gboolean keep_sda(const gchar* name, gpointer data) {
    (*(gint*)data)++;
    return g_strcmp0(name, "sda") == 0;
}

static void test_diskstats(void) {
    const gchar* text =
        "   7       0 loop0 10 0 20 0 0 0 0 0 0 0 0\n"
        "   8       0 sda 100 5 2000 30 50 2 1000 40 0 70 70 0 0 0 0\n"
        "   8       1 sda1 90 5 1800 30 50 2 1000 40 0 60 60\n"
        " 259       0 nvme0n1 1 0 8 0 2 0 16 0 0 5 5\n";
    GPtrArray* all = sysstat_parse_diskstats(text, NULL, NULL);
    g_assert_cmpuint(all->len, ==, 3); /* loop0 left out */
    SysDiskDev* d = g_ptr_array_index(all, 0);
    g_assert_cmpstr(d->name, ==, "sda");
    g_assert_cmpuint(d->read_sectors, ==, 2000);
    g_assert_cmpuint(d->write_sectors, ==, 1000);
    g_assert_cmpuint(d->io_ms, ==, 70);
    g_ptr_array_unref(all);

    gint calls = 0;
    GPtrArray* some = sysstat_parse_diskstats(text, keep_sda, &calls);
    g_assert_cmpint(calls, ==, 4);
    g_assert_cmpuint(some->len, ==, 1);
    g_ptr_array_unref(some);
}

static void test_mounts(void) {
    GPtrArray* m = sysstat_parse_mounts("proc /proc proc rw 0 0\n"
                                        "/dev/sda2 / ext4 rw,relatime 0 0\n"
                                        "tmpfs /tmp tmpfs rw 0 0\n"
                                        "/dev/sda1 /boot/efi vfat rw 0 0\n"
                                        "/dev/loop3 /snap/core/1 squashfs ro 0 0\n"
                                        "/dev/sda2 /home ext4 rw 0 0\n"
                                        "/dev/sdb1 /media/My\\040Disk ext4 rw 0 0\n");
    g_assert_cmpuint(m->len, ==, 3);
    SysMount* a = g_ptr_array_index(m, 0);
    g_assert_cmpstr(a->device, ==, "/dev/sda2");
    g_assert_cmpstr(a->mount_point, ==, "/");
    g_assert_cmpstr(a->fs_type, ==, "ext4");
    g_assert_cmpstr(((SysMount*)g_ptr_array_index(m, 1))->mount_point, ==, "/boot/efi");
    g_assert_cmpstr(((SysMount*)g_ptr_array_index(m, 2))->mount_point, ==, "/media/My Disk");
    g_ptr_array_unref(m);

    SysMount root = { NULL, (gchar*)"/", NULL, 0, 0, 0 };
#ifdef G_OS_UNIX
    g_assert_true(sysstat_mount_fill(&root));
    g_assert_cmpuint(root.total, >, 0);
    g_assert_cmpuint(root.used, <=, root.total);
#endif
}

static void test_pid_stat(void) {
    SysProc p = { 0 };
    /* A name with a space and a parenthesis */
    g_assert_true(sysstat_parse_pid_stat("1234 (my (app) x) S 1 1234 1234 0 -1 4194560 500 0 0 0 "
                                         "250 150 0 0 20 0 7 0 12345 100000000 2048 18446744073709551615\n",
                                         &p));
    g_assert_cmpint(p.pid, ==, 1234);
    g_assert_cmpstr(p.name, ==, "my (app) x");
    g_assert_cmpint(p.state, ==, 'S');
    g_assert_cmpuint(p.cpu_ticks, ==, 400);
    g_assert_cmpuint(p.threads, ==, 7);
    g_assert_cmpuint(p.rss_pages, ==, 2048);
    g_free(p.name);
    g_assert_false(sysstat_parse_pid_stat("garbage", &p));
}

static void write_file(const gchar* dir, const gchar* name, const gchar* text) {
    gchar* path = g_build_filename(dir, name, NULL);
    gchar* parent = g_path_get_dirname(path);
    g_assert_cmpint(g_mkdir_with_parents(parent, 0755), ==, 0);
    g_assert_true(g_file_set_contents(path, text, -1, NULL));
    g_free(parent);
    g_free(path);
}

static void remove_tree(const gchar* path) {
    GDir* d = g_dir_open(path, 0, NULL);
    if (d != NULL) {
        const gchar* n;
        while ((n = g_dir_read_name(d)) != NULL) {
            gchar* child = g_build_filename(path, n, NULL);
            remove_tree(child);
            g_free(child);
        }
        g_dir_close(d);
    }
    g_remove(path);
}

/* A fake /proc read twice, one second apart (the times are set by hand) */
static void test_sample(void) {
    gchar* root = g_dir_make_tmp("sysstat_XXXXXX", NULL);
    g_assert_nonnull(root);
    g_assert_false(sysstat_available(root));
    write_file(root, "stat", STAT_1);
    write_file(root, "meminfo", "MemTotal: 1000 kB\nMemAvailable: 400 kB\n");
    write_file(root, "loadavg", "1.00 0.50 0.25 2/100 1\n");
    write_file(root, "uptime", "3600.5 7000.0\n");
    write_file(root, "net/dev", "h\nh\neth0: 1000 0 0 0 0 0 0 0 2000 0 0 0 0 0 0 0\n");
    write_file(root, "diskstats", "8 0 sda 0 0 100 0 0 0 200 0 0 100 0\n");
    write_file(root, "self/mounts", "/dev/sda1 / ext4 rw 0 0\n");
    write_file(root, "42/stat", "42 (worker) R 1 42 42 0 -1 0 0 0 0 0 10 10 0 0 20 0 3 0 1 1 10\n");
    write_file(root, "self/stat", "1 (not a pid dir) S 0\n");
    g_assert_true(sysstat_available(root));

    SysSample* a = sysstat_sample(root, SYSSTAT_PROCS | SYSSTAT_MOUNTS);
    sysstat_sample_rates(a, NULL);
    g_assert_cmpuint(a->cpu->len, ==, 3);
    g_assert_cmpuint(a->core_percent->len, ==, 2);
    g_assert_cmpfloat(a->cpu_percent, ==, 0.0);
    g_assert_cmpuint(a->mem.total, ==, 1000 * 1024);
    g_assert_cmpfloat_with_epsilon(a->load[0], 1.0, 1e-9);
    g_assert_cmpuint(a->tasks_total, ==, 100);
    g_assert_cmpfloat_with_epsilon(a->uptime, 3600.5, 1e-9);
    g_assert_cmpuint(a->net->len, ==, 1);
    g_assert_cmpuint(a->disks->len, ==, 1);
    g_assert_cmpuint(a->mounts->len, ==, 1);
    g_assert_cmpuint(a->procs->len, ==, 1);
    SysProc* p = g_ptr_array_index(a->procs, 0);
    g_assert_cmpint(p->pid, ==, 42);
    g_assert_cmpstr(p->name, ==, "worker");
    g_assert_nonnull(p->user);

    write_file(root, "stat", STAT_2);
    write_file(root, "net/dev", "h\nh\neth0: 3000 0 0 0 0 0 0 0 2500 0 0 0 0 0 0 0\n");
    write_file(root, "diskstats", "8 0 sda 0 0 300 0 0 0 600 0 0 600 0\n");
    write_file(root, "42/stat", "42 (worker) R 1 42 42 0 -1 0 0 0 0 0 110 60 0 0 20 0 3 0 1 1 10\n");
    SysSample* b = sysstat_sample(root, SYSSTAT_PROCS);
    b->time_us = a->time_us + 2 * G_USEC_PER_SEC;
    sysstat_sample_rates(b, a);
    g_assert_null(b->mounts);
    g_assert_cmpfloat_with_epsilon(b->cpu_percent, 30.0, 1e-9);
    g_assert_cmpfloat_with_epsilon(g_array_index(b->core_percent, gdouble, 0), 60.0, 1e-9);
    g_assert_cmpfloat_with_epsilon(b->net_rx_rate, 1000.0, 1e-9);
    g_assert_cmpfloat_with_epsilon(b->net_tx_rate, 250.0, 1e-9);
    g_assert_cmpfloat_with_epsilon(b->disk_read_rate, 100.0 * 512, 1e-9);
    g_assert_cmpfloat_with_epsilon(b->disk_write_rate, 200.0 * 512, 1e-9);
    g_assert_cmpfloat_with_epsilon(b->disk_busy, 25.0, 1e-9);
    /* 150 ticks of 500 on 2 CPUs: 60% of one CPU */
    p = g_ptr_array_index(b->procs, 0);
    g_assert_cmpfloat_with_epsilon(p->cpu_percent, 60.0, 1e-9);

    sysstat_sample_free(a);
    sysstat_sample_free(b);
    remove_tree(root);
    g_free(root);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/sysstat/cpu", test_cpu);
    g_test_add_func("/sysstat/meminfo", test_meminfo);
    g_test_add_func("/sysstat/loadavg", test_loadavg);
    g_test_add_func("/sysstat/netdev", test_netdev);
    g_test_add_func("/sysstat/diskstats", test_diskstats);
    g_test_add_func("/sysstat/mounts", test_mounts);
    g_test_add_func("/sysstat/pid_stat", test_pid_stat);
    g_test_add_func("/sysstat/sample", test_sample);
    return g_test_run();
}
