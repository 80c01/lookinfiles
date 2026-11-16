/* System statistics of this machine: /proc on Linux, the Win32 API on Windows (see sysstat_host.h) */
#ifdef _WIN32
/* GetIfTable2() and the Windows 10 API */
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "core/sysstat_host.h"

#include <string.h>

#ifdef G_OS_WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <psapi.h>
#include <sddl.h>
#include <tlhelp32.h>
#include <wchar.h>
#include <winioctl.h>
#endif

#ifdef G_OS_WIN32

/* ------------------------------------------------------------------------- */
/* Windows                                                                   */
/* ------------------------------------------------------------------------- */

#define MAX_PHYSICAL_DRIVES 32
#define SECTOR_BYTES 512
#define TICKS_PER_MS 10000 /* 100 ns units */

static guint64 filetime_u64(FILETIME ft) {
    return ((guint64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static gchar* wide_to_utf8(const wchar_t* s) {
    gchar* out = s != NULL ? g_utf16_to_utf8((const gunichar2*)s, -1, NULL, NULL, NULL) : NULL;
    return out != NULL ? out : g_strdup("");
}

/* A function of ntdll.dll (not in the import libraries of every toolchain) */
static void (*ntdll_function(const gchar* name))(void) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll != NULL ? (void (*)(void))GetProcAddress(ntdll, name) : NULL;
}

/* SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION of NtQuerySystemInformation() */
typedef struct {
    LARGE_INTEGER idle;
    LARGE_INTEGER kernel; /* idle time included */
    LARGE_INTEGER user;
    LARGE_INTEGER reserved[2];
    ULONG reserved2;
} CorePerf;

typedef LONG(WINAPI* NtQuerySystemInformationFunc)(ULONG, PVOID, ULONG, PULONG);
#define PROCESSOR_PERFORMANCE_CLASS 8

static guint cpu_count(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors > 0 ? (guint)si.dwNumberOfProcessors : 1;
}

static void times_set(SysCpuTimes* t, guint64 idle, guint64 kernel, guint64 user) {
    memset(t, 0, sizeof(*t));
    t->user = user;
    t->system = kernel > idle ? kernel - idle : 0;
    t->idle = idle;
}

static gboolean cpu_all(SysCpuTimes* all) {
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) return FALSE;
    times_set(all, filetime_u64(idle), filetime_u64(kernel), filetime_u64(user));
    return TRUE;
}

/* All CPUs, then each one (the share of all when they cannot be read) */
static void read_cpus(GArray* out) {
    SysCpuTimes all;
    if (!cpu_all(&all)) return;
    g_array_append_val(out, all);
    guint n = cpu_count();
    NtQuerySystemInformationFunc query = (NtQuerySystemInformationFunc)ntdll_function("NtQuerySystemInformation");
    CorePerf* perf = g_new0(CorePerf, n);
    ULONG got = 0;
    guint count = 0;
    if (query != NULL && query(PROCESSOR_PERFORMANCE_CLASS, perf, (ULONG)(n * sizeof(CorePerf)), &got) == 0) {
        count = MIN(n, (guint)(got / sizeof(CorePerf)));
    }
    for (guint i = 0; i < count; i++) {
        SysCpuTimes t;
        times_set(&t, (guint64)perf[i].idle.QuadPart, (guint64)perf[i].kernel.QuadPart, (guint64)perf[i].user.QuadPart);
        g_array_append_val(out, t);
    }
    for (guint i = 0; count == 0 && i < n; i++) {
        SysCpuTimes t = {0};
        t.user = all.user / n;
        t.system = all.system / n;
        t.idle = all.idle / n;
        g_array_append_val(out, t);
    }
    g_free(perf);
}

static gboolean read_mem(SysMemInfo* mem) {
    memset(mem, 0, sizeof(*mem));
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return FALSE;
    mem->total = ms.ullTotalPhys;
    mem->available = ms.ullAvailPhys;
    mem->free = ms.ullAvailPhys;
    PERFORMANCE_INFORMATION pi;
    pi.cb = sizeof(pi);
    if (GetPerformanceInfo(&pi, sizeof(pi))) {
        mem->cached = (guint64)pi.SystemCache * pi.PageSize;
    }
    /* The page files: what the commit limit adds to the physical memory */
    if (ms.ullTotalPageFile > ms.ullTotalPhys) {
        mem->swap_total = ms.ullTotalPageFile - ms.ullTotalPhys;
        guint64 avail = ms.ullAvailPageFile > ms.ullAvailPhys ? ms.ullAvailPageFile - ms.ullAvailPhys : 0;
        mem->swap_free = MIN(avail, mem->swap_total);
    }
    return TRUE;
}

/* The network adapters that are up, without the loopback and the filters
 * stacked on an adapter (they count the same bytes) */
static GPtrArray* read_net(void) {
    GPtrArray* out = g_ptr_array_new_with_free_func(sysstat_net_dev_free);
    MIB_IF_TABLE2* table = NULL;
    if (GetIfTable2(&table) != NO_ERROR) return out;
    for (ULONG i = 0; i < table->NumEntries; i++) {
        const MIB_IF_ROW2* row = &table->Table[i];
        if (row->Type == IF_TYPE_SOFTWARE_LOOPBACK || row->OperStatus != IfOperStatusUp ||
            !row->InterfaceAndOperStatusFlags.HardwareInterface || row->InterfaceAndOperStatusFlags.FilterInterface) {
            continue;
        }
        SysNetDev* d = g_new0(SysNetDev, 1);
        d->name = g_strdup_printf("%lu", (gulong)row->InterfaceIndex);
        d->rx_bytes = row->InOctets;
        d->tx_bytes = row->OutOctets;
        g_ptr_array_add(out, d);
    }
    FreeMibTable(table);
    return out;
}

/* The physical drives (no rights needed for IOCTL_DISK_PERFORMANCE). The
 * busy time is the time of the query less the idle time: only its changes
 * between two readings mean something, as for io_ms. */
static GPtrArray* read_disks(void) {
    GPtrArray* out = g_ptr_array_new_with_free_func(sysstat_disk_dev_free);
    for (guint n = 0; n < MAX_PHYSICAL_DRIVES; n++) {
        wchar_t path[40];
        _snwprintf(path, G_N_ELEMENTS(path), L"\\\\.\\PhysicalDrive%u", n);
        path[G_N_ELEMENTS(path) - 1] = 0;
        HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;
        DISK_PERFORMANCE perf;
        DWORD got = 0;
        if (DeviceIoControl(h, IOCTL_DISK_PERFORMANCE, NULL, 0, &perf, sizeof(perf), &got, NULL)) {
            SysDiskDev* d = g_new0(SysDiskDev, 1);
            d->name = g_strdup_printf("PhysicalDrive%u", n);
            d->read_sectors = (guint64)perf.BytesRead.QuadPart / SECTOR_BYTES;
            d->write_sectors = (guint64)perf.BytesWritten.QuadPart / SECTOR_BYTES;
            guint64 query = (guint64)perf.QueryTime.QuadPart;
            guint64 idle = (guint64)perf.IdleTime.QuadPart;
            d->io_ms = query > idle ? (query - idle) / TICKS_PER_MS : 0;
            g_ptr_array_add(out, d);
        }
        CloseHandle(h);
    }
    return out;
}

/* The fixed and removable drives with a medium: "C:\", its label, its
 * filesystem */
static GPtrArray* read_mounts(void) {
    GPtrArray* out = g_ptr_array_new_with_free_func(sysstat_mount_free);
    wchar_t drives[512];
    DWORD len = GetLogicalDriveStringsW(G_N_ELEMENTS(drives) - 1, drives);
    if (len == 0 || len >= G_N_ELEMENTS(drives)) return out;
    DWORD old_mode = 0;
    SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &old_mode); /* no "insert a disk" box */
    for (const wchar_t* d = drives; *d != 0; d += wcslen(d) + 1) {
        UINT type = GetDriveTypeW(d);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) continue;
        ULARGE_INTEGER avail, total, free_bytes;
        if (!GetDiskFreeSpaceExW(d, &avail, &total, &free_bytes) || total.QuadPart == 0) continue;
        wchar_t label[MAX_PATH + 1] = {0};
        wchar_t fs[MAX_PATH + 1] = {0};
        GetVolumeInformationW(d, label, G_N_ELEMENTS(label), NULL, NULL, NULL, fs, G_N_ELEMENTS(fs));
        SysMount* m = g_new0(SysMount, 1);
        m->mount_point = wide_to_utf8(d);
        m->device = label[0] != 0 ? wide_to_utf8(label) : g_strdup(m->mount_point);
        m->fs_type = wide_to_utf8(fs);
        m->total = total.QuadPart;
        m->avail = avail.QuadPart;
        m->used = total.QuadPart > free_bytes.QuadPart ? total.QuadPart - free_bytes.QuadPart : 0;
        g_ptr_array_add(out, m);
    }
    SetThreadErrorMode(old_mode, NULL);
    return out;
}

