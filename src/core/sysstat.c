/* System statistics of Linux from /proc (see sysstat.h) */
#include "core/sysstat.h"

#include <string.h>
#include <glib/gstdio.h>
#ifdef G_OS_UNIX
#include <pwd.h>
#include <sys/statvfs.h>
#include <unistd.h>
#endif

#define SECTOR_BYTES 512

/* The unsigned number at *p, which moves past it and the blanks before it */
static guint64 next_u64(const gchar** p) {
    const gchar* s = *p;
    while (*s == ' ' || *s == '\t') s++;
    gchar* end = NULL;
    guint64 v = g_ascii_strtoull(s, &end, 10);
    if (end == s) end = (gchar*)s;
    *p = end;
    return v;
}

static gboolean blank(gchar c) {
    return c == ' ' || c == '\t';
}

gboolean sysstat_parse_cpu(const gchar* text, GArray* out) {
    if (text == NULL || out == NULL) return FALSE;
    gboolean found = FALSE;
    gchar** lines = g_strsplit(text, "\n", -1);
    for (gint i = 0; lines[i] != NULL; i++) {
        const gchar* l = lines[i];
        if (!g_str_has_prefix(l, "cpu")) continue;
        const gchar* p = l + 3;
        gboolean total = blank(*p);
        if (!total && !g_ascii_isdigit(*p)) continue;
        if (total != !found) continue; /* the "cpu" line comes first, once */
        while (g_ascii_isdigit(*p)) p++;
        SysCpuTimes t;
        t.user = next_u64(&p);
        t.nice = next_u64(&p);
        t.system = next_u64(&p);
        t.idle = next_u64(&p);
        t.iowait = next_u64(&p);
        t.irq = next_u64(&p);
        t.softirq = next_u64(&p);
        t.steal = next_u64(&p);
        g_array_append_val(out, t);
        found = TRUE;
    }
    g_strfreev(lines);
    return found;
}

static guint64 cpu_total(const SysCpuTimes* t) {
    return t->user + t->nice + t->system + t->idle + t->iowait + t->irq + t->softirq + t->steal;
}

static guint64 cpu_idle(const SysCpuTimes* t) {
    return t->idle + t->iowait;
}

guint64 sysstat_cpu_elapsed(const SysCpuTimes* prev, const SysCpuTimes* cur) {
    if (prev == NULL || cur == NULL) return 0;
    guint64 a = cpu_total(prev), b = cpu_total(cur);
    return b > a ? b - a : 0;
}

gdouble sysstat_cpu_percent(const SysCpuTimes* prev, const SysCpuTimes* cur) {
    guint64 elapsed = sysstat_cpu_elapsed(prev, cur);
    if (elapsed == 0) return 0.0;
    guint64 i0 = cpu_idle(prev), i1 = cpu_idle(cur);
    guint64 idle = i1 > i0 ? i1 - i0 : 0;
    if (idle > elapsed) idle = elapsed;
    return 100.0 * (gdouble)(elapsed - idle) / (gdouble)elapsed;
}

gboolean sysstat_parse_meminfo(const gchar* text, SysMemInfo* out) {
    if (text == NULL || out == NULL) return FALSE;
    memset(out, 0, sizeof(*out));
    gboolean have_total = FALSE, have_avail = FALSE;
    guint64 sreclaimable = 0;
    gchar** lines = g_strsplit(text, "\n", -1);
    for (gint i = 0; lines[i] != NULL; i++) {
        const gchar* colon = strchr(lines[i], ':');
        if (colon == NULL) continue;
        gsize len = (gsize)(colon - lines[i]);
        const gchar* p = colon + 1;
        guint64 v = next_u64(&p) * 1024; /* kB */
#define KEY(k) (len == strlen(k) && strncmp(lines[i], k, len) == 0)
        if (KEY("MemTotal")) { out->total = v; have_total = TRUE; }
        else if (KEY("MemFree")) out->free = v;
        else if (KEY("MemAvailable")) { out->available = v; have_avail = TRUE; }
        else if (KEY("Buffers")) out->buffers = v;
        else if (KEY("Cached")) out->cached = v;
        else if (KEY("SReclaimable")) sreclaimable = v;
        else if (KEY("SwapTotal")) out->swap_total = v;
        else if (KEY("SwapFree")) out->swap_free = v;
#undef KEY
    }
    g_strfreev(lines);
    out->cached += sreclaimable;
    if (!have_avail) out->available = MIN(out->total, out->free + out->buffers + out->cached);
    return have_total;
}

