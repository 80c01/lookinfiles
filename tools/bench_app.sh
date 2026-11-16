#!/bin/sh
# Application benchmark (make bench-app, docs/performance.md): runs the
# application on a virtual X display with LOOKIN_PROFILE and measures
#   - the start-up: launch to the window drawn and idle (BENCH_RUNS runs,
#     median), and the costliest start-up steps,
#   - the idle cost: CPU, resident memory and frames drawn while nobody
#     touches the window (BENCH_IDLE seconds),
#   - browsing: the preview time of each file while going down the list, the
#     slow frames (over 16.7 ms) and the memory afterwards.
# Fails when a result exceeds its budget (BENCH_* variables below).
#
# usage: tools/bench_app.sh [BINARY]
# Requires: Xvfb, xdotool (sudo apt install xvfb xdotool), Linux (/proc).
# Xvfb draws with the CPU (GSK_RENDERER=cairo): the times are those of a slow
# display, comparable between two runs on the same machine, not absolute.
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN=$(cd "$(dirname "${1:-$ROOT/lookinfiles}")" && pwd)/$(basename "${1:-lookinfiles}")
OUT=${BENCH_OUT:-$ROOT/build/bench}
case "$OUT" in /*) ;; *) OUT=$(pwd)/$OUT ;; esac
FIX=${TMPDIR:-/tmp}/lookin-bench
RUNS=${BENCH_RUNS:-3}
IDLE=${BENCH_IDLE:-5}

# Budgets
MAX_STARTUP_MS=${BENCH_STARTUP_MS:-600}      # LOOKIN_PROFILE origin to "startup done"
MAX_IDLE_CPU=${BENCH_IDLE_CPU:-1.0}          # % of one core while idle
MAX_IDLE_RSS_MB=${BENCH_IDLE_RSS_MB:-150}    # resident memory when idle
MAX_IDLE_FPS=${BENCH_IDLE_FPS:-1.5}          # frames per second drawn while idle
MAX_PREVIEW_MS=${BENCH_PREVIEW_MS:-100}      # slowest preview while browsing
MAX_BROWSE_RSS_MB=${BENCH_BROWSE_RSS_MB:-200} # resident memory after browsing

export LANGUAGE=en
for tool in Xvfb xdotool; do
    command -v $tool >/dev/null 2>&1 || { echo "bench-app: '$tool' not found. Install: sudo apt install xvfb xdotool"; exit 1; }
done
[ -x "$BIN" ] || { echo "bench-app: binary not found: $BIN (run make first)"; exit 1; }
[ -r /proc/self/stat ] || { echo "bench-app: needs Linux (/proc)"; exit 1; }

# --- Fixture: a git repository of 2000 files (sources, text, Markdown, JSON)
rm -rf "$OUT" "$FIX"
mkdir -p "$OUT" "$FIX/home/.config/gtk-4.0" "$FIX/work"
printf '[Settings]\ngtk-cursor-blink=false\n' > "$FIX/home/.config/gtk-4.0/settings.ini"
W=$FIX/work
for d in 1 2 3 4 5 6 7 8 9 10; do
    mkdir -p "$W/dir$d"
    for f in $(seq 1 200); do : > "$W/dir$d/file$f.txt"; done
done
cp "$ROOT"/src/preview/viewer.c "$ROOT"/src/explorer/file_list.c "$ROOT"/src/main.c "$W/"
cp "$ROOT"/AGENTS.md "$ROOT"/docs/architecture.md "$ROOT"/docs/previews.md "$W/"
seq -f 'plain line %g of a long text file' 1 20000 > "$W/long.txt"
awk 'BEGIN { print "["; for (i = 1; i <= 2000; i++) printf "  {\"id\": %d, \"name\": \"item %d\"}%s\n", i, i, (i < 2000 ? "," : ""); print "]" }' > "$W/data.json"
awk 'BEGIN { print "id,name,amount"; for (i = 1; i <= 5000; i++) printf "%d,Name %d,%d.50\n", i, i, i }' > "$W/table.csv"
(cd "$W" && git init -q && git add -A && git -c user.name=bench -c user.email=bench@localhost commit -qm fixture \
    && echo change >> long.txt && : > untracked.md) >/dev/null 2>&1

rm -f "$OUT/display"
Xvfb -displayfd 3 -screen 0 1280x800x24 -nolisten tcp 3>"$OUT/display" 2>/dev/null &
XVFB=$!
APP=
cleanup() {
    [ -n "$APP" ] && kill "$APP" 2>/dev/null
    kill $XVFB 2>/dev/null
}
trap cleanup EXIT INT TERM

until_true() {
    limit=$1
    shift
    i=0
    until "$@"; do
        [ $i -ge "$limit" ] && return 1
        sleep 0.01
        i=$((i + 1))
    done
}
until_true 300 test -s "$OUT/display" || { echo "bench-app: FAIL - Xvfb did not start"; exit 1; }

export DISPLAY=":$(cat "$OUT/display")"
export GDK_BACKEND=x11 GSK_RENDERER=cairo NO_AT_BRIDGE=1
export HOME="$FIX/home" XDG_CONFIG_HOME="$FIX/home/.config"
cd "$W" || exit 1
TICK=$(getconf CLK_TCK)

status=0
fail() {
    echo "bench-app: FAIL - $*"
    status=1
}
# a > b for decimals
gt() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a > b) }'; }
# CPU ticks (user + system) and resident KB of the application
cpu_ticks() { awk '{ print $14 + $15 }' "/proc/$APP/stat"; }
rss_kb() { awk '/^VmRSS:/ { print $2 }' "/proc/$APP/status"; }
# The value of key=... in the report line of a log
report() { sed -n "s/.*profile: report .*\b$2=\([-0-9.]*\).*/\1/p" "$1"; }
# The total ms of a mark of a log
mark_ms() { sed -n "s/^profile: mark *\([0-9.]*\) ms +[0-9.]* $2\$/\1/p" "$1" | head -1; }