/* Account names by SID, shared by the worker threads */
G_LOCK_DEFINE_STATIC(account_names);
static GHashTable* account_names;

static gchar* account_name(PSID sid) {
    LPWSTR sid_text = NULL;
    if (!ConvertSidToStringSidW(sid, &sid_text)) return g_strdup("");
    gchar* key = wide_to_utf8(sid_text);
    LocalFree(sid_text);
    gchar* name = NULL;
    G_LOCK(account_names);
    if (account_names == NULL) account_names = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    const gchar* known = g_hash_table_lookup(account_names, key);
    if (known != NULL) name = g_strdup(known);
    G_UNLOCK(account_names);
    if (name != NULL) {
        g_free(key);
        return name;
    }
    wchar_t user[256];
    wchar_t domain[256];
    DWORD user_len = G_N_ELEMENTS(user);
    DWORD domain_len = G_N_ELEMENTS(domain);
    SID_NAME_USE use;
    name = LookupAccountSidW(NULL, sid, user, &user_len, domain, &domain_len, &use) ? wide_to_utf8(user)
                                                                                   : g_strdup(key);
    G_LOCK(account_names);
    g_hash_table_replace(account_names, key, g_strdup(name));
    G_UNLOCK(account_names);
    return name;
}

static gchar* process_user(HANDLE proc) {
    HANDLE token = NULL;
    if (!OpenProcessToken(proc, TOKEN_QUERY, &token)) return g_strdup("");
    DWORD64 buf[64]; /* TOKEN_USER and its SID, aligned */
    DWORD got = 0;
    gchar* name = GetTokenInformation(token, TokenUser, buf, sizeof(buf), &got)
                      ? account_name(((TOKEN_USER*)buf)->User.Sid)
                      : g_strdup("");
    CloseHandle(token);
    return name;
}