guint64 sysstat_mem_used(const SysMemInfo* mem) {
    if (mem == NULL || mem->available > mem->total) return 0;
    return mem->total - mem->available;
}

gboolean sysstat_parse_loadavg(const gchar* text, gdouble load[3], guint* running, guint* total) {
    if (text == NULL) return FALSE;
    const gchar* p = text;
    for (gint i = 0; i < 3; i++) {
        gchar* end = NULL;
        gdouble v = g_ascii_strtod(p, &end);
        if (end == p) return FALSE;
        if (load != NULL) load[i] = v;
        p = end;
    }
    const gchar* q = p;
    guint r = (guint)next_u64(&q);
    guint t = 0;
    if (*q == '/') {
        q++;
        t = (guint)next_u64(&q);
    }
    if (running != NULL) *running = r;
    if (total != NULL) *total = t;
    return TRUE;
}

void sysstat_net_dev_free(gpointer dev) {
    SysNetDev* d = dev;
    if (d == NULL) return;
    g_free(d->name);
    g_free(d);
}

GPtrArray* sysstat_parse_netdev(const gchar* text) {
    GPtrArray* out = g_ptr_array_new_with_free_func(sysstat_net_dev_free);
    if (text == NULL) return out;
    gchar** lines = g_strsplit(text, "\n", -1);
    for (gint i = 0; lines[i] != NULL; i++) {
        const gchar* colon = strchr(lines[i], ':');
        if (colon == NULL) continue; /* the two header lines */
        gchar* name = g_strstrip(g_strndup(lines[i], (gsize)(colon - lines[i])));
        if (name[0] == '\0' || strchr(name, '|') != NULL || strcmp(name, "lo") == 0) {
            g_free(name);
            continue;
        }
        const gchar* p = colon + 1;
        SysNetDev* d = g_new0(SysNetDev, 1);
        d->name = name;
        d->rx_bytes = next_u64(&p);
        for (gint f = 0; f < 7; f++) next_u64(&p); /* packets errs drop fifo frame compressed multicast */
        d->tx_bytes = next_u64(&p);
        g_ptr_array_add(out, d);
    }
    g_strfreev(lines);
    return out;
}

void sysstat_disk_dev_free(gpointer dev) {
    SysDiskDev* d = dev;
    if (d == NULL) return;
    g_free(d->name);
    g_free(d);
}

static gboolean keep_default(const gchar* name, gpointer data) {
    (void)data;
    return !g_str_has_prefix(name, "loop") && !g_str_has_prefix(name, "ram");
}

GPtrArray* sysstat_parse_diskstats(const gchar* text, gboolean (*keep)(const gchar* name, gpointer data),
                                   gpointer data) {
    GPtrArray* out = g_ptr_array_new_with_free_func(sysstat_disk_dev_free);
    if (text == NULL) return out;
    if (keep == NULL) keep = keep_default;
    gchar** lines = g_strsplit(text, "\n", -1);
    for (gint i = 0; lines[i] != NULL; i++) {
        const gchar* p = lines[i];
        next_u64(&p); /* major */
        next_u64(&p); /* minor */
        while (blank(*p)) p++;
        const gchar* name_end = p;
        while (*name_end != '\0' && !blank(*name_end)) name_end++;
        if (name_end == p) continue;
        gchar* name = g_strndup(p, (gsize)(name_end - p));
        if (!keep(name, data)) {
            g_free(name);
            continue;
        }
        p = name_end;
        SysDiskDev* d = g_new0(SysDiskDev, 1);
        d->name = name;
        next_u64(&p);                    /* reads completed */
        next_u64(&p);                    /* reads merged */
        d->read_sectors = next_u64(&p);
        next_u64(&p);                    /* ms reading */
        next_u64(&p);                    /* writes completed */
        next_u64(&p);                    /* writes merged */
        d->write_sectors = next_u64(&p);
        next_u64(&p);                    /* ms writing */
        next_u64(&p);                    /* I/Os in progress */
        d->io_ms = next_u64(&p);
        g_ptr_array_add(out, d);
    }
    g_strfreev(lines);
    return out;
}

