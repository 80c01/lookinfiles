#!/bin/sh
# Enforce module layering (run by `make check`).
#
# Foundation modules are pure helpers: they must not include application
# headers, so they stay reusable and unit-testable without a display.
# Leaf UI modules may use AppData and foundation modules, but never the
# feature modules (file_list, viewer, editor, ui, workspace, ...).
set -eu
cd "$(dirname "$0")/.."

status=0

# module:allowed-project-headers (comma separated)
RULES="
app_paths:
file_types:
git_utils:
line_diff:
pane_geometry:
minimap_geometry:
dir_summary:file_types
hex_dump:
elf_info:
zip_reader:
docx:zip_reader
org_time:
org_doc:org_time
org_table:org_doc
vim_keys:
vim_regex:
vim_motion:
vim_parse:vim_keys
vim_ex:
vim_engine:vim_engine_private,vim_parse,vim_keys,vim_motion,vim_regex
vim_engine_ex:vim_engine_private,vim_ex,vim_keys,vim_motion,vim_regex
diff_gutter:git_utils,line_diff
explorer_row:
layout_store:app_paths
text_style:app_types,file_types
theme:
markdown:app_types,image_utils
html:app_types,image_utils
minimap:app_types,diff_gutter,line_diff,minimap_geometry
tree_nav:app_types
app_events:app_types
panels:app_types,app_events
icons:
image_utils:
dialog:
tab_drop:pane_geometry
explorer_tree:explorer_row
remote_store:app_types,app_paths
preview_summary:app_types,remote_store,explorer_row
fs_watch:app_types,app_events,git_utils,git_tab
"

for rule in $RULES; do
    mod=${rule%%:*}
    allowed=$(echo "$mod,${rule#*:}" | tr ',' '\n')
    for f in "src/$mod.c" "src/$mod.h"; do
        [ -f "$f" ] || continue
        for inc in $(sed -n 's/^#include "\([a-z_]*\)\.h".*/\1/p' "$f"); do
            if ! echo "$allowed" | grep -qx "$inc"; then
                echo "layering: $f includes \"$inc.h\" (allowed: $(echo $allowed))"
                status=1
            fi
        done
    done
done

# editor_modes_private.h is internal to the editor mode files
for f in $(grep -l '#include "editor_modes_private.h"' src/*.c src/*.h); do
    case "$f" in
        src/editor_modes.c|src/editor_notepad.c|src/editor_emacs.c|src/editor_vim.c|src/editor_org.c) ;;
        *) echo "layering: $f includes editor_modes_private.h (internal to editor mode files)"; status=1 ;;
    esac
done

# Event subscribers are called through app_emit() (app_events.h), not directly,
# outside their own module and main.c (where they are subscribed).
for pair in update_path_label:topbar update_status_bar:status_bar \
            workspace_save_current:workspace workspace_apply_positions:workspace \
            refresh_listing:file_list; do
    fn=${pair%%:*}
    owner=${pair#*:}
    for f in $(grep -lE "\b$fn\(" src/*.c); do
        case "$f" in
            src/$owner.c|src/main.c) ;;
            *) echo "layering: $f calls $fn() directly; use app_emit() (see app_events.h)"; status=1 ;;
        esac
    done
done

# Every interaction stays inside the main window: no other top-level window
# (dialogs are AppDialog overlays, menus are popovers anchored in the window).
for f in $(grep -lE 'gtk_window_new|gtk_(alert|file|color|font)_dialog_new|gtk_file_chooser|gtk_message_dialog|gtk_dialog_new|gtk_app_chooser_dialog|GTK_TYPE_WINDOW|GTK_TYPE_DIALOG' src/*.c); do
    case "$f" in
        src/main.c) ;;
        *) echo "layering: $f opens a separate window; use AppDialog (dialog.h) inside the main window"; status=1 ;;
    esac
done

# No new include cycles: two modules must not include each other's header.
# The remaining pairs are the explorer host and its sidebar views (the explorer
# dispatches keys/rows to the view, the view asks the explorer to navigate).
ALLOWED_CYCLES="bookmarks:file_list file_list:remote"
for c in src/*.c; do
    a=$(basename "$c" .c)
    for h in $(sed -n 's/^#include "\([a-z_]*\)\.h".*/\1/p' "$c"); do
        [ "$h" = "$a" ] && continue
        [ -f "src/$h.c" ] || continue
        grep -q "^#include \"$a.h\"" "src/$h.c" || continue
        [ "$a" \< "$h" ] || continue
        case " $ALLOWED_CYCLES " in
            *" $a:$h "*) ;;
            *) echo "layering: include cycle $a.c <-> $h.c (use app_events or move the shared code down a layer)"; status=1 ;;
        esac
    done
done

# Bundled icons (icons/COPYING): single-colour symbolic SVGs without transforms,
# each listed in the GResource manifest
for f in icons/scalable/actions/*.svg; do
    if grep -q 'transform' "$f"; then
        echo "icons: $f uses a transform (not rendered by some GTK versions)"
        status=1
    fi
    if ! grep -q "scalable/actions/$(basename "$f")" icons/lookin.gresource.xml; then
        echo "icons: $f is missing from icons/lookin.gresource.xml"
        status=1
    fi
done

[ $status -eq 0 ] && echo "Layering OK."
exit $status
