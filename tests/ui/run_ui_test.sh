#!/bin/sh
# Headless UI smoke test: drives the application on a virtual X display.
#
# usage: tests/ui/run_ui_test.sh [BINARY]     (or: make ui-test)
#
# Requires: Xvfb, xdotool, ImageMagick (sudo apt install xvfb xdotool imagemagick)
#
# Environment:
#   UI_OUT       output directory (default: build/ui); screenshots go to $UI_OUT/shots
#   UI_ICON_THEME  optional icon theme to force (e.g. hicolor, which has no
#                symbolic icons: checks that every icon falls back to one built
#                into GTK; a missing icon logs a warning and fails the test)
#   UI_BASELINE  optional directory of reference screenshots (e.g. the shots of a
#                run on another branch); every step except the terminal and
#                animation ones must
#                match it within UI_TOLERANCE differing pixels (default: 50,
#                colors within 10% count as equal to absorb anti-aliasing noise)
#
# Fails when: the window does not open, a dialog opens a separate window, GTK
# logs a warning or critical, Ctrl+Q does not quit cleanly, or a screenshot
# differs from the baseline.
set -u

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BIN=$(cd "$(dirname "${1:-$ROOT/lookin}")" && pwd)/$(basename "${1:-lookin}")
OUT=${UI_OUT:-$ROOT/build/ui}
case "$OUT" in /*) ;; *) OUT=$ROOT/$OUT ;; esac
TOLERANCE=${UI_TOLERANCE:-50}
SHOTS=$OUT/shots
# Fixed fixture path: the top bar shows it, so screenshots stay comparable
# between runs from different checkouts or output directories.
FIX=${TMPDIR:-/tmp}/lookin-ui

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
if [ -n "${UI_ICON_THEME:-}" ]; then
    printf 'gtk-icon-theme-name=%s\n' "$UI_ICON_THEME" >> "$FIX/home/.config/gtk-4.0/settings.ini"
fi
CFG="$FIX/home/.config/lookin"
mkdir -p "$CFG/notes/General" "$CFG/notes/Projects and long notebook name"
echo a > "$CFG/notes/General/short.txt"
echo c > "$CFG/notes/Projects and long notebook name/plan.md"
{
    printf '<?xml version="1.0" encoding="UTF-8"?>\n<bookmarks version="2">\n'
    printf '<folder name="Work folders" expanded="true">\n'
    printf '<bookmark name="Source directory of the project" target="%s/work/src"/>\n' "$FIX"
    printf '</folder>\n</bookmarks>\n'
} > "$CFG/bookmarks.xml"
mkdir -p "$FIX/home/.ssh"
printf 'Host demo-server\n    HostName 192.0.2.10\n    User demo\n' > "$FIX/home/.ssh/config"
(
    cd "$FIX/work" || exit 1
    printf '# Title\n\nSome *markdown* text.\n\n- item one\n- item two\n\n- [x] done task\n- [/] running task\n- [-] other running task\n- [ ] todo task\n' > README.md
    printf '#include <stdio.h>\nstatic int answer(void) {\n    return 42;\n}\nint main(void) {\n    printf("hi %%d\\n", answer());\n    return 0;\n}\n' > src/main.c
    printf '# Sources\n\nThe *entry point* is `main.c`.\n' > src/README.md   # folder preview README
    seq -f 'plain line %g' 1 1500 > notes.txt
    printf 'R&D <draft>\n' > 'R&D.txt'
    printf '* TODO Task one\n** Sub heading\nbody\n' > docs/plan.org
    # No extension: the preview detects the type from the content
    convert -size 160x90 gradient:'#3a7bd5-#00d2ff' png:docs/screenshot
    printf '#!/bin/sh\n# Greet the user\necho "hello $USER"\n' > docs/tool
    # ELF executable: architecture, type, linked libraries
    cp /bin/true docs/true
    # Unknown binary format: hex dump preview (all 256 byte values, twice)
    for i in $(seq 0 511); do printf "\\$(printf %03o $((i % 256)))"; done > docs/unknown.bin
    # Word document: headings, lists, table, image, link (tests/ui/fixtures)
    cp "$ROOT/tests/ui/fixtures/report.docx" docs/word-report.docx
    git init -q -b master . 2>/dev/null || git init -q .
    git add README.md notes.txt
    GIT_AUTHOR_DATE="2000-01-01T00:00:00Z" GIT_COMMITTER_DATE="2000-01-01T00:00:00Z" \
        git -c user.email=ui@test -c user.name=ui commit -qm init
    echo changed >> README.md
    # Change bars since the last commit: modified, added, deleted lines
    # (long file: the minimap shows them too)
    seq -f 'plain line %g' 1 1500 | sed -e '1s/1/one/' -e '2a added line' -e '50,59d' \
        -e '90,95s/$/ (edited)/' -e '$a added at the end' > notes.txt
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
# Drag with the left button from (x1, y1) to (x2, y2) in small steps; with a
# name, shoot while the button is still held
drag() {
    xdotool mousemove "$1" "$2" mousedown 1
    sleep 0.2
    i=1
    while [ $i -le 10 ]; do
        xdotool mousemove $(($1 + ($3 - $1) * i / 10)) $(($2 + ($4 - $2) * i / 10))
        sleep 0.05
        i=$((i + 1))
    done
    xdotool mousemove $(($3 + 1)) "$4"
    [ -n "${5:-}" ] && shot "$5"
    xdotool mouseup 1
}

shot start
key j j;                shot select_readme_md
key j;                  shot preview_readme_md_tasks
key g g;                shot gg_first_row
key g g;                shot gg_reselect_same_row
key j;                  shot dir_preview_readme   # src: table, git line, rendered README.md
key k
key space;              shot expand_docs
key j;                  shot select_plan_org
xdotool mousemove 390 88 click 1
                        shot org_todo_clicked
key alt+Left;           shot explorer_focused
key Return;             shot open_org_in_editor
xdotool mousemove 600 79 click 1     # cursor on the first heading
key shift+Right;        shot org_editor_todo_cycled
# Org editing: heading folding, global visibility, structure editing, tables
key Tab;                shot org_editor_children   # opened folded (#+STARTUP: overview)
key Tab;                shot org_editor_subtree
key Tab;                shot org_editor_folded
key Tab
key shift+Tab;          shot org_editor_contents   # global cycle from the startup overview
key shift+Tab;          shot org_editor_show_all
key alt+Return
xdotool type --delay 80 "Second"
                        shot org_editor_new_heading
key alt+Up;             shot org_editor_subtree_moved_up
xdotool keydown ctrl key --delay 120 c t keyup ctrl
                        shot org_editor_ctrl_c_ctrl_t
key End Return
xdotool type --delay 60 "|name|qty|"
key Tab
xdotool type --delay 60 "apple"
key Tab
xdotool type --delay 60 "12"
key Tab;                shot org_editor_table_aligned
# Minibuffer: Tab completes M-x command names, Escape quits
key alt+x
xdotool type --delay 60 "org-cl"
key Tab;                shot minibuffer_tab_completion
key Escape;             shot minibuffer_quit
# Incremental search: extends in place, fails, wraps, DEL undoes, C-g goes back
key ctrl+s
xdotool type --delay 80 "ap"
                        shot isearch_typed
key ctrl+s;             shot isearch_failing
key ctrl+s;             shot isearch_wrapped
key BackSpace BackSpace; shot isearch_backspace
key ctrl+g;             shot isearch_failing_part_dropped
key ctrl+g;             shot isearch_cancelled   # back where the search started
key ctrl+s
xdotool type --delay 80 "task"
key Return;             shot isearch_accepted
# C-SPC sets the mark, then C-f / C-n extend the region; C-g drops it
key alt+less            # M-<: beginning of buffer
xdotool keydown ctrl key --delay 120 space f f f f f n keyup ctrl
                        shot emacs_region
key ctrl+g;             shot emacs_region_cleared
# C-SPC C-SPC sets then deactivates the mark
xdotool keydown ctrl key --delay 120 space space f keyup ctrl
                        shot emacs_mark_deactivated
# C-u C-c . inserts a timestamp with the current time (undone afterwards)
key End
xdotool keydown ctrl key --delay 120 u c keyup ctrl
key period;             shot org_timestamp_time_prompt
key Return;             shot org_timestamp_with_time
key ctrl+slash
# M-x text-mode: plain Emacs editing of the .org file (unfolded, modeline Text);
# M-x org-mode: back to Org-mode, folded as at startup
key alt+x; xdotool type --delay 60 "text-mode"; key Return
                        shot emacs_text_mode
key Tab;                shot text_mode_tab_no_fold
key ctrl+slash          # undo the Tab
key alt+x; xdotool type --delay 60 "org-mode"; key Return
                        shot emacs_org_mode
# Org files open in Emacs mode: C-x C-s with Ctrl held down
xdotool keydown ctrl key --delay 120 x s keyup ctrl
                        shot org_editor_saved
xdotool mousemove 90 122 click 1     # plan.org row: focus the explorer (M-Left is an Emacs key)
                        shot explorer_focused_again
key j;                  shot detect_image_without_extension
key j;                  shot detect_script_without_extension
key j;                  shot elf_summary
key m;                  shot elf_raw_hex_dump
key m
key j;                  shot hex_dump_unknown_binary
key j;                  shot word_document
key m;                  shot word_document_raw_hex_dump
key m
key k k k k k           # back to plan.org
key question;           shot help_open
key question;           shot help_closed
key G;                  shot G_last_row
key k;                  shot select_previous_row
key j j;                shot select_next_rows
key Return;             shot open_editor_notepad
xdotool mousemove 1220 600 click --repeat 3 --delay 150 5   # wheel down over the minimap
                        shot minimap_wheel_scrolled
xdotool click --repeat 3 --delay 150 4                       # and back up
xdotool mousemove 1237 783 click 1
                        shot mode_menu
xdotool mousemove 1205 650 click 1   # pick "Notepad" (closes the menu)
xdotool mousemove 700 300 click 1    # focus the editor text
xdotool type --delay 80 "Edited "
                        shot editor_modified
key ctrl+s;             shot editor_saved
key ctrl+alt+n;         shot editor_line_numbers
key ctrl+alt+n;         shot editor_line_numbers_hidden
xdotool mousemove 1237 783 click 1
xdotool mousemove 1218 721 click 1   # pick "Vim"
key Escape g g l v l l l j; shot vim_visual
key V;                  shot vim_visual_line
key Escape;             shot vim_visual_cancelled
# Vim engine: block selection, pending operator, recording, Ex substitute,
# incremental search, then undo back to the saved text (no [+])
key ctrl+v j l;         shot vim_visual_block
key Escape 2 d;         shot vim_operator_pending
key Escape q a;         shot vim_recording
key q
key colon w Tab;        shot vim_wildmenu          # every match listed, first one inserted
key Tab Tab;            shot vim_wildmenu_cycled
key ctrl+u Escape
key colon
xdotool type --delay 60 "%s/Second/2nd/"
                        shot vim_cmdline
key Return;             shot vim_substituted
key slash
xdotool type --delay 60 "Task"
                        shot vim_incsearch
key Return;             shot vim_search_highlight
key u;                  shot vim_undone
xdotool mousemove 415 50 click 1;   shot preview_default_mode   # bar: Notepad, not Vim
xdotool mousemove 730 50 click 1     # back to the Vim tab (notes.txt)
xdotool mousemove 700 300 click 1;  shot vim_tab_mode_again     # bar: Vim
key ctrl+w;             shot editor_closed
xdotool mousemove 150 600 click 1    # focus the explorer (empty area)
key ctrl+w;             shot last_editor_closed   # must show Preview, not Git Commits
key alt+plus;           shot workspace_added
key alt+1;              shot workspace_1
key i;                  shot hidden_toggle
key o;                  shot sort_cycle
# Cycling views must not leave names ellipsized (cached cell widths)
key ctrl+a;             shot bookmarks_view
key j;                  shot bookmark_dir_preview   # folder preview once its kind is read
key ctrl+a;             shot files_after_bookmarks
key ctrl+u;             shot notes_view
key ctrl+u;             shot files_after_notes
key ctrl+shift+r;       shot remote_view
key ctrl+shift+r;       shot files_view_back
# Dialogs are drawn inside the main window, never in a window of their own
key a;                  shot new_file_dialog
windows=$(xdotool search --onlyvisible --pid $APP 2>/dev/null | wc -l)
key Escape;             shot new_file_dialog_closed
key alt+l;              shot layouts_dialog       # saved workspace layouts (empty list)
key Escape;             shot layouts_dialog_closed
# Themes (Alt+T): selecting a row previews it, Escape goes back, Enter applies
key alt+t;              shot themes_dialog
key Down;               shot themes_light_preview
key Escape;             shot themes_cancelled          # Dark Modern again
key alt+t; key Down Down Return; shot themes_high_contrast_applied
key alt+t; key Home Return;      shot themes_dark_again
# Ctrl+= / Ctrl+0: larger fonts, then the theme's sizes
key ctrl+equal ctrl+equal;       shot fonts_larger
key ctrl+0;                      shot fonts_theme_sizes
echo "external" > "$FIX/work/external.txt"   # created outside the app: the tree must update
sleep 3;                shot external_file_created   # let the async rescan settle
xdotool mousemove 508 50 click 1     # Git Commits tab
# Commit made outside the app: the Git tab must list it (fixed date: stable shot)
( cd "$FIX/work" && git add external.txt &&
  GIT_AUTHOR_DATE="2000-01-02T00:00:00Z" GIT_COMMITTER_DATE="2000-01-02T00:00:00Z" \
      git -c user.email=ui@test -c user.name=ui commit -qm "external commit" )
sleep 3;                shot git_tab_after_commit
xdotool mousemove 420 120 click 1   # older commit: its diff replaces the newest one
                        shot git_older_commit
# Editor tabs dragged with the mouse: an edge of an editor splits it, its tab
# bar takes the tab (the emptied split closes)
xdotool mousemove 347 50 click 1;   shot tab_docked   # plan.org to the editor panel
xdotool mousemove 80 282 click 1; key Return;  shot second_editor_tab   # README.md, beside plan.org
drag 900 50 1000 700 tab_drag_bottom_edge;     shot tab_dropped_split_down
drag 860 359 1100 50 tab_drag_tab_bar;        shot tab_dropped_back
# Each workspace keeps its own editor splits: workspace 3 moves tabs between
# notebooks without touching (or closing) the splits of workspace 1
key ctrl+alt+Down;      shot ws1_editor_split
key alt+plus;           shot ws3_added
key G Return ctrl+alt+d; shot ws3_editor_dedicated
# The explorer can be hidden while the dedicated editor is the only other panel
xdotool mousemove 1042 17 click 1    # Preview pill: hide the preview
xdotool mousemove 948 17 click 1;    shot ws3_editor_alone   # Explorer pill: hide it
xdotool mousemove 948 17 click 1     # show the explorer again
xdotool mousemove 1042 17 click 1    # and the preview
xdotool mousemove 1000 400 click 1; key ctrl+Home; shot ws3_editor_panels_restored
key alt+Left alt+Left k Return ctrl+alt+Down; shot ws3_editor_split
key ctrl+alt+Up;        shot ws3_editor_unsplit
key alt+1;              shot ws1_editor_split_kept
key alt+3;              shot ws3_editors_kept
key alt+minus;          shot ws3_removed
# Code navigation (TASK_CODE_NAV.md): src/main.c calls answer() on line 6
key j space j j;        shot code_nav_preview_main_c
xdotool mousemove 560 160 keydown ctrl click 1 keyup ctrl   # on "answer" (line numbers shift the text)
sleep 1;                shot code_nav_preview_ctrl_click   # editor opened on "static int answer"
key ctrl+alt+minus;     shot code_nav_back            # back to the call (Notepad)
xdotool mousemove 524 160 keydown ctrl click 1 keyup ctrl
sleep 0.5;              shot code_nav_ctrl_click      # Ctrl+Click in the editor
key ctrl+alt+minus
key ctrl+k ctrl+i; sleep 1; shot code_help_card       # what answer() is
key Escape End Left Left Left
key ctrl+shift+space; sleep 1; shot code_help_signature   # inside answer( )
key Escape Left
key shift+F12; sleep 1; shot code_refs_tab            # References tab after Git Commits
key Return; sleep 0.5;  shot code_refs_opened         # first reference opened
key ctrl+shift+o; sleep 0.5; shot code_symbols_dialog
xdotool type --delay 80 "ma"
                        shot code_symbols_filtered
key Return; sleep 0.5;  shot code_symbols_jumped      # cursor on main()
xdotool mousemove 1237 783 click 1
xdotool mousemove 1218 721 click 1   # pick "Vim"
key slash; xdotool type --delay 60 "answer"; key Return
key g d; sleep 0.5;     shot code_nav_vim_gd
key ctrl+o;             shot code_nav_vim_ctrl_o
key K; sleep 1;         shot code_help_vim_K
key Escape
xdotool mousemove 430 92; sleep 0.3; xdotool mousemove 438 96   # rest on "answer" (line 2)
sleep 1.5;              shot code_help_mouse_hover
xdotool mousemove 150 600            # away: the card closes
key ctrl+w
key ctrl+t; sleep 1;    shot terminal
key ctrl+q

status=0
if [ "$windows" -ne 1 ]; then
    echo "ui-test: FAIL - the New File dialog opened a separate window ($windows windows)"
    status=1
fi
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

# --- Second session: one preview per file format (fixtures from tests/ui/formats.sh)
FMT="$FIX/formats"
mkdir -p "$FMT"
. "$ROOT/tests/ui/formats.sh"
cd "$FMT" || exit 1
TZ=UTC LOOKIN_DEBUG_PREVIEW=1 "$BIN" -s 1280x800 "$FMT" > "$OUT/app_formats.log" 2>&1 &
APP=$!
WIN=
i=0
while [ -z "$WIN" ] && [ $i -lt 50 ]; do
    sleep 0.2
    WIN=$(xdotool search --pid $APP 2>/dev/null | tail -1)
    i=$((i + 1))
done
if [ -z "$WIN" ]; then
    echo "ui-test: FAIL - format session window did not open"
    status=1
else
    sleep 2
    xdotool windowfocus --sync "$WIN" 2>/dev/null
    format_scenario
    key ctrl+q
    i=0
    while kill -0 $APP 2>/dev/null && [ $i -lt 30 ]; do sleep 0.1; i=$((i + 1)); done
    if kill -0 $APP 2>/dev/null; then
        echo "ui-test: FAIL - Ctrl+Q did not quit the format session"
        status=1
    else
        wait $APP || { echo "ui-test: FAIL - format session exited with an error"; status=1; }
        APP=
    fi
fi

# Time from the selection to the preview on screen, per format
grep '^preview ' "$OUT/app_formats.log" | sed 's/^/  /'

# --- GTK warnings and criticals (the missing session bus is expected headless)
# Known, not yet understood (TASK.md, "Known issues"): after a wide table
# preview then narrower ones, GTK sometimes snapshots the trough of the
# preview's own scrollbar before allocating it. Nothing is drawn wrong. Only
# that exact message is tolerated.
cat "$OUT/app.log" "$OUT/app_formats.log" 2>/dev/null | grep -E 'WARNING|CRITICAL' |
    grep -v 'Unable to acquire session bus' |
    grep -v 'Trying to snapshot GtkGizmo .* without a current allocation' > "$OUT/warnings.log"
if [ -s "$OUT/warnings.log" ]; then
    echo "ui-test: FAIL - GTK warnings/criticals (see $OUT/app.log, $OUT/app_formats.log):"
    sed 's/^/  /' "$OUT/warnings.log"
    status=1
fi

# --- Optional comparison with reference screenshots
if [ -n "${UI_BASELINE:-}" ]; then
    for f in "$SHOTS"/*.png; do
        name=$(basename "$f")
        case "$name" in *_terminal.png|*_animated.png) continue ;; esac   # prompt timing, animation frame vary
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