void sysstat_mount_free(gpointer mount) {
    SysMount* m = mount;
    if (m == NULL) return;
    g_free(m->device);
    g_free(m->mount_point);
    g_free(m->fs_type);
    g_free(m);
}

/* A field of /proc/self/mounts: octal escapes (\040 for a space) decoded */
static gchar* unescape_field(const gchar* s) {
    GString* out = g_string_new(NULL);
    for (const gchar* p = s; *p != '\0'; p++) {
        if (p[0] == '\\' && p[1] >= '0' && p[1] <= '3' && p[2] >= '0' && p[2] <= '7' && p[3] >= '0' &&
            p[3] <= '7') {
            g_string_append_c(out, (gchar)((p[1] - '0') * 64 + (p[2] - '0') * 8 + (p[3] - '0')));
            p += 3;
        } else {
            g_string_append_c(out, *p);
        }
    }
    return g_string_free(out, FALSE);
}

GPtrArray* sysstat_parse_mounts(const gchar* text) {
    GPtrArray* out = g_ptr_array_new_with_free_func(sysstat_mount_free);
    if (text == NULL) return out;
    GHashTable* seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gchar** lines = g_strsplit(text, "\n", -1);
    for (gint i = 0; lines[i] != NULL; i++) {
        gchar** f = g_strsplit_set(lines[i], " \t", 4);
        if (g_strv_length(f) >= 3 && f[0][0] == '/' && strcmp(f[2], "squashfs") != 0) {
            gchar* dev = unescape_field(f[0]);
            gchar* dir = unescape_field(f[1]);
            gchar* dev_key = g_strconcat("d:", dev, NULL);
            gchar* dir_key = g_strconcat("m:", dir, NULL);
            if (g_hash_table_contains(seen, dev_key) || g_hash_table_contains(seen, dir_key)) {
                g_free(dev);
                g_free(dir);
                g_free(dev_key);
                g_free(dir_key);
            } else {
                g_hash_table_add(seen, dev_key);
                g_hash_table_add(seen, dir_key);
                SysMount* m = g_new0(SysMount, 1);
                m->device = dev;
                m->mount_point = dir;
                m->fs_type = g_strdup(f[2]);
                g_ptr_array_add(out, m);
            }
        }
        g_strfreev(f);
    }
    g_strfreev(lines);
    g_hash_table_unref(seen);
    return out;
}

gboolean sysstat_mount_fill(SysMount* mount) {
    if (mount == NULL || mount->mount_point == NULL) return FALSE;
#ifndef G_OS_UNIX
    return FALSE;
#else
    struct statvfs st;
    if (statvfs(mount->mount_point, &st) != 0) return FALSE;
    guint64 unit = st.f_frsize != 0 ? st.f_frsize : st.f_bsize;
    mount->total = (guint64)st.f_blocks * unit;
    mount->avail = (guint64)st.f_bavail * unit;
    guint64 free_bytes = (guint64)st.f_bfree * unit;
    mount->used = mount->total > free_bytes ? mount->total - free_bytes : 0;
    return TRUE;
#endif
}

void sysstat_proc_free(gpointer proc) {
    SysProc* p = proc;
    if (p == NULL) return;
    g_free(p->name);
    g_free(p->user);
    g_free(p);
}

