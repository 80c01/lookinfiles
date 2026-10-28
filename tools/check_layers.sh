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
file_types:
git_utils:
text_style:app_types,file_types
theme:
markdown:app_types
html:app_types
minimap:app_types
tree_nav:app_types
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
        src/editor_modes.c|src/editor_notepad.c|src/editor_emacs.c|src/editor_vim.c) ;;
        *) echo "layering: $f includes editor_modes_private.h (internal to editor mode files)"; status=1 ;;
    esac
done

[ $status -eq 0 ] && echo "Layering OK."
exit $status