# Start the application with its profile in $1; returns once it is idle
launch() {
    LOOKIN_PROFILE="$1" "$BIN" -s 1280x800 "$W" > "$1.out" 2>&1 &
    APP=$!
    until_true 1000 grep -q 'startup done' "$1" 2>/dev/null || { fail "start-up did not finish (see $1.out)"; cat "$1.out"; exit 1; }
}
quit() {
    xdotool key ctrl+q
    until_true 300 sh -c "! kill -0 $APP 2>/dev/null" || fail "Ctrl+Q did not quit"
    wait $APP 2>/dev/null
    APP=
}

# --- Start-up and idle
echo "bench-app: $RUNS start-ups, then $IDLE s idle each"
: > "$OUT/startup.txt"
for r in $(seq 1 "$RUNS"); do
    LOG=$OUT/run$r.log
    t0=$(date +%s%3N)
    launch "$LOG"
    wall=$(( $(date +%s%3N) - t0 ))
    echo "$(mark_ms "$LOG" 'startup done') $(mark_ms "$LOG" 'first frame') $wall" >> "$OUT/startup.txt"
    c0=$(cpu_ticks)
    sleep "$IDLE"
    c1=$(cpu_ticks)
    rss=$(rss_kb)
    quit
    echo "$c0 $c1 $rss $(report "$LOG" frames) $(report "$LOG" unpainted)" >> "$OUT/idle.txt"
