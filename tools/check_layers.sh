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

# Modules live in src/<dir>/<module>.c + .h (main.c at the top of src/).
# A module is named by its file name, wherever its directory is.
SRC_C=$(find src -name '*.c' | sort)
SRC_CH=$(find src -name '*.[ch]' | sort)

# The file of a module ($1: name, $2: c or h), empty if there is none
mod_file() {
    for f in "src/$1.$2" src/*/"$1.$2"; do
        if [ -f "$f" ]; then echo "$f"; return 0; fi
    done
    return 0
}

# Project headers included by a file, as module names (no directory)
included() {
    sed -n 's/^#include "\([a-z_0-9/]*\)\.h".*/\1/p' "$1" | sed 's|.*/||'
}

# Modules are in the directories of src/: only main.c is at the top
for f in src/*.[ch]; do
    [ "$f" = src/main.c ] && continue
    echo "layout: $f is at the top of src/ (put the module in a directory, docs/architecture.md)"
    status=1
done

# Module names are unique: the objects are flat in build/
dups=$(for f in $SRC_CH; do basename "$f"; done | sort | uniq -d)
for d in $dups; do
    echo "layout: $d exists in several directories of src/ (module names are unique)"
    status=1
done

# Project headers are included by their path from src/ ("canvas/canvas_doc.h"),
# never by a bare name that only resolves next to the including file
for f in $SRC_CH tests/*.c tests/*.h tools/*.c; do
    for inc in $(sed -n 's/^#include "\([^"]*\)".*/\1/p' "$f"); do
        [ -f "src/$inc" ] && continue
        case "$f" in
            src/*) ;;
            *) [ -f "$(dirname "$f")/$inc" ] && continue ;;
        esac
        echo "includes: $f includes \"$inc\" (write the path from src/, e.g. \"core/i18n.h\")"
        status=1
    done
done

# module:allowed-project-headers (comma separated)
RULES="
app_paths:
i18n:
file_types:text_encoding,i18n
file_type_icons:file_type_icons_table
git_utils:palette,i18n
line_diff:
preview_cache:
path_kind:
pane_geometry:
minimap_geometry:
ui_probe:
dir_summary:file_types,text_encoding,i18n
dir_scan:file_types
file_save:
hex_dump:
ps_convert:i18n
elf_info:i18n
zip_reader:i18n
gif_decoder:i18n
ole_reader:i18n
ole_props:ole_reader,preview_doc,i18n
doc_reader:ole_reader,preview_doc,doc_writer,i18n
xls_reader:ole_reader,spreadsheet,i18n
ppt_reader:ole_reader,preview_doc,slide_render,i18n
mail_message:
mail_view:mail_message,preview_doc,i18n
mail_box:mail_message
mail_dir:mail_message,preview_doc,i18n
mail_msg:mail_message,ole_reader,rtf,i18n
mail_tnef:mail_message,mail_msg,i18n
mail_store:preview_doc,i18n
office97:ole_reader,ole_props,preview_doc,doc_reader,xls_reader,spreadsheet,ppt_reader,slide_render,mail_msg,mail_view,mail_message,i18n
docx:zip_reader,zip_xml,preview_doc,doc_writer,docx_styles,i18n
docx_styles:zip_reader,zip_xml,preview_doc,doc_writer
preview_doc:
doc_writer:preview_doc
csv:preview_doc,i18n
json_view:preview_doc,i18n
diff_view:preview_doc,i18n
archive_listing:zip_reader,i18n
archive_extract:app_paths,zip_reader,i18n
cert_view:preview_doc,i18n
card_view:preview_doc,i18n
log_view:preview_doc
exif:i18n
font_info:i18n
man_view:preview_doc,i18n
notebook:json_view,i18n
zip_xml:zip_reader
spreadsheet:zip_reader,zip_xml,preview_doc,i18n
odf:zip_reader,zip_xml,i18n
pptx:zip_reader,zip_xml,preview_doc,pptx_shapes,slide_render,i18n
pptx_shapes:pptx_shapes_private,zip_reader,zip_xml,preview_doc,slide_render
pptx_build:pptx_shapes,pptx_shapes_private,preview_doc,slide_render
slide_render:preview_doc,i18n
epub:zip_reader,zip_xml,i18n
rtf:i18n
sqlite_view:preview_doc,i18n
gettext_view:preview_doc,i18n
class_info:preview_doc,i18n
wasm_info:preview_doc,i18n
torrent_info:preview_doc,i18n
pyc_info:preview_doc,i18n
tfm_info:preview_doc,i18n
pcap_info:preview_doc,i18n
media_tags:
media_tracks:i18n
media_player:media_tracks,i18n
track_menu:media_player,i18n
text_encoding:
vt_screen:
vt_pty:i18n
term_widget:vt_screen,vt_pty
preview_formats:i18n,mail_box,mail_message,mail_store,mail_tnef,mail_view,office97,file_types,text_encoding,preview_doc,docx,zip_reader,elf_info,hex_dump,csv,json_view,diff_view,archive_listing,cert_view,card_view,log_view,font_info,man_view,notebook,zip_xml,spreadsheet,odf,pptx,epub,rtf,sqlite_view,gettext_view,class_info,wasm_info,torrent_info,pyc_info,tfm_info,pcap_info
org_time:
org_doc:org_doc_private,org_time
org_list:org_doc,org_doc_private
org_table:org_doc
dunnet:
vim_keys:
nav_keys:
vim_regex:
vim_motion:
vim_parse:vim_keys
vim_ex:
vim_engine:vim_engine_private,vim_keys,vim_motion,text_tags
vim_engine_motions:vim_engine_private,vim_motion,vim_regex
vim_engine_insert:vim_engine_private,vim_keys,vim_motion
vim_engine_ops:vim_engine_private,vim_keys,vim_motion
vim_engine_normal:vim_engine_private,vim_keys,vim_motion
vim_engine_visual:vim_engine_private,vim_keys,vim_motion
vim_engine_ex:vim_engine_private,vim_ex,vim_keys,vim_motion,vim_regex
diff_gutter:git_utils,line_diff,palette
color_column:palette
explorer_row:
code_symbol:i18n
code_scan_c:code_symbol
code_scan:code_symbol,code_scan_c
code_outline_tree:code_symbol,i18n
code_tags:code_symbol,json_view
code_index:code_symbol,code_scan,code_tags,git_utils
jump_list:
trash_restore:
note_types:
html_text:
notes_store:note_types,notebook_icons
notebook_icons:
canvas_doc:
canvas_geometry:
canvas_history:
canvas_rough_draw:canvas_doc,canvas_geometry,palette
canvas_text:canvas_doc,fonts,palette,text_tags
user_guide:
fuzzy_match:
web_address:
path_globs:
text_search:code_index,i18n
session_store:app_paths,layout_store
tab_tree:
settings_store:app_paths
palette:
image_theme:palette
fonts:
theme_store:fonts,palette,app_paths
text_tags:fonts,palette
lsp_protocol:code_symbol,json_view
lsp_client:lsp_protocol
lsp_config:code_symbol
layout_store:app_paths,tab_tree,i18n
text_style:app_types,file_types,app_paths,palette,theme
theme:palette,fonts,theme_store,file_type_icons,icon_sets
markdown:app_types,image_utils,fonts,text_tags,markdown_table,markdown_link,markdown_parse,md_out,icons
markdown_parse:md_out,markdown_table,image_utils,i18n
md_out:
markdown_link:
markdown_table:fonts,palette,markdown_link,md_out
html_tags:fonts,text_tags
html:app_types,html_text,image_utils,icons,i18n
minimap:app_types,diff_gutter,line_diff,minimap_geometry,palette,preview_state
preview_doc_view:preview_doc,html,html_tags,markdown,image_utils,text_style,app_types,palette,fonts,text_tags
tree_nav:app_types
app_events:app_types
panels:app_types,app_events,nav_keys
outline_pane:code_outline_tree,code_symbol,icons,panels,i18n,nav_keys
focus_route:app_types,app_events,nav_keys,panels
icons:file_type_icons,palette
icon_sets:app_paths,theme_store
icon_picker:icons
image_utils:
animation:gif_decoder,image_utils
dialog:
tab_drop:pane_geometry
after_frame:
explorer_tree:explorer_row,icons
remote_store:app_types,app_paths,notebook_icons
preview_summary:app_types,remote_store,explorer_row,note_types,canvas_doc,i18n
fs_watch:app_types,app_events,git_utils,git_tab,preview_state
preview_state:app_types
git_state:
"

for rule in $RULES; do
    mod=${rule%%:*}
    allowed=$(echo "$mod,${rule#*:}" | tr ',' '\n')
    for f in $(mod_file "$mod" c) $(mod_file "$mod" h); do
        for inc in $(included "$f"); do
            if ! echo "$allowed" | grep -qx "$inc"; then
                echo "layering: $f includes \"$inc.h\" (allowed: $(echo $allowed))"
                status=1
            fi
        done
    done
done

# notes_private.h is internal to the Notes view files
for f in $(grep -lE '#include "([a-z]+/)?notes_private.h"' $SRC_CH); do
    case "$(basename "$f")" in
        notes.c|notes_dialogs.c|notes_actions.c) ;;
        *) echo "layering: $f includes notes_private.h (internal to notes.c, notes_dialogs.c, notes_actions.c)"; status=1 ;;
    esac
done

# editor_private.h is internal to the editor tab files
for f in $(grep -lE '#include "([a-z]+/)?editor_private.h"' $SRC_CH); do
    case "$(basename "$f")" in
        editor.c|editor_notebooks.c|editor_tab_menu.c) ;;
        *) echo "layering: $f includes editor_private.h (internal to editor.c, editor_notebooks.c, editor_tab_menu.c)"; status=1 ;;
    esac
done

# editor_modes_private.h is internal to the editor mode files
for f in $(grep -lE '#include "([a-z]+/)?editor_modes_private.h"' $SRC_CH); do
    case "$(basename "$f")" in
        editor_modes.c|editor_notepad.c|editor_emacs.c|editor_block_cursor.c|editor_vim.c|editor_org.c) ;;
        *) echo "layering: $f includes editor_modes_private.h (internal to editor mode files)"; status=1 ;;
    esac
done

# pptx_shapes_private.h is internal to the .pptx shape files
for f in $(grep -lE '#include "([a-z]+/)?pptx_shapes_private.h"' $SRC_CH); do
    case "$(basename "$f")" in
        pptx_shapes.c|pptx_build.c) ;;
        *) echo "layering: $f includes pptx_shapes_private.h (internal to pptx_shapes.c, pptx_build.c)"; status=1 ;;
    esac
done

# canvas_page_private.h is internal to the Canvas page files
for f in $(grep -lE '#include "([a-z]+/)?canvas_page_private.h"' $SRC_CH); do
    case "$(basename "$f")" in
        canvas_view.c|canvas_page.c|canvas_keys.c|canvas_drag.c|canvas_clip.c|canvas_menu.c|canvas_rotate.c) ;;
        *) echo "layering: $f includes canvas_page_private.h (internal to the Canvas page files)"; status=1 ;;
    esac
done

# Event subscribers are called through app_emit() (app_events.h), not directly,
# outside their own module and main.c (where they are subscribed).
for pair in update_path_label:topbar update_status_bar:status_bar \
            workspace_save_current:workspace workspace_apply_positions:workspace \
            refresh_listing:file_list; do
    fn=${pair%%:*}
    owner=${pair#*:}
    for f in $(grep -lE "\b$fn\(" $SRC_C); do
        case "$(basename "$f")" in
            $owner.c|main.c) ;;
            *) echo "layering: $f calls $fn() directly; use app_emit() (see app_events.h)"; status=1 ;;
        esac
    done
done

# The explorer view changes through explorer_set_view() (tree_nav.c); only the
# workspace switch restores it directly.
for f in $(grep -lE 'app->sidebar_view = ' $SRC_C); do
    case "$(basename "$f")" in
        tree_nav.c|workspace.c) ;;
        *) echo "layering: $f sets app->sidebar_view; use explorer_set_view() (tree_nav.h)"; status=1 ;;
    esac
done

# Every interaction stays inside the main window: no other top-level window
# (dialogs are AppDialog overlays, menus are popovers anchored in the window).
for f in $(grep -lE 'gtk_window_new|gtk_(alert|file|color|font)_dialog_new|gtk_file_chooser|gtk_message_dialog|gtk_dialog_new|gtk_app_chooser_dialog|GTK_TYPE_WINDOW|GTK_TYPE_DIALOG' $SRC_C); do
    case "$(basename "$f")" in
        main.c) ;;
        *) echo "layering: $f opens a separate window; use AppDialog (dialog.h) inside the main window"; status=1 ;;
    esac
done

# Dependencies between the directories of src/ (dir:included-dirs), frozen at
# their current state: the feature directories are already tightly coupled, so
# a new dependency between two directories fails until it is listed here.
# Removing one is always welcome (then drop it from the list).
DIR_DEPS="
canvas:core,editor,explorer,theme,widgets
chrome:canvas,core,editor,explorer,games,git,markup,media,notes,preview,search,terminal,theme,widgets,workspace
code:core,editor,formats,git,markup,preview,theme,widgets
core:explorer,git,notes,terminal
editor:canvas,code,core,explorer,games,git,org,preview,search,terminal,theme,vim,widgets,workspace
explorer:chrome,code,core,editor,formats,games,git,media,notes,preview,search,terminal,theme,widgets,workspace
formats:core,preview
games:core,editor,explorer,theme,widgets
git:core,theme,widgets
mail:core,office,preview
markup:core,media,theme
media:core,editor,preview,search,theme
notes:canvas,core,editor,explorer,preview,theme,widgets
office:core,formats,mail,preview
org:core,editor,media,preview,theme
preview:canvas,chrome,code,core,editor,explorer,formats,games,git,mail,markup,media,notes,office,org,search,theme,widgets
search:code,core,editor,explorer,theme,widgets
terminal:core,editor,explorer,theme,widgets,workspace
theme:core
vim:theme
widgets:code,core,explorer,git,preview,theme
workspace:canvas,chrome,core,editor,explorer,games,git,media,preview,search,terminal,theme,widgets
"
for d in src/*/; do
    dir=$(basename "$d")
    line=$(printf '%s\n' "$DIR_DEPS" | grep "^$dir:" || true)
    if [ -z "$line" ]; then
        echo "dirs: src/$dir/ has no entry in DIR_DEPS (tools/check_layers.sh)"
        status=1
        continue
    fi
    allowed=",${line#*:},"
    for f in "$d"*.[ch]; do
        for inc in $(sed -n 's/^#include "\([a-z_0-9]*\)\/[a-z_0-9]*\.h".*/\1/p' "$f" | sort -u); do
            [ "$inc" = "$dir" ] && continue
            case "$allowed" in
                *",$inc,"*) ;;
                *) echo "dirs: $f includes a header of $inc/, a new dependency of $dir/ (move the code down, emit an event, or list it in DIR_DEPS)"; status=1 ;;
            esac
        done
    done