gboolean sysstat_parse_pid_stat(const gchar* text, SysProc* out) {
    if (text == NULL || out == NULL) return FALSE;
    const gchar* open = strchr(text, '(');
    const gchar* close = strrchr(text, ')');
    if (open == NULL || close == NULL || close < open) return FALSE;
    const gchar* p = text;
    gint pid = (gint)next_u64(&p);
    if (pid <= 0) return FALSE;
    gchar* raw = g_strndup(open + 1, (gsize)(close - open - 1));
    gchar* name = g_utf8_make_valid(raw, -1);
    g_free(raw);
    p = close + 1;
    while (blank(*p)) p++;
    gchar state = *p != '\0' ? *p : '?';
    if (*p != '\0') p++;
    /* Fields after the state, numbered as in proc(5): ppid is 4 */
    guint64 utime = 0, stime = 0, threads = 0, rss = 0;
    for (gint field = 4; field <= 24; field++) {
        while (blank(*p)) p++;
        if (*p == '\0') break;
        gchar* end = NULL;
        gint64 v = g_ascii_strtoll(p, &end, 10);
        if (end == p) break;
        p = end;
        guint64 u = v > 0 ? (guint64)v : 0;
        if (field == 14) utime = u;
        else if (field == 15) stime = u;
        else if (field == 20) threads = u;
        else if (field == 24) rss = u;
    }
    g_free(out->name);
    out->pid = pid;
    out->name = name;
    out->state = state;
    out->cpu_ticks = utime + stime;
    out->threads = (guint)threads;
    out->rss_pages = rss;
    return TRUE;
}

/* User names by uid, shared by the worker threads */
G_LOCK_DEFINE_STATIC(user_names);
static GHashTable* user_names;

static gchar* user_name(guint uid) {
    G_LOCK(user_names);
    if (user_names == NULL) user_names = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    const gchar* known = g_hash_table_lookup(user_names, GUINT_TO_POINTER(uid + 1));
    gchar* name = NULL;
    if (known != NULL) {
        name = g_strdup(known);
    } else {
#ifdef G_OS_UNIX
        struct passwd pw, *res = NULL;
        gchar buf[1024];
        if (getpwuid_r((uid_t)uid, &pw, buf, sizeof buf, &res) == 0 && res != NULL && res->pw_name != NULL) {
            name = g_utf8_make_valid(res->pw_name, -1);
        }
#endif
        if (name == NULL) name = g_strdup_printf("%u", uid);
        g_hash_table_insert(user_names, GUINT_TO_POINTER(uid + 1), g_strdup(name));
    }
    G_UNLOCK(user_names);
    return name;
}

static gboolean all_digits(const gchar* s) {
    if (s == NULL || *s == '\0') return FALSE;
    for (; *s != '\0'; s++) {
        if (!g_ascii_isdigit(*s)) return FALSE;
    }
    return TRUE;
}

GPtrArray* sysstat_read_procs(const gchar* proc_root) {
    GPtrArray* out = g_ptr_array_new_with_free_func(sysstat_proc_free);
    GDir* dir = g_dir_open(proc_root != NULL ? proc_root : "/proc", 0, NULL);
    if (dir == NULL) return out;
    const gchar* entry;
    while ((entry = g_dir_read_name(dir)) != NULL) {
        if (!all_digits(entry)) continue;
        gchar* pid_dir = g_build_filename(proc_root != NULL ? proc_root : "/proc", entry, NULL);
        gchar* stat_path = g_build_filename(pid_dir, "stat", NULL);
        gchar* text = NULL;
        if (g_file_get_contents(stat_path, &text, NULL, NULL)) {
            SysProc* p = g_new0(SysProc, 1);
            if (sysstat_parse_pid_stat(text, p)) {
                GStatBuf st;
                if (g_stat(pid_dir, &st) == 0) p->uid = (guint)st.st_uid;
                p->user = user_name(p->uid);
                g_ptr_array_add(out, p);
            } else {
                sysstat_proc_free(p);
            }
            g_free(text);
        }
        g_free(stat_path);
        g_free(pid_dir);
    }
    g_dir_close(dir);
    return out;
}

