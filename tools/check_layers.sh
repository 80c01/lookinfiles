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
file_types:text_encoding
git_utils:palette
line_diff:
preview_cache:
path_kind:
pane_geometry:
minimap_geometry:
dir_summary:file_types,text_encoding
hex_dump:
elf_info:
zip_reader:
gif_decoder:
ole_reader:
ole_props:ole_reader,preview_doc
doc_reader:ole_reader,preview_doc,doc_writer
xls_reader:ole_reader,spreadsheet
ppt_reader:ole_reader,preview_doc,slide_render
mail_message:
mail_view:mail_message,preview_doc
mail_box:mail_message
mail_dir:mail_message,preview_doc
mail_msg:mail_message,ole_reader,rtf
mail_tnef:mail_message,mail_msg
mail_store:preview_doc
office97:ole_reader,ole_props,preview_doc,doc_reader,xls_reader,spreadsheet,ppt_reader,slide_render,mail_msg,mail_view,mail_message
docx:zip_reader,zip_xml,preview_doc,doc_writer,docx_styles
docx_styles:zip_reader,zip_xml,preview_doc,doc_writer
preview_doc:
doc_writer:preview_doc
csv:preview_doc
json_view:preview_doc
diff_view:preview_doc
archive_listing:zip_reader
cert_view:preview_doc
card_view:preview_doc
log_view:preview_doc
exif:
font_info:
man_view:preview_doc
notebook:json_view
zip_xml:zip_reader
spreadsheet:zip_reader,zip_xml,preview_doc
odf:zip_reader,zip_xml
pptx:zip_reader,zip_xml,preview_doc,pptx_shapes,slide_render
pptx_shapes:pptx_shapes_private,zip_reader,zip_xml,preview_doc,slide_render
pptx_build:pptx_shapes,pptx_shapes_private,preview_doc,slide_render
slide_render:preview_doc
epub:zip_reader,zip_xml
rtf:
sqlite_view:preview_doc
gettext_view:preview_doc
media_tags:
text_encoding:
preview_formats:mail_box,mail_message,mail_store,mail_tnef,mail_view,office97,file_types,text_encoding,preview_doc,docx,zip_reader,elf_info,hex_dump,csv,json_view,diff_view,archive_listing,cert_view,card_view,log_view,font_info,man_view,notebook,zip_xml,spreadsheet,odf,pptx,epub,rtf,sqlite_view,gettext_view
org_time:
org_doc:org_time
org_table:org_doc
vim_keys:
nav_keys:
vim_regex:
vim_motion:
vim_parse:vim_keys
vim_ex:
vim_engine:vim_engine_private,vim_parse,vim_keys,vim_motion,vim_regex,palette,text_tags
vim_engine_ex:vim_engine_private,vim_ex,vim_keys,vim_motion,vim_regex
diff_gutter:git_utils,line_diff,palette
explorer_row:
code_symbol:
code_scan_c:code_symbol
code_scan:code_symbol,code_scan_c
code_tags:code_symbol,json_view
code_index:code_symbol,code_scan,code_tags,git_utils
jump_list:
trash_restore:
note_types:
canvas_doc:
user_guide:
fuzzy_match:
text_search:code_index
session_store:app_paths
palette:
fonts:
theme_store:fonts,palette,app_paths
text_tags:fonts,palette
lsp_protocol:code_symbol,json_view
lsp_client:lsp_protocol
lsp_config:code_symbol
layout_store:app_paths
text_style:app_types,file_types,app_paths,palette,theme
theme:palette,fonts,theme_store
markdown:app_types,image_utils,palette,fonts,text_tags
html:app_types,image_utils,palette,fonts,text_tags
minimap:app_types,diff_gutter,line_diff,minimap_geometry,palette
preview_doc_view:preview_doc,html,markdown,image_utils,text_style,app_types,palette,fonts,text_tags
tree_nav:app_types
app_events:app_types
panels:app_types,app_events,nav_keys
focus_route:app_types,app_events,nav_keys,panels
icons:
image_utils:
animation:gif_decoder,image_utils
dialog:
tab_drop:pane_geometry
explorer_tree:explorer_row
remote_store:app_types,app_paths
preview_summary:app_types,remote_store,explorer_row,note_types,canvas_doc
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

# pptx_shapes_private.h is internal to the .pptx shape files
for f in $(grep -l '#include "pptx_shapes_private.h"' src/*.c src/*.h); do
    case "$f" in
        src/pptx_shapes.c|src/pptx_build.c) ;;
        *) echo "layering: $f includes pptx_shapes_private.h (internal to pptx_shapes.c, pptx_build.c)"; status=1 ;;
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

# Colours come from the palette (palette.h: palette_color(), @role in CSS;
# theme values in theme_store),
# so that a theme can change them all: no colour literal elsewhere
for f in $(grep -lE '#[0-9a-fA-F]{6}\b' src/*.c src/*.h); do
    case "$f" in
        src/palette.c|src/palette.h|src/theme_store.c|src/theme_store.h) ;;
        *) echo "colours: $f has a colour literal; use a palette role (palette.h)"; status=1 ;;
    esac
done

# Fonts come from the font roles (fonts.h: @font_ui / @size_... in CSS,
# fonts_family() in C): no font family or pixel size written elsewhere
for f in $(grep -lE "font-family: *[^@ ]|font-size: *[0-9.]+px|\"family\", *\"[^@]|font_family=\\\\\"[a-zA-Z]|<tt>" src/*.c); do
    case "$f" in
        src/fonts.c) ;;
        *) echo "fonts: $f sets a font family or size; use the font roles (fonts.h)"; status=1 ;;
    esac
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
