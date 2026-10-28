#!/bin/sh
# Headless UI smoke test: drives the application on a virtual X display.
#
# usage: tests/ui/run_ui_test.sh [BINARY]     (or: make ui-test)
#
# Requires: Xvfb, xdotool, ImageMagick (sudo apt install xvfb xdotool imagemagick)
#
# Environment:
#   UI_OUT       output directory (default: build/ui); screenshots go to $UI_OUT/shots
#   UI_BASELINE  optional directory of reference screenshots (e.g. the shots of a
#                run on another branch); every step except the terminal one must
#                match it within UI_TOLERANCE differing pixels (default: 50,
#                colors within 10% count as equal to absorb anti-aliasing noise)
#
# Fails when: the window does not open, GTK logs a warning or critical, Ctrl+Q
# does not quit cleanly, or a screenshot differs from the baseline.
set -u

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BIN=$(cd "$(dirname "${1:-$ROOT/explorateur_fichiers}")" && pwd)/$(basename "${1:-explorateur_fichiers}")
OUT=${UI_OUT:-$ROOT/build/ui}
case "$OUT" in /*) ;; *) OUT=$ROOT/$OUT ;; esac
TOLERANCE=${UI_TOLERANCE:-50}
SHOTS=$OUT/shots
# Fixed fixture path: the top bar shows it, so screenshots stay comparable
# between runs from different checkouts or output directories.
FIX=${TMPDIR:-/tmp}/explorateur_fichiers-ui

for tool in Xvfb xdotool import compare; do
    if ! command -v $tool >/dev/null 2>&1; then
        echo "ui-test: '$tool' not found. Install: sudo apt install xvfb xdotool imagemagick"
        exit 1
    fi
done
[ -x "$BIN" ] || { echo "ui-test: binary not found: $BIN (run make first)"; exit 1; }

rm -rf "$OUT" "$FIX"
mkdir -p "$SHOTS" "$FIX/home/.config/gtk-4.0" "$FIX/work/src" "$FIX/work/docs"

# --- Fixture: small git repository with one modified and several untracked files
printf '[Settings]\ngtk-cursor-blink=false\n' > "$FIX/home/.config/gtk-4.0/settings.ini"
(
    cd "$FIX/work" || exit 1
    printf '# Title\n\nSome *markdown* text.\n\n- item one\n- item two\n' > README.md
    printf '#include <stdio.h>\nint main(void) {\n    printf("hi\\n");\n    return 0;\n}\n' > src/main.c
    printf 'plain line 1\nplain line 2\n' > notes.txt
    printf 'R&D <draft>\n' > 'R&D.txt'
    printf '* TODO Task one\n** Sub heading\nbody\n' > docs/plan.org
    git init -q -b master . 2>/dev/null || git init -q .
    git add README.md
    GIT_AUTHOR_DATE="2000-01-01T00:00:00Z" GIT_COMMITTER_DATE="2000-01-01T00:00:00Z" \
        git -c user.email=ui@test -c user.name=ui commit -qm init
    echo changed >> README.md
) || { echo "ui-test: fixture setup failed"; exit 1; }

# --- Virtual display (free display number chosen by Xvfb)
Xvfb -displayfd 3 -screen 0 1280x800x24 -nolisten tcp 3>"$OUT/display" 2>/dev/null &
XVFB=$!
i=0
while [ ! -s "$OUT/display" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
[ -s "$OUT/display" ] || { echo "ui-test: Xvfb did not start"; kill $XVFB 2>/dev/null; exit 1; }

export DISPLAY=":$(cat "$OUT/display")"
export GDK_BACKEND=x11 GSK_RENDERER=cairo NO_AT_BRIDGE=1
export HOME="$FIX/home" XDG_CONFIG_HOME="$FIX/home/.config"

APP=
cleanup() {
    [ -n "$APP" ] && kill "$APP" 2>/dev/null
    kill $XVFB 2>/dev/null
}
trap cleanup EXIT INT TERM

cd "$FIX/work" || exit 1
"$BIN" -s 1280x800 "$FIX/work" > "$OUT/app.log" 2>&1 &
APP=$!

WIN=
i=0
while [ -z "$WIN" ] && [ $i -lt 50 ]; do
    sleep 0.2
    WIN=$(xdotool search --pid $APP 2>/dev/null | tail -1)
    i=$((i + 1))
done
[ -n "$WIN" ] || { echo "ui-test: FAIL - application window did not open"; cat "$OUT/app.log"; exit 1; }
sleep 2
xdotool windowfocus --sync "$WIN" 2>/dev/null

# --- Scenario: one screenshot per step, named NN_<step>.png
n=0
shot() {
    n=$((n + 1))
    sleep 1
    import -window root "$SHOTS/$(printf %02d $n)_$1.png"
}
key() { xdotool key --delay 120 "$@"; }

shot start
key j j;                shot select_readme_md
key g g;                shot gg_first_row
key g g;                shot gg_reselect_same_row
key space;              shot expand_docs
key j;                  shot select_plan_org
xdotool mousemove 390 88 click 1
                        shot org_todo_clicked
key alt+Left;           shot explorer_focused
key Return;             shot open_org_in_editor
xdotool mousemove 600 79 click 1     # cursor on the first heading
key shift+Right;        shot org_editor_todo_cycled
# Org files open in Emacs mode: C-x C-s with Ctrl held down
xdotool keydown ctrl key --delay 120 x s keyup ctrl
                        shot org_editor_saved
key alt+Left;           shot explorer_focused_again
key question;           shot help_open
key question;           shot help_closed
key G;                  shot G_last_row
key k;                  shot select_previous_row
key j j;                shot select_next_rows
key Return;             shot open_editor_notepad
xdotool mousemove 1237 783 click 1
                        shot mode_menu
xdotool mousemove 1205 650 click 1   # pick "Notepad" (closes the menu)
xdotool mousemove 700 300 click 1    # focus the editor text
xdotool type --delay 80 "Edited "
                        shot editor_modified
key ctrl+s;             shot editor_saved
key ctrl+w;             shot editor_closed
key alt+plus;           shot workspace_added
key alt+1;              shot workspace_1
key i;                  shot hidden_toggle
key o;                  shot sort_cycle
key ctrl+t; sleep 1;    shot terminal
key ctrl+q

status=0
i=0
while kill -0 $APP 2>/dev/null && [ $i -lt 30 ]; do sleep 0.1; i=$((i + 1)); done
if kill -0 $APP 2>/dev/null; then
    echo "ui-test: FAIL - Ctrl+Q did not quit the application"
    status=1
else
    wait $APP
    rc=$?
    APP=
    [ $rc -eq 0 ] || { echo "ui-test: FAIL - application exited with code $rc"; status=1; }
fi

# --- GTK warnings and criticals (the missing session bus is expected headless)
grep -E 'WARNING|CRITICAL' "$OUT/app.log" | grep -v 'Unable to acquire session bus' > "$OUT/warnings.log"
if [ -s "$OUT/warnings.log" ]; then
    echo "ui-test: FAIL - GTK warnings/criticals (see $OUT/app.log):"
    sed 's/^/  /' "$OUT/warnings.log"
    status=1
fi

# --- Optional comparison with reference screenshots
if [ -n "${UI_BASELINE:-}" ]; then
    for f in "$SHOTS"/*.png; do
        name=$(basename "$f")
        case "$name" in *_terminal.png) continue ;; esac   # shell prompt timing varies
        ref="$UI_BASELINE/$name"
        if [ ! -f "$ref" ]; then
            echo "ui-test: FAIL - no baseline for $name"
            status=1
            continue
        fi
        diff=$(compare -fuzz 10% -metric AE "$f" "$ref" /dev/null 2>&1 | cut -d' ' -f1)
        if [ "${diff%.*}" -gt "$TOLERANCE" ] 2>/dev/null; then
            echo "ui-test: FAIL - $name differs from baseline ($diff pixels)"
            status=1
        fi
    done
fi

echo "ui-test: $n screenshots in $SHOTS"
[ $status -eq 0 ] && echo "ui-test: OK"
exit $status