static gchar* read_file(const gchar* root, const gchar* name) {
    gchar* path = g_build_filename(root, name, NULL);
    gchar* text = NULL;
    if (!g_file_get_contents(path, &text, NULL, NULL)) text = NULL;
    g_free(path);
    return text;
}

gboolean sysstat_available(const gchar* proc_root) {
    gchar* path = g_build_filename(proc_root != NULL ? proc_root : "/proc", "stat", NULL);
    gboolean ok = g_file_test(path, G_FILE_TEST_IS_REGULAR);
    g_free(path);
    return ok;
}

/* A whole physical disk of /sys/block: not a loop or ram device, and not
 * built on other devices (device mapper, RAID), whose I/O is already
 * counted on the disks under it */
static gboolean keep_sys_block(const gchar* name, gpointer data) {
    (void)data;
    if (!keep_default(name, NULL)) return FALSE;
    gchar* dir = g_build_filename("/sys/block", name, NULL);
    gboolean keep = g_file_test(dir, G_FILE_TEST_IS_DIR);
    if (keep) {
        gchar* slaves = g_build_filename(dir, "slaves", NULL);
        GDir* d = g_dir_open(slaves, 0, NULL);
        if (d != NULL) {
            if (g_dir_read_name(d) != NULL) keep = FALSE;
            g_dir_close(d);
        }
        g_free(slaves);
    }
    g_free(dir);
    return keep;
}

SysSample* sysstat_sample(const gchar* proc_root, SysSampleFlags flags) {
    gboolean real = proc_root == NULL || strcmp(proc_root, "/proc") == 0;
    const gchar* root = proc_root != NULL ? proc_root : "/proc";
    SysSample* s = g_new0(SysSample, 1);
    s->time_us = g_get_monotonic_time();
    s->cpu = g_array_new(FALSE, TRUE, sizeof(SysCpuTimes));
    s->core_percent = g_array_new(FALSE, TRUE, sizeof(gdouble));

    gchar* text = read_file(root, "stat");
    sysstat_parse_cpu(text, s->cpu);
    g_free(text);
    text = read_file(root, "meminfo");
    sysstat_parse_meminfo(text, &s->mem);
    g_free(text);
    text = read_file(root, "loadavg");
    sysstat_parse_loadavg(text, s->load, &s->tasks_running, &s->tasks_total);
    g_free(text);
    text = read_file(root, "uptime");
    if (text != NULL) s->uptime = g_ascii_strtod(text, NULL);
    g_free(text);
    text = read_file(root, "net/dev");
    s->net = sysstat_parse_netdev(text);
    g_free(text);
    text = read_file(root, "diskstats");
    gboolean sys_block = real && g_file_test("/sys/block", G_FILE_TEST_IS_DIR);
    s->disks = sysstat_parse_diskstats(text, sys_block ? keep_sys_block : NULL, NULL);
    g_free(text);

    if (flags & SYSSTAT_MOUNTS) {
        text = read_file(root, "self/mounts");
        s->mounts = sysstat_parse_mounts(text);
        g_free(text);
        for (guint i = 0; i < s->mounts->len;) {
            SysMount* m = g_ptr_array_index(s->mounts, i);
            if (real ? sysstat_mount_fill(m) && m->total > 0 : TRUE) {
                i++;
            } else {
                g_ptr_array_remove_index(s->mounts, i);
            }
        }
    }
    if (flags & SYSSTAT_PROCS) s->procs = sysstat_read_procs(root);
    return s;
}

static gdouble rate(guint64 prev, guint64 cur, gdouble seconds) {
    return cur > prev && seconds > 0 ? (gdouble)(cur - prev) / seconds : 0.0;
}