done

# No new include cycles: two modules must not include each other's header.
# The remaining pairs are the explorer host and its sidebar views (the explorer
# dispatches keys/rows to the view, the view asks the explorer to navigate).
ALLOWED_CYCLES="bookmarks:file_list file_list:remote"
for c in $SRC_C; do
    a=$(basename "$c" .c)
    for h in $(included "$c"); do
        [ "$h" = "$a" ] && continue
        hc=$(mod_file "$h" c)
        [ -n "$hc" ] || continue
        included "$hc" | grep -qx "$a" || continue
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
for f in $(grep -lE '#[0-9a-fA-F]{6}\b' $SRC_CH); do
    case "$(basename "$f")" in
        palette.c|palette.h|theme_store.c|theme_store.h) ;;
        *) echo "colours: $f has a colour literal; use a palette role (palette.h)"; status=1 ;;
    esac
done

# Fonts come from the font roles (fonts.h: @font_ui / @size_... in CSS,
# fonts_family() in C): no font family or pixel size written elsewhere
for f in $(grep -lE "font-family: *[^@ ]|font-size: *[0-9.]+px|\"family\", *\"[^@]|font_family=\\\\\"[a-zA-Z]|<tt>" $SRC_C); do
    case "$(basename "$f")" in
        fonts.c) ;;
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

# Icon sets (icons/sets/<set>/): files named lookin-set-<icon>[-symbolic].svg,
# without transforms, each listed in the GResource manifest
for f in icons/sets/*/*.svg; do
    case "$(basename "$f")" in
        lookin-set-*.svg) ;;
        *) echo "icons: $f must be named lookin-set-<icon>.svg"; status=1 ;;
    esac
    if grep -q 'transform' "$f"; then
        echo "icons: $f uses a transform (not rendered by some GTK versions)"
        status=1
    fi
    if ! grep -q "sets/$(basename "$(dirname "$f")")/$(basename "$f")<" icons/lookin.gresource.xml; then
        echo "icons: $f is missing from icons/lookin.gresource.xml"
        status=1
    fi
done

[ $status -eq 0 ] && echo "Layering OK."
exit $status
