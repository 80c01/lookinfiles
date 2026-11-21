/* Statistics of a remote Linux machine from one shell command (see
 * sysstat_remote.h). */
#include "core/sysstat_remote.h"

#include <string.h>

/* POSIX sh with the Linux /proc; errors (a process gone, a missing file)
 * are dropped. df goes under timeout(1) when there is one: a hung network
 * mount must not hold the reading forever. */
static const gchar* BASE =
    "exec 2>/dev/null; LC_ALL=C; export LC_ALL; "
    "echo @@stat; cat /proc/stat; "
    "echo @@meminfo; cat /proc/meminfo; "
    "echo @@loadavg; cat /proc/loadavg; "
    "echo @@uptime; cat /proc/uptime; "
    "echo @@netdev; cat /proc/net/dev; "
    "echo @@diskstats; cat /proc/diskstats; "
    "echo @@block; for d in /sys/block/*; do [ -n \"$(ls -A \"$d/slaves\")\" ] || echo \"${d##*/}\"; done; "
    "echo @@host; uname -n; "
    "echo @@os; uname -sr; "
    "echo @@pagesize; getconf PAGESIZE; ";
static const gchar* MOUNTS =
    "echo @@mounts; cat /proc/self/mounts; "
    "echo @@df; if command -v timeout >/dev/null; then timeout 5 df -kP; else df -kP; fi; ";
static const gchar* PROCS =
    "echo @@procstat; cat /proc/[0-9]*/stat; "
    "echo @@procuid; stat -c '%u %n' /proc/[0-9]*; "
    "echo @@passwd; cut -d: -f1,3 /etc/passwd; ";

gchar* sysstat_remote_command(SysSampleFlags flags) {
    return g_strconcat(BASE, (flags & SYSSTAT_MOUNTS) ? MOUNTS : "", (flags & SYSSTAT_PROCS) ? PROCS : "",
                       "echo @@end", NULL);
}

/* The sections of the output: name -> text (owned by the table) */
static GHashTable* sections(const gchar* text) {
    GHashTable* out = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    gchar* name = NULL;
    const gchar* start = NULL;
    const gchar* p = text;
    while (p != NULL && *p != '\0') {
        const gchar* nl = strchr(p, '\n');
        const gchar* end = nl != NULL ? nl : p + strlen(p);
        if (end - p > 2 && p[0] == '@' && p[1] == '@') {
            if (name != NULL) g_hash_table_replace(out, name, g_strndup(start, (gsize)(p - start)));
            name = g_strndup(p + 2, (gsize)(end - p - 2));
            g_strstrip(name);
            start = nl != NULL ? nl + 1 : end;
        }
        p = nl != NULL ? nl + 1 : NULL;
    }
    if (name != NULL) g_hash_table_replace(out, name, g_strdup(start));
    return out;
}

/* First line of a section, trimmed (NULL when missing or blank). Caller frees. */
static gchar* first_line(GHashTable* sec, const gchar* name) {
    const gchar* text = g_hash_table_lookup(sec, name);
    if (text == NULL) return NULL;
    const gchar* nl = strchr(text, '\n');
    gchar* line = nl != NULL ? g_strndup(text, (gsize)(nl - text)) : g_strdup(text);
    g_strstrip(line);
    if (line[0] == '\0') g_clear_pointer(&line, g_free);
    return line;
}

static gboolean keep_listed(const gchar* name, gpointer data) {
    return g_hash_table_contains(data, name);
}

/* "df -kP" lines: mount point -> SysMount with the sizes (bytes) */
static GHashTable* parse_df(const gchar* text) {
    GHashTable* out = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    gchar** lines = g_strsplit(text != NULL ? text : "", "\n", -1);
    for (guint i = 1; lines[i] != NULL; i++) { /* [0]: the header */
        const gchar* p = lines[i];
        guint64 v[3] = { 0, 0, 0 };
        gboolean ok = TRUE;
        for (gint field = 0; field < 5 && ok; field++) { /* device, total, used, available, capacity */
            while (*p == ' ' || *p == '\t') p++;
            const gchar* word = p;
            while (*p != '\0' && *p != ' ' && *p != '\t') p++;
            ok = p > word;
            if (ok && field >= 1 && field <= 3) v[field - 1] = g_ascii_strtoull(word, NULL, 10);
        }
        while (*p == ' ' || *p == '\t') p++;
        if (!ok || *p == '\0') continue;
        guint64* sizes = g_new(guint64, 3);
        for (gint k = 0; k < 3; k++) sizes[k] = v[k] * 1024;
        g_hash_table_replace(out, g_strdup(p), sizes); /* the mount point may hold spaces */
    }
    g_strfreev(lines);
    return out;
}

/* uid -> user name from "name:uid" lines */
static GHashTable* parse_passwd(const gchar* text) {
    GHashTable* out = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    gchar** lines = g_strsplit(text != NULL ? text : "", "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        gchar* colon = strchr(lines[i], ':');
        if (colon == NULL) continue;
        *colon = '\0';
        guint uid = (guint)g_ascii_strtoull(colon + 1, NULL, 10);
        if (!g_hash_table_contains(out, GUINT_TO_POINTER(uid))) {
            g_hash_table_insert(out, GUINT_TO_POINTER(uid), g_strdup(lines[i]));
        }
    }
    g_strfreev(lines);
    return out;
}

