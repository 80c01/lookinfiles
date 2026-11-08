#!/bin/sh
# Fast headless UI smoke test (about a second): starts the application on a
# virtual X display, drives a few steps without screenshots and checks that
#   - the window opens and Ctrl+Q quits cleanly,
#   - GTK logs no warning or critical,
#   - the minimap shows and hides exactly once per change (no flicker), from
#     the LOOKIN_DEBUG_MINIMAP log.
#
# usage: tests/ui/smoke.sh [BINARY]     (or: make ui-test)
# Requires: Xvfb, xdotool. The screenshot scenario is tests/ui/run_ui_test.sh
# (make ui-shots).
set -u

# English interface whatever the locale, so screenshots stay comparable
# (UI_LANGUAGE=fr runs the scenario with the French translation)
export LANGUAGE="${UI_LANGUAGE:-en}"

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BIN=$(cd "$(dirname "${1:-$ROOT/lookinfiles}")" && pwd)/$(basename "${1:-lookinfiles}")
OUT=${UI_OUT:-$ROOT/build/ui}
case "$OUT" in /*) ;; *) OUT=$ROOT/$OUT ;; esac
FIX=${TMPDIR:-/tmp}/lookin-smoke
LOG=$OUT/smoke.log

for tool in Xvfb xdotool; do
    if ! command -v $tool >/dev/null 2>&1; then
        echo "ui-test: '$tool' not found. Install: sudo apt install xvfb xdotool"
        exit 1
    fi
done
[ -x "$BIN" ] || { echo "ui-test: binary not found: $BIN (run make first)"; exit 1; }

START=$(date +%s%3N)
rm -rf "$FIX"
mkdir -p "$OUT" "$FIX/home/.config/gtk-4.0" "$FIX/work"
printf '[Settings]\ngtk-cursor-blink=false\n' > "$FIX/home/.config/gtk-4.0/settings.ini"
seq -f 'plain line %g' 1 1500 > "$FIX/work/a_long.txt"
seq -f 'short line %g' 1 5 > "$FIX/work/b_short.txt"

rm -f "$OUT/smoke_display"
Xvfb -displayfd 3 -screen 0 1280x800x24 -nolisten tcp 3>"$OUT/smoke_display" 2>/dev/null &
XVFB=$!
APP=
cleanup() {
    [ -n "$APP" ] && kill "$APP" 2>/dev/null
    kill $XVFB 2>/dev/null
}
trap cleanup EXIT INT TERM

# Poll every 10 ms until a command succeeds, at most $1 hundredths of a second
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
until_true 300 test -s "$OUT/smoke_display" || { echo "ui-test: FAIL - Xvfb did not start"; exit 1; }

export DISPLAY=":$(cat "$OUT/smoke_display")"
export GDK_BACKEND=x11 GSK_RENDERER=cairo NO_AT_BRIDGE=1
export HOME="$FIX/home" XDG_CONFIG_HOME="$FIX/home/.config"
cd "$FIX/work" || exit 1
LOOKIN_DEBUG_MINIMAP=1 "$BIN" -s 1280x800 "$FIX/work" > "$LOG" 2>&1 &
APP=$!

status=0
fail() {
    echo "ui-test: FAIL - $*"
    status=1
}
# Number of minimap show / hide lines in the log
events() { grep -c '^minimap .* \(show\|hide\):' "$LOG"; }
has_events() { [ "$(events)" -ge "$1" ]; }
# Wait for the n-th show / hide, then leave 100 ms for another one to follow
# (a flicker would: a confirmation waits at most about 100 ms)
expect_events() {
    until_true 200 has_events "$1" || fail "minimap: expected $1 show/hide, got $(events) ($2)"
    sleep 0.1
}

until_true 500 has_events 1 || { fail "the window did not open"; cat "$LOG"; exit 1; }
xdotool search --sync --pid $APP >/dev/null 2>&1

key() { xdotool key --delay 20 "$@"; }
resize() { xdotool search --pid $APP 2>/dev/null | tail -1 | xargs -I{} xdotool windowsize {} "$1" "$2"; }

# 1: long file selected at startup (shown), 2: short file (hidden), 3: long
# file again (shown); resizing over the long or short file changes nothing
expect_events 1 "long file at startup"
key Down;  expect_events 2 "short file"
resize 1000 700; resize 1280 800; sleep 0.1
key Up;    expect_events 3 "long file again"
resize 1000 700; resize 1280 800; sleep 0.1
# 4-5: Help tab (preview minimap hidden, help minimap shown), 6: closed (the
# help minimap goes with its tab, the preview one comes back)
key F1;     expect_events 5 "Help tab"
key ctrl+w; expect_events 6 "Help tab closed"

# Each minimap's own sequence (two minimaps changing in the same frame may
# log in either order)
check_sequence() {
    got=$(sed -n "s/^minimap $1 \(show\|hide\):.*/\1/p" "$LOG" | tr '\n' ' ')
    [ "$got" = "$2 " ] || fail "$1 minimap: got '$got', expected '$2'"
}
check_sequence preview "show hide show hide show"
check_sequence help "show"

key ctrl+q
if until_true 100 sh -c "! kill -0 $APP 2>/dev/null"; then
    wait $APP || fail "the application exited with an error"
    APP=
else
    fail "Ctrl+Q did not quit the application"
fi

grep -E 'WARNING|CRITICAL' "$LOG" | grep -v 'Unable to acquire session bus' > "$OUT/smoke_warnings.log"
[ -s "$OUT/smoke_warnings.log" ] && fail "GTK warnings/criticals (see $LOG):
$(sed 's/^/  /' "$OUT/smoke_warnings.log")"

echo "ui-test: $(($(date +%s%3N) - START)) ms"
[ $status -eq 0 ] && echo "ui-test: OK"
exit $status