/* Every process but the idle one; those of other users (services) may not
 * open: no times, memory or user then */
static GPtrArray* read_procs(void) {
    GPtrArray* out = g_ptr_array_new_with_free_func(sysstat_proc_free);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    guint64 page = sysstat_page_size();
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    for (BOOL more = Process32FirstW(snap, &pe); more; more = Process32NextW(snap, &pe)) {
        if (pe.th32ProcessID == 0) continue;
        SysProc* p = g_new0(SysProc, 1);
        p->pid = (gint)pe.th32ProcessID;
        p->name = wide_to_utf8(pe.szExeFile);
        p->state = '?';
        p->threads = (guint)pe.cntThreads;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (h != NULL) {
            FILETIME created, exited, kernel, user;
            if (GetProcessTimes(h, &created, &exited, &kernel, &user)) {
                p->cpu_ticks = filetime_u64(kernel) + filetime_u64(user);
            }
            PROCESS_MEMORY_COUNTERS pmc;
            pmc.cb = sizeof(pmc);
            if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) p->rss_pages = (guint64)pmc.WorkingSetSize / page;
            p->user = process_user(h);
            CloseHandle(h);
        } else {
            p->user = g_strdup("");
        }
        g_ptr_array_add(out, p);
    }
    CloseHandle(snap);
    return out;
}

gboolean sysstat_host_available(void) {
    return TRUE;
}

gboolean sysstat_host_has_load(void) {
    return FALSE;
}

gboolean sysstat_host_has_proc_state(void) {
    return FALSE;
}