/* pid -> uid from "uid /proc/pid" lines */
static GHashTable* parse_uids(const gchar* text) {
    GHashTable* out = g_hash_table_new(g_direct_hash, g_direct_equal);
    gchar** lines = g_strsplit(text != NULL ? text : "", "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        gchar* end = NULL;
        guint64 uid = g_ascii_strtoull(lines[i], &end, 10);
        const gchar* slash = end != NULL ? strrchr(end, '/') : NULL;
        if (end == lines[i] || slash == NULL) continue;
        gint pid = (gint)g_ascii_strtoll(slash + 1, NULL, 10);
        if (pid > 0) g_hash_table_insert(out, GINT_TO_POINTER(pid), GUINT_TO_POINTER((guint)uid));
    }
    g_strfreev(lines);
    return out;
}

static GPtrArray* parse_procs(GHashTable* sec) {
    GPtrArray* out = g_ptr_array_new_with_free_func(sysstat_proc_free);
    GHashTable* uids = parse_uids(g_hash_table_lookup(sec, "procuid"));
    GHashTable* users = parse_passwd(g_hash_table_lookup(sec, "passwd"));
    const gchar* text = g_hash_table_lookup(sec, "procstat");
    gchar** lines = g_strsplit(text != NULL ? text : "", "\n", -1);
    for (guint i = 0; lines[i] != NULL; i++) {
        if (lines[i][0] == '\0') continue;
        SysProc* p = g_new0(SysProc, 1);
        if (!sysstat_parse_pid_stat(lines[i], p)) {
            sysstat_proc_free(p);
            continue;
        }
        gpointer uid = NULL;
        if (g_hash_table_lookup_extended(uids, GINT_TO_POINTER(p->pid), NULL, &uid)) p->uid = GPOINTER_TO_UINT(uid);
        const gchar* name = g_hash_table_lookup(users, GUINT_TO_POINTER(p->uid));
        p->user = name != NULL ? g_strdup(name) : g_strdup_printf("%u", p->uid);
        g_ptr_array_add(out, p);
    }
    g_strfreev(lines);
    g_hash_table_unref(users);
    g_hash_table_unref(uids);
    return out;
}

SysSample* sysstat_remote_parse(const gchar* text, SysSampleFlags flags, gchar** host, gchar** os,
                                guint64* page_size) {
    if (host != NULL) *host = NULL;
    if (os != NULL) *os = NULL;
    if (page_size != NULL) *page_size = 0;
    GHashTable* sec = sections(text != NULL ? text : "");
    SysSample* s = g_new0(SysSample, 1);
    s->cpu = g_array_new(FALSE, TRUE, sizeof(SysCpuTimes));
    s->core_percent = g_array_new(FALSE, TRUE, sizeof(gdouble));
    if (!sysstat_parse_cpu(g_hash_table_lookup(sec, "stat"), s->cpu)) {
        sysstat_sample_free(s);
        g_hash_table_unref(sec);
        return NULL;
    }
    sysstat_parse_meminfo(g_hash_table_lookup(sec, "meminfo"), &s->mem);
    sysstat_parse_loadavg(g_hash_table_lookup(sec, "loadavg"), s->load, &s->tasks_running, &s->tasks_total);
    const gchar* up = g_hash_table_lookup(sec, "uptime");
    s->uptime = up != NULL ? g_ascii_strtod(up, NULL) : 0;
    s->time_us = (gint64)(s->uptime * G_USEC_PER_SEC); /* the machine's clock */
    s->net = sysstat_parse_netdev(g_hash_table_lookup(sec, "netdev"));

    /* Whole disks: those of /sys/block without slaves (as sysstat_sample()) */
    const gchar* block = g_hash_table_lookup(sec, "block");
    GHashTable* disks = NULL;
    if (block != NULL) {
        disks = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        gchar** names = g_strsplit(block, "\n", -1);
        for (guint i = 0; names[i] != NULL; i++) {
            g_strstrip(names[i]);
            if (names[i][0] != '\0' && !g_str_has_prefix(names[i], "loop") && !g_str_has_prefix(names[i], "ram")) {
                g_hash_table_add(disks, g_strdup(names[i]));
            }
        }
        g_strfreev(names);
    }
    s->disks = sysstat_parse_diskstats(g_hash_table_lookup(sec, "diskstats"), disks != NULL ? keep_listed : NULL, disks);
    if (disks != NULL) g_hash_table_unref(disks);

    if (flags & SYSSTAT_MOUNTS) {
        s->mounts = sysstat_parse_mounts(g_hash_table_lookup(sec, "mounts"));
        GHashTable* df = parse_df(g_hash_table_lookup(sec, "df"));
        for (guint i = 0; i < s->mounts->len;) {
            SysMount* m = g_ptr_array_index(s->mounts, i);
            const guint64* sizes = g_hash_table_lookup(df, m->mount_point);
            if (sizes != NULL && sizes[0] > 0) {
                m->total = sizes[0];
                m->used = sizes[1];
                m->avail = sizes[2];
                i++;
            } else {
                g_ptr_array_remove_index(s->mounts, i); /* as statvfs() failing here */
            }
        }
        g_hash_table_unref(df);
    }
    if (flags & SYSSTAT_PROCS) s->procs = parse_procs(sec);

    if (host != NULL) *host = first_line(sec, "host");
    if (os != NULL) *os = first_line(sec, "os");
    if (page_size != NULL) {
        gchar* ps = first_line(sec, "pagesize");
        *page_size = ps != NULL ? g_ascii_strtoull(ps, NULL, 10) : 0;
        g_free(ps);
    }
    g_hash_table_unref(sec);
    return s;
}
