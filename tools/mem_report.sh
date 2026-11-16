#!/bin/sh
# Where the resident memory of a running LookinFiles goes (docs/performance.md):
# /proc/<pid>/smaps summed by kind of mapping, then the largest mappings.
#
# usage: tools/mem_report.sh [PID]    (default: the newest lookinfiles process)
# Linux only. Run it while the memory is high, before quitting.
set -eu

PID=${1:-$(pgrep -n -x lookinfiles || true)}
[ -n "$PID" ] || { echo "mem_report: no lookinfiles process (give its PID)"; exit 1; }
[ -r "/proc/$PID/smaps" ] || { echo "mem_report: cannot read /proc/$PID/smaps"; exit 1; }

echo "PID $PID, $(ls /proc/$PID/task | wc -l) threads"
grep -E '^(Rss|Pss|Anonymous|Private_Dirty|Swap):' "/proc/$PID/smaps_rollup" | tr -s ' ' | sed 's/^/  /'
echo
awk '
/^[0-9a-f]+-[0-9a-f]+ / {
    name = ""
    for (i = 6; i <= NF; i++) name = name (i > 6 ? " " : "") $i
    if (name == "[heap]") kind = "heap (malloc)"
    else if (name == "") kind = "anonymous (malloc arenas, thread stacks, images)"
    else if (name ~ /^\[stack/) kind = "main stack"
    else if (name ~ /^\/dev\/dri|drm|i915|amdgpu|nvidia|^\/dev\/(nvidia|kgsl)/) kind = "GPU buffers (/dev/dri, driver)"
    else if (name ~ /^\/memfd:|^\/dev\/shm|^SYSV|\(deleted\)/) kind = "shared memory (memfd, shm, SYSV)"
    else if (name ~ /\.so([.0-9]*)?$/) kind = "libraries"
    else if (name ~ /^\[/) kind = "kernel ([vdso], [vvar]...)"
    else kind = "mapped files (fonts, caches, data)"
    next
}
/^Rss:/ { rss[kind] += $2; if (name != "") file[name] += $2 }
END {
    print "resident by kind:"
    for (k in rss) printf "  %7.1f MB  %s\n", rss[k] / 1024, k | "sort -rn"
    close("sort -rn")
    print ""
    print "largest named mappings:"
    for (f in file) if (file[f] > 2048) printf "  %7.1f MB  %s\n", file[f] / 1024, f | "sort -rn | head -12"
}' "/proc/$PID/smaps"