done
med() { sort -n | awk '{ v[NR] = $1 } END { print v[int((NR + 1) / 2)] }'; }
startup=$(awk '{ print $1 }' "$OUT/startup.txt" | med)
first=$(awk '{ print $2 }' "$OUT/startup.txt" | med)
wall=$(awk '{ print $3 }' "$OUT/startup.txt" | med)
idle_cpu=$(awk -v t="$TICK" -v s="$IDLE" '{ print ($2 - $1) * 100 / t / s }' "$OUT/idle.txt" | med)
idle_rss=$(awk '{ print $3 }' "$OUT/idle.txt" | med)
idle_fps=$(awk -v s="$IDLE" '{ print $4 / s }' "$OUT/idle.txt" | med)
idle_ups=$(awk -v s="$IDLE" '{ print $5 / s }' "$OUT/idle.txt" | med)

printf '  start-up            %6.0f ms to idle (first frame %.0f ms, launch to idle %s ms incl. exec)\n' "$startup" "$first" "$wall"
echo "  costliest steps (run 1):"
awk '/^profile: mark/ { sub(/^\+/, "", $5); d = $5 + 0; $1 = $2 = $3 = $4 = $5 = ""; sub(/^ +/, ""); print d, $0 }' "$OUT/run1.log" \
    | sort -rn | head -5 | awk '{ d = $1; $1 = ""; printf "      %7.1f ms %s\n", d, $0 }'
printf '  idle CPU            %6.2f %% of a core\n' "$idle_cpu"
printf '  idle memory         %6.1f MB resident\n' "$(awk -v k="$idle_rss" 'BEGIN { print k / 1024 }')"
printf '  idle frames         %6.2f per second painted, %.2f without drawing\n' "$idle_fps" "$idle_ups"

# --- Browsing: down the list, one preview per file
LOG=$OUT/browse.log
launch "$LOG"
xdotool key --delay 150 $(for i in $(seq 1 40); do printf 'Down '; done)
sleep 1
rss=$(rss_kb)
quit
sed -n 's/^profile: preview .*: \([0-9.]*\) ms.*/\1/p' "$LOG" | sort -n > "$OUT/previews.txt"
n=$(wc -l < "$OUT/previews.txt")
pmax=$(tail -1 "$OUT/previews.txt")
pmed=$(med < "$OUT/previews.txt")
slow=$(report "$LOG" slow)
worst=$(report "$LOG" worst_ms)
printf '  previews            %6s  (median %s ms, slowest %s ms)\n' "$n" "${pmed:-?}" "${pmax:-?}"
echo "  slowest previews:"
sed -n 's/^profile: preview \(.*\): \([0-9.]*\) ms.*/\2 \1/p' "$LOG" | sort -rn | head -3 \
    | awk '{ d = $1; $1 = ""; printf "      %7.1f ms%s\n", d, $0 }'
printf '  frames while browsing: %s slow, worst %s ms\n' "${slow:-?}" "${worst:-?}"
printf '  memory after        %6.1f MB resident\n' "$(awk -v k="$rss" 'BEGIN { print k / 1024 }')"

# --- Budgets
gt "$startup" "$MAX_STARTUP_MS" && fail "start-up $startup ms > $MAX_STARTUP_MS ms"
gt "$idle_cpu" "$MAX_IDLE_CPU" && fail "idle CPU $idle_cpu % > $MAX_IDLE_CPU %"
gt "$((idle_rss / 1024))" "$MAX_IDLE_RSS_MB" && fail "idle memory $((idle_rss / 1024)) MB > $MAX_IDLE_RSS_MB MB"
gt "$idle_fps" "$MAX_IDLE_FPS" && fail "idle frames $idle_fps/s > $MAX_IDLE_FPS/s"
[ -n "$pmax" ] && gt "$pmax" "$MAX_PREVIEW_MS" && fail "slowest preview $pmax ms > $MAX_PREVIEW_MS ms"
gt "$((rss / 1024))" "$MAX_BROWSE_RSS_MB" && fail "memory after browsing $((rss / 1024)) MB > $MAX_BROWSE_RSS_MB MB"
[ "$n" -gt 0 ] || fail "no preview timing in $LOG"

echo "  logs: $OUT"
[ $status -eq 0 ] && echo "bench-app: OK"
exit $status