SysSample* sysstat_host_sample(SysSampleFlags flags) {
    SysSample* s = g_new0(SysSample, 1);
    s->time_us = g_get_monotonic_time();
    s->cpu = g_array_new(FALSE, TRUE, sizeof(SysCpuTimes));
    s->core_percent = g_array_new(FALSE, TRUE, sizeof(gdouble));
    read_cpus(s->cpu);
    read_mem(&s->mem);
    s->uptime = (gdouble)GetTickCount64() / 1000.0;
    s->net = read_net();
    s->disks = read_disks();
    if (flags & SYSSTAT_MOUNTS) s->mounts = read_mounts();
    if (flags & SYSSTAT_PROCS) {
        s->procs = read_procs();
        s->tasks_total = s->procs->len;
    }
    return s;
}

gboolean sysstat_host_cpu(SysCpuTimes* all, guint* ncpu) {
    if (!cpu_all(all)) return FALSE;
    *ncpu = cpu_count();
    return TRUE;
}

gboolean sysstat_host_mem(SysMemInfo* mem) {
    return read_mem(mem);
}

gboolean sysstat_host_load(gdouble load[3]) {
    load[0] = load[1] = load[2] = 0;
    return FALSE;
}

typedef LONG(WINAPI* RtlGetVersionFunc)(OSVERSIONINFOW*);

gchar* sysstat_host_os(void) {
    /* GetVersionEx() reports the version the program was made for */
    RtlGetVersionFunc get_version = (RtlGetVersionFunc)ntdll_function("RtlGetVersion");
    OSVERSIONINFOW v;
    memset(&v, 0, sizeof(v));
    v.dwOSVersionInfoSize = sizeof(v);
    if (get_version == NULL || get_version(&v) != 0) return g_strdup("Windows");
    if (v.dwMajorVersion == 10) {
        /* Windows 11 still says 10.0: its builds start at 22000 */
        return g_strdup_printf("Windows %s (%lu)", v.dwBuildNumber >= 22000 ? "11" : "10", (gulong)v.dwBuildNumber);
    }
    return g_strdup_printf("Windows %lu.%lu (%lu)", (gulong)v.dwMajorVersion, (gulong)v.dwMinorVersion,
                           (gulong)v.dwBuildNumber);
}

#else

/* ------------------------------------------------------------------------- */
/* Linux (/proc)                                                             */
/* ------------------------------------------------------------------------- */

static gchar* read_proc(const gchar* name) {
    gchar* path = g_build_filename("/proc", name, NULL);
    gchar* text = NULL;
    if (!g_file_get_contents(path, &text, NULL, NULL)) text = NULL;
    g_free(path);
    return text;
}

gboolean sysstat_host_available(void) {
    return sysstat_available(NULL);
}

gboolean sysstat_host_has_load(void) {
    return sysstat_host_available();
}

gboolean sysstat_host_has_proc_state(void) {
    return sysstat_host_available();
}

SysSample* sysstat_host_sample(SysSampleFlags flags) {
    return sysstat_sample(NULL, flags);
}

gboolean sysstat_host_cpu(SysCpuTimes* all, guint* ncpu) {
    gchar* text = read_proc("stat");
    GArray* cpu = g_array_new(FALSE, TRUE, sizeof(SysCpuTimes));
    gboolean ok = sysstat_parse_cpu(text, cpu);
    g_free(text);
    if (ok) {
        *all = g_array_index(cpu, SysCpuTimes, 0);
        *ncpu = cpu->len > 1 ? cpu->len - 1 : 1;
    }
    g_array_unref(cpu);
    return ok;
}

gboolean sysstat_host_mem(SysMemInfo* mem) {
    gchar* text = read_proc("meminfo");
    gboolean ok = sysstat_parse_meminfo(text, mem);
    g_free(text);
    return ok;
}

gboolean sysstat_host_load(gdouble load[3]) {
    load[0] = load[1] = load[2] = 0;
    gchar* text = read_proc("loadavg");
    gboolean ok = sysstat_parse_loadavg(text, load, NULL, NULL);
    g_free(text);
    return ok;
}

gchar* sysstat_host_os(void) {
    gchar* release = read_proc("sys/kernel/osrelease");
    if (release != NULL) {
        gchar* os = g_strdup_printf("Linux %s", g_strstrip(release));
        g_free(release);
        return os;
    }
    gchar* name = g_get_os_info(G_OS_INFO_KEY_NAME);
    return name != NULL ? name : g_strdup("?");
}

#endif
