#ifndef SYSSTAT_HOST_H
#define SYSSTAT_HOST_H

/* System statistics of the machine the application runs on, in the types of
 * sysstat.h. Linux: /proc (sysstat_sample()). Windows: the Win32 API
 * (GetSystemTimes and NtQuerySystemInformation for the CPUs,
 * GlobalMemoryStatusEx, GetIfTable2, IOCTL_DISK_PERFORMANCE, the drives,
 * Toolhelp for the processes); CPU times are then in 100 ns units, there is
 * no load average and no process state. Elsewhere nothing is available. */
#include "core/sysstat.h"

/* TRUE when this machine's statistics can be read */
gboolean sysstat_host_available(void);
/* TRUE when the load average is known (Linux) */
gboolean sysstat_host_has_load(void);
/* TRUE when the processes have a state (running, sleeping...: Linux) */
gboolean sysstat_host_has_proc_state(void);

/* A snapshot of this machine (sysstat_sample_rates() / _free() apply);
 * may block (network drives): call it in a worker thread */
SysSample* sysstat_host_sample(SysSampleFlags flags);

/* Quick readings for the main loop: the times of all CPUs and their number,
 * the memory, the load average (FALSE where unknown) */
gboolean sysstat_host_cpu(SysCpuTimes* all, guint* ncpu);
gboolean sysstat_host_mem(SysMemInfo* mem);
gboolean sysstat_host_load(gdouble load[3]);

/* The system and its version, "Linux 6.8.0-45-generic" or "Windows 11
 * (build 22631)" (freed by the caller) */
gchar* sysstat_host_os(void);

#endif /* SYSSTAT_HOST_H */