void sysstat_sample_rates(SysSample* cur, const SysSample* prev) {
    if (cur == NULL) return;
    g_array_set_size(cur->core_percent, cur->cpu->len > 0 ? cur->cpu->len - 1 : 0);
    if (prev == NULL || prev->cpu->len == 0 || cur->cpu->len == 0) return;
    gdouble seconds = (gdouble)(cur->time_us - prev->time_us) / G_USEC_PER_SEC;

    const SysCpuTimes* all0 = &g_array_index(prev->cpu, SysCpuTimes, 0);
    const SysCpuTimes* all1 = &g_array_index(cur->cpu, SysCpuTimes, 0);
    cur->cpu_percent = sysstat_cpu_percent(all0, all1);
    for (guint i = 1; i < cur->cpu->len && i < prev->cpu->len; i++) {
        g_array_index(cur->core_percent, gdouble, i - 1) =
            sysstat_cpu_percent(&g_array_index(prev->cpu, SysCpuTimes, i), &g_array_index(cur->cpu, SysCpuTimes, i));
    }

    cur->net_rx_rate = cur->net_tx_rate = 0;
    for (guint i = 0; i < cur->net->len; i++) {
        SysNetDev* d = g_ptr_array_index(cur->net, i);
        for (guint j = 0; j < prev->net->len; j++) {
            SysNetDev* o = g_ptr_array_index(prev->net, j);
            if (strcmp(o->name, d->name) != 0) continue;
            cur->net_rx_rate += rate(o->rx_bytes, d->rx_bytes, seconds);
            cur->net_tx_rate += rate(o->tx_bytes, d->tx_bytes, seconds);
            break;
        }
    }

    cur->disk_read_rate = cur->disk_write_rate = cur->disk_busy = 0;
    for (guint i = 0; i < cur->disks->len; i++) {
        SysDiskDev* d = g_ptr_array_index(cur->disks, i);
        for (guint j = 0; j < prev->disks->len; j++) {
            SysDiskDev* o = g_ptr_array_index(prev->disks, j);
            if (strcmp(o->name, d->name) != 0) continue;
            cur->disk_read_rate += rate(o->read_sectors, d->read_sectors, seconds) * SECTOR_BYTES;
            cur->disk_write_rate += rate(o->write_sectors, d->write_sectors, seconds) * SECTOR_BYTES;
            gdouble busy = rate(o->io_ms, d->io_ms, seconds) / 10.0; /* ms per s -> % */
            cur->disk_busy = MAX(cur->disk_busy, MIN(busy, 100.0));
            break;
        }
    }

    /* Processes: their share of one CPU over the time all CPUs ran */
    if (cur->procs != NULL && prev->procs != NULL) {
        guint ncpu = cur->cpu->len > 1 ? cur->cpu->len - 1 : 1;
        guint64 elapsed = sysstat_cpu_elapsed(all0, all1);
        GHashTable* before = g_hash_table_new(g_direct_hash, g_direct_equal);
        for (guint i = 0; i < prev->procs->len; i++) {
            SysProc* p = g_ptr_array_index(prev->procs, i);
            g_hash_table_insert(before, GINT_TO_POINTER(p->pid), p);
        }
        for (guint i = 0; i < cur->procs->len; i++) {
            SysProc* p = g_ptr_array_index(cur->procs, i);
            SysProc* o = g_hash_table_lookup(before, GINT_TO_POINTER(p->pid));
            p->cpu_percent = 0;
            if (o == NULL || elapsed == 0 || p->cpu_ticks < o->cpu_ticks) continue;
            p->cpu_percent = 100.0 * (gdouble)(p->cpu_ticks - o->cpu_ticks) * ncpu / (gdouble)elapsed;
        }
        g_hash_table_unref(before);
    }
}

void sysstat_sample_free(SysSample* sample) {
    if (sample == NULL) return;
    g_array_unref(sample->cpu);
    g_array_unref(sample->core_percent);
    g_clear_pointer(&sample->net, g_ptr_array_unref);
    g_clear_pointer(&sample->disks, g_ptr_array_unref);
    g_clear_pointer(&sample->mounts, g_ptr_array_unref);
    g_clear_pointer(&sample->procs, g_ptr_array_unref);
    g_free(sample);
}

guint64 sysstat_page_size(void) {
#ifdef G_OS_UNIX
    long size = sysconf(_SC_PAGESIZE);
    if (size > 0) return (guint64)size;
#endif
    return 4096;
}
