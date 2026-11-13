#ifndef SYSSTAT_H
#define SYSSTAT_H

/* System statistics of Linux, read from /proc (and /sys/block): CPU times,
 * memory, load average, network and block devices, mounted filesystems and
 * processes. The parsers take the text of a /proc file and are pure; the
 * readers take the /proc root (normally "/proc", a fixture in the tests).
 * sysstat_sample() reads a whole snapshot (in a worker thread: statvfs() on
 * a network mount can block) and sysstat_sample_rates() turns two snapshots
 * into percentages and rates. Linux only: elsewhere the readers find no file
 * and return empty results (sysstat_available() is FALSE). */
#include <glib.h>

/* Cumulated CPU times of /proc/stat, in clock ticks */
typedef struct {
    guint64 user, nice, system, idle, iowait, irq, softirq, steal;
} SysCpuTimes;

/* /proc/meminfo, in bytes */
typedef struct {
    guint64 total;
    guint64 available; /* MemAvailable (estimated from free + cache on old kernels) */
    guint64 free;
    guint64 buffers;
    guint64 cached;    /* Cached + SReclaimable */
    guint64 swap_total;
    guint64 swap_free;
} SysMemInfo;

/* A network interface of /proc/net/dev (cumulated byte counts) */
typedef struct {
    gchar* name;
    guint64 rx_bytes;
    guint64 tx_bytes;
} SysNetDev;

/* A block device of /proc/diskstats (cumulated) */
typedef struct {
    gchar* name;
    guint64 read_sectors;  /* 512-byte sectors */
    guint64 write_sectors;
    guint64 io_ms;         /* time spent doing I/O */
} SysDiskDev;

/* A mounted filesystem of /proc/self/mounts, sizes in bytes */
typedef struct {
    gchar* device;
    gchar* mount_point;
    gchar* fs_type;
    guint64 total;
    guint64 used;
    guint64 avail; /* for unprivileged users */
} SysMount;

/* A process of /proc/<pid>/stat */
typedef struct {
    gint pid;
    gchar* name;        /* comm: at most 15 characters */
    gchar state;        /* R, S, D, Z, T, I... */
    guint64 cpu_ticks;  /* utime + stime */
    guint64 rss_pages;
    guint threads;
    guint uid;
    gchar* user;        /* name of uid, filled by sysstat_read_procs() */
    gdouble cpu_percent; /* filled by sysstat_sample_rates(): 100 = one core */
} SysProc;

/* What sysstat_sample() reads besides CPU, memory, load, network and disks */
typedef enum {
    SYSSTAT_PROCS = 1 << 0,
    SYSSTAT_MOUNTS = 1 << 1,
} SysSampleFlags;

typedef struct {
    gint64 time_us;      /* monotonic time of the reading */
    GArray* cpu;         /* SysCpuTimes: [0] all CPUs, then each CPU */
    SysMemInfo mem;
    gdouble load[3];     /* 1, 5 and 15 minutes */
    guint tasks_running;
    guint tasks_total;
    gdouble uptime;      /* seconds */
    GPtrArray* net;      /* SysNetDev*, without the loopback */
    GPtrArray* disks;    /* SysDiskDev*, whole disks only */
    GPtrArray* mounts;   /* SysMount*, or NULL without SYSSTAT_MOUNTS */
    GPtrArray* procs;    /* SysProc*, or NULL without SYSSTAT_PROCS */

    /* Filled by sysstat_sample_rates() (0 for the first sample) */
    gdouble cpu_percent;     /* all CPUs, 0..100 */
    GArray* core_percent;    /* gdouble per CPU, 0..100 */
    gdouble net_rx_rate;     /* bytes per second, all interfaces */
    gdouble net_tx_rate;
    gdouble disk_read_rate;  /* bytes per second, all disks */
    gdouble disk_write_rate;
    gdouble disk_busy;       /* busiest disk, 0..100 */
} SysSample;

/* /proc/stat: the "cpu" line then each "cpuN" line appended to out
 * (SysCpuTimes). FALSE if there is no "cpu" line. */
gboolean sysstat_parse_cpu(const gchar* text, GArray* out);
/* Busy share of the CPU time between two readings, 0..100 (0 if none passed) */
gdouble sysstat_cpu_percent(const SysCpuTimes* prev, const SysCpuTimes* cur);
/* Ticks between two readings, all fields */
guint64 sysstat_cpu_elapsed(const SysCpuTimes* prev, const SysCpuTimes* cur);

/* /proc/meminfo. FALSE without MemTotal. */
gboolean sysstat_parse_meminfo(const gchar* text, SysMemInfo* out);
/* Memory in use: total - available */
guint64 sysstat_mem_used(const SysMemInfo* mem);

/* /proc/loadavg: the three averages and the running / total tasks */
gboolean sysstat_parse_loadavg(const gchar* text, gdouble load[3], guint* running, guint* total);

/* /proc/net/dev: SysNetDev*, the loopback left out */
GPtrArray* sysstat_parse_netdev(const gchar* text);

/* /proc/diskstats: SysDiskDev* of every device for which keep(name, data)
 * is TRUE (NULL: every device but loop and ram devices) */
GPtrArray* sysstat_parse_diskstats(const gchar* text, gboolean (*keep)(const gchar* name, gpointer data),
                                   gpointer data);

/* /proc/self/mounts: SysMount* of the filesystems on a device (the device
 * starts with '/'), each device and mount point once, without the read-only
 * images of snaps (squashfs); sizes left 0 (sysstat_mount_fill()) */
GPtrArray* sysstat_parse_mounts(const gchar* text);
/* Sizes of a mount from statvfs(); FALSE if it failed */
gboolean sysstat_mount_fill(SysMount* mount);

/* /proc/<pid>/stat into out (name owned by out; user, uid and cpu_percent
 * left alone). The name may hold spaces and parentheses. */
gboolean sysstat_parse_pid_stat(const gchar* text, SysProc* out);
/* Every process of proc_root (SysProc*), with its owner */
GPtrArray* sysstat_read_procs(const gchar* proc_root);

void sysstat_net_dev_free(gpointer dev);
void sysstat_disk_dev_free(gpointer dev);
void sysstat_mount_free(gpointer mount);
void sysstat_proc_free(gpointer proc);

/* TRUE when proc_root has the files of a Linux /proc */
gboolean sysstat_available(const gchar* proc_root);
/* A snapshot of proc_root (NULL: "/proc"); the disks are the whole disks of
 * /sys/block when it exists */
SysSample* sysstat_sample(const gchar* proc_root, SysSampleFlags flags);
/* Fill the rates of cur from prev, read earlier (prev may be NULL) */
void sysstat_sample_rates(SysSample* cur, const SysSample* prev);
void sysstat_sample_free(SysSample* sample);

/* Size of a memory page, for rss_pages */
guint64 sysstat_page_size(void);

#endif /* SYSSTAT_H */
