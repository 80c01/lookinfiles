# Architecture

Module reference for LookinFiles. The short rules are in [AGENTS.md](../AGENTS.md).

## Architecture (keep it when editing)

Modules live in `src/<module>.c` + `src/<module>.h`. Layers, from bottom to top
(a module may only depend on modules in its own layer or below):

**1. Foundation — pure helpers, no `AppData`, no widgets, unit-tested in `tests/`:**
- `app_paths` — application name (`APP_NAME`), config and cache directories
  (`app_config_dir()`, `app_cache_dir()`: never hard-code the directory name).
- `file_types` — file type detection: by name (`file_extension()`,
  `is_*_file()`, `file_kind_from_name()`) and by content (`FileKind`,
  `file_kind_detect()` on the first `FILE_SNIFF_BYTES`: magic numbers, text vs
  binary; `file_kind_of_path()`). The preview
  detects the kind in its worker thread (`app->viewed_kind`): decide on the
  displayed file with `viewed_kind`, not with its name.
- `notebook_icons` — the icons offered for notebooks and bookmark folders
  (theme glyph names)
  and the mapping of the emoji stored by older versions.
- `file_type_icons` — VS Code (Seti) file-type icons: `file_type_icon()`
  gives the glyph and colour of a file name, from the table
  `file_type_icons_table.h` generated with the glyphs by
  `tools/gen_file_icons.py`. Shown by `file_type_image_new()` /
  `file_type_image_set()` (`icons.c`) wherever a file is listed (explorer
  tree, editor tabs, bookmarks, Quick Open, references; the other explorer
  rows get a matching glyph from `glyph_image_set()`), and by
  `file_type_paintable()` inside text (directory preview).
- `git_utils` — `find_git_root()`, `git_resolve_dir()` (worktrees/submodules),
  `git_read_branch()`, `scan_git_statuses()`, `git_read_head_blob()` (spawns git: worker thread only).
- `layout_store` — saved workspace layouts (`SavedLayout`: panel visibility,
  split fractions, orientation, dedicated editor panel and its splits as
  `tab_tree` text without files (`0` marks a pane of files, `t` a terminal
  tab, e.g. `H:0.500([0],[t,t])`), terminal pane tree as text such as
  `H:0.500(T,V:0.400(T,T))`), `layouts.ini` load/save, and the
  default layout of new workspaces (group `[Default]`, built-in if absent).
- `gif_decoder` — GIF frames composed on an RGBA canvas (LZW, interlacing,
  transparency, disposal, delays, loop count), played by `animation`
  (GdkPixbufAnimation is deprecated in gdk-pixbuf 2.44: never use it).
- `zip_reader` — read-only ZIP archives (`zip_reader_open()`,
  `zip_reader_read()` with a size limit; stored / deflated via GIO's raw
  `GZlibDecompressor`, ZIP64, no encryption).
- `archive_extract` — one entry of an archive written to a cache file
  (`archive_extract_entry()`: ZIP with `zip_reader`, other formats with
  libarchive), safe entry paths (`archive_extract_clean_path()`), cache path
  per archive version, names the explorer opens as folders
  (`archive_extract_can_browse()`). Used by `archive_tree`.
- `ole_reader`, `ole_props`, `office97` — OLE2 compound files (Office
  97-2003, `.msg`...): sectors, FAT / mini FAT and directory read from a
  mapped file with loop and range checks (`tests/ole_builder.h` builds them
  in tests), the property sets (`OleProperties`: card and HTML head), and the
  preview dispatch by stream names with the property card + stream list
  fallback. `doc_reader` — Word `.doc` written by `doc_writer`
  like `docx` (piece table, PAPX / CHPX FKPs, style sheet, font table,
  header stories). `xls_reader` — BIFF8 / BIFF5 workbooks to
  the `Workbook` of `spreadsheet` (shown by `spreadsheet_append_sheets()`,
  shared with `.xlsx`). `ppt_reader` — `.ppt` records to a `SlideDeck`
  (`slide_render`): persist directory, drawings (OfficeArt shapes, groups,
  properties, Pictures stream), master text styles, colour schemes, the
  slide list's text, notes.
- `mail_message`, `mail_view` — mail messages: RFC 5322 headers, RFC 2047 /
  2231 decoding, MIME tree, base64 / quoted-printable, charsets with the
  Windows-1252 fallback; the preview (header card, text or HTML body,
  attachments, forwarded messages), shared by every mail source.
  `mail_box` — mbox scan (bounded: both ends of large files). Paged
  previews: `PreviewDoc.pages` + `PreviewInput.page` (`app->preview_page`,
  `PgDn` / `PgUp` in the viewer). `mail_dir` — Maildir / MH folders as a
  message table, used by `dir_preview` instead of the listing.
  `mail_msg` — Outlook `.msg` (MAPI property streams of an OLE2 file) to a
  `MailMessage`, compressed RTF (LZFu) decoder; dispatched by `office97`.
  `mail_tnef` — TNEF `winmail.dat` (attributes + MAPI property blocks) to a
  `MailMessage`. `mail_store` — header card of PST / OST / DBX stores
  (header bytes only; no libpff).
- `docx`, `docx_styles` — Word document (`.docx`) shown like Word
  (`docx_file_preview()`, written by `doc_writer`): run and paragraph
  properties, styles with basedOn chains and document defaults, theme fonts,
  numbering labels, tables, images, the first header and footer, text
  limit. Tests build archives with `tests/zip_builder.h`.
- `slide_render` — slides as a model (`SlideDeck`: shapes with position,
  rotation, geometry, fill, outline, picture, text, tables) drawn with cairo
  and Pango into a PNG per slide (`slide_render_png()`), and the deck preview
  (`slide_deck_preview()`: one picture per slide, notes below). Shared by
  `pptx` and `ppt_reader`.
- `pptx`, `pptx_shapes`, `pptx_build` (internal `pptx_shapes_private.h`) — PowerPoint `.pptx` read into a `SlideDeck`
  (`pptx_read_deck()`): theme colours and fonts, colour modifiers, slide
  masters, layouts and placeholders (inheritance of positions, body and list
  styles), groups, freeforms, tables with the banded style, pictures,
  backgrounds, speaker notes.
- `doc_writer` — Word processing documents written as a `PreviewDoc` that
  looks like the document (`DocWriter`: runs with fonts, sizes relative to
  the default size, colours, highlights; paragraph alignment, indents,
  spacing, list labels; tables as grids with wrapped cells; pictures, page
  breaks; header and footer around the body), shared by `docx` and
  `doc_reader`.
- `preview_doc` — `PreviewDoc`, a preview built in a worker thread: styled
  text (spans of `PreviewStyle`, inline images) or HTML / Markdown source for
  the existing renderers; summary card helpers (`preview_doc_title()`,
  `preview_doc_field()`, `preview_doc_section()`) and text tables
  (`PreviewTable`: aligned columns, numeric columns right-aligned, zebra rows).
- `preview_formats` — registry of structured previews, one builder per
  `FileKind` (`preview_format_build()` from a `PreviewInput`), the hex dump
  (`preview_format_hex_dump()`), and `preview_format_build_file()` for tests
  and tools. A new format = a parser module in this layer + one case in
  `builder_for()` + fixtures (see previews.md, "Adding a format").
- `user_guide` — the user guide built into the binary: `docs/user-guide/*.md`
  and the reference pages it links to, compiled as a GResource by the Makefile
  (generated list, prefix `/org/lookin/guide/`); `user_guide_read()` by page name
  (`"user-guide/editor.md"`, `"keyboard.md"`), `user_guide_title()`,
  `user_guide_pages()`, `user_guide_resolve_link()` (page, anchor, web or outside),
  `user_guide_anchor()` (GitHub heading ids). Its test checks every link of the guide.
- `org_time` — Org timestamps (`<2026-10-29 Thu 10:00 +1w>`): parse, format,
  field arithmetic, repeaters, the date prompt (`org_timestamp_read()`).
- `org_doc` — Org document commands on an array of lines + cursor (`OrgPos`):
  headings, subtrees, TODO keywords (`#+TODO`), priorities, tags, planning,
  clock, lists, checkboxes, statistics cookies, links, `<s` templates. The
  editor (`editor_org`) and the preview (`orgmode`) both edit through it.
- `org_table` — Org tables: align, cell navigation, row / column editing.
- `line_diff` — Myers line diff of a text against its previous version,
  reduced to per-line gutter marks (`LineDiffMark`: added, modified, deleted).
- `pane_geometry` — `pane_find_neighbor()`: the pane next to another one in a
  direction (same row or column only), used to move editor tabs between
  notebooks; `pane_drop_zone()` / `pane_zone_rect()`: the side of a pane a
  dragged tab is dropped on, and the area it would take.
- `minimap_geometry` — minimap scale, scroll offset and slider (VS Code
  style: fixed minimum scale, the minimap scrolls with the view over long
  documents), slider drag and click-to-center mapping.
- `dir_summary` — one directory level for the directory preview: entries
  (name, type, size, date), totals, sorting, relative dates, column padding.
- `preview_cache` — bounded most-recently-used cache of previews built,
  keyed by file and settings, with the file's stamp (size, mtime, inode).
- `path_kind` — whether a path is a folder, a file or missing, read in a
  worker thread (one check per path at a time) and kept, so a slow or hung
  mount never blocks the UI.
- `nav_keys` — navigation keys of the explorer and viewer in each global mode, strictly
  (Notepad, Vim, Emacs): `nav_keys_lookup()` maps a key and the pending
  prefix (`gg`, `zz`, `C-x o`, `Ctrl+W w`) to a `NavAction` (also Vim `t` /
  Emacs `C-o`: the file in the dedicated editor panel, `editor_open_dedicated()`;
  next / previous tab: `gt` / `gT`, `C-x <right>` / `<left>` with
  `nav_keys_ctrl_x()`, `Ctrl+Tab`, `Ctrl+PgDn` / `Ctrl+PgUp`, also used by
  the editor modes), `nav_keys_search_lookup()` the keys of the search bar. Pure, tested in
  `tests/test_nav_keys.c`.
- `vim_keys`, `vim_parse`, `vim_motion`, `vim_regex`, `vim_ex` — Vim pieces:
  key codes and `<notation>` (keys are Unicode code points, special keys in
  the private use area), the `["x][count]op[count]motion` grammar
  (`vim_parse()`), motions and text objects on `GtkTextIter`, Vim patterns
  translated to GRegex plus `:s` replacements and buffer search, Ex command
  line parsing (ranges, abbreviations).
- `vim_engine` (+ `vim_engine_ex.c`, internal `vim_engine_private.h`) — the
  Vim editor on a `GtkTextBuffer`: modes, operators, registers, marks, dot
  repeat, macros, Visual block, its own undo list (one step per command,
  GTK's history merges adjacent edits), search and Ex commands. No widgets:
  the host (`editor_vim`) implements `VimHost` (messages, command line
  entry, scroll, clipboard, `:w` / `:q`). In Insert mode text keys are left
  to the text view and recorded from the buffer signals. Tested in
  `tests/test_vim_engine.c` by typing keys on a headless buffer.
- `code_symbol`, `code_scan_c`, `code_scan`, `code_index` — the built-in
  code index of code navigation (code-nav.md): languages and the
  identifier under the cursor (`code_word_at()`), symbol scanners
  (`code_scan_text()`: a C-family state machine, line regexes for the other
  languages; no parser, locals not listed), the project's files (`git
  ls-files`, else a bounded walk) and the ranked definition lookup
  (`code_index_find()`, blocking: worker thread only).
- `code_tags` — ctags backend: finds Universal Ctags (`code_tags_program()`),
  parses its JSON output, and keeps a per-project index (`CodeTagsIndex`,
  with a lock) that re-tags only changed files and is cached in
  `app_cache_dir()/tags/`; `code_index_find()` uses it through
  `CodeQuery.tags`, open buffers are still scanned.
- `web_address` — what is typed in the address bar of a browser tab turned
  into a URI (`web_address_to_uri()`: URIs as is, paths as file URIs, hosts
  with https, or http for localhost, other words searched), and
  `web_address_is_web()` (http / https: the bookmarks a browser tab opens).
- `fuzzy_match` — fuzzy scoring of paths for quick open (file name, word
  starts and consecutive characters first) and the matched positions.
- `path_globs` — comma-separated path patterns matched like `.gitignore`
  (name at any depth, anchored, folders only), for the files to include /
  exclude of a search in files.
- `text_search` — search of a text in a project's files (literal, match
  case, whole word, regex through `GRegex`, or letters in order; binary and
  large files skipped, its own higher limits `TEXT_SEARCH_MAX_*`), as
  `CodeReference`s with their length, and why it stopped
  (`TextSearchStats`).
- `jump_list` — back / forward history of navigation jumps across files
  (also the explorer's folder history, `nav_history`).
- `session_store` — `session.ini` (workspaces at quit, with their panels
  as a `SavedLayout` and their editor tabs as `tab_tree` text) and
  `recent.ini` (recent folders and files, most recent first, `RECENT_MAX`),
  and the saved workspaces of the workspace bookmarks (`sessions/<id>.ini`,
  target `lookinfiles-session://<id>`: `session_target_make()` /
  `session_target_id()` accept lowercase hex ids only, `session_snapshot_path()`).
- `tab_tree` — the editor tabs of a workspace as text (`TabTreeNode`): file
  indices per notebook (`t` for a terminal tab), current page, view last
  focused, and the splits with their share, e.g.
  `H:0.500([0*,2],V:0.400([1!],[0,t]))`.
- `settings_store` — `settings.ini` (`Settings`: editor mode, line numbers
  and wrap of new editor tabs, column line of the editors, window size at
  startup), defaults for
  missing or invalid values, other keys kept on save.
- `trash_restore` — puts back a file moved to the trash: finds its newest
  `.trashinfo` (home trash, then the mount's `.Trash-$uid` / `.Trash/$uid`)
  and moves it back.
- `note_types` — note types of the notes view (`NoteType`: Plain Text,
  Markdown, Org-mode): extension, display name, icon, row badge and the
  name stored in `notes.xml` (the former `rich` reads as Markdown).
- `canvas_doc` — Canvas notes (`<title>.canvas/`: `page.xml` and
  `media/`): text boxes with styled runs and pictures placed freely, read and
  written as XML, new picture names, unused pictures pruned, Markdown export
  (`canvas_doc_export_markdown()`: a `.md` note next to it, pictures linked
  inside the Canvas).
- `palette` — every colour of the application, by role (`PaletteRole`:
  surfaces, text, accent, git, diff, syntax, editor, terminal ANSI...):
  `palette_color()` ("#rrggbb" for text tags and Pango markup),
  `palette_rgb()` (cairo), `palette_set()` / `palette_reset()` (themes),
  each role a CSS named colour (`@bg_window`, `alpha(@accent, 0.25)`), and
  `palette_expand()` for `@role` tokens in text templates.
- `theme_store` — themes: built-in ones (Dark Modern = the defaults,
  Light Modern, High Contrast Dark, then the `themes/*.ini` files bundled as
  GResources by the Makefile; a test keeps their main text pairs at a
  readable contrast) and user INI files in
  `app_config_dir()/themes/` (`[Theme]` name / dark / inherits, `[Colors]`
  palette roles, `[Fonts]`), strict parsing with errors naming group and
  key, inheritance (`theme_store_apply()` sets the palette and fonts), save
  of the current look, the chosen theme in `theme.ini`. `theme_apply_saved()`
  (`theme`) applies it at startup, before any widget.
- `text_tags` — text tags that follow the theme: `text_tag_create()` /
  `text_tag_ensure()` take roles as values (`"@accent"`, `"@font_code"`),
  remembered per tag; `text_tags_refresh_all()` sets them again in every
  buffer after a theme change.
- `fonts` — the interface and code fonts (family list + pixel size,
  `fonts_set()` for themes); CSS tokens `@font_ui`, `@font_code`,
  `@size_small` / `@size_bar` / `@size_normal` / `@size_title` (derived
  from the UI size), `@size_code`; `fonts_family()` for text tags and
  markup; `FONT_SCALE_*` for headings, titles, captions and code in
  rendered documents.
- `lsp_protocol`, `lsp_client`, `lsp_config` — Language Server Protocol:
  messages and results (positions: UTF-16 unless the server chose UTF-8,
  `lsp_column_to_byte()`), one server process spoken to asynchronously on the
  main context (`LspClient`: queued until initialized, server requests
  answered, full-text document sync; never free it from its own callbacks),
  and server commands (defaults + `lsp.ini`). `tests/fixtures/fake_lsp.py`
  is a fake server for the client tests.
- `explorer_row` — `ExplorerRow`, one row of the explorer tree (type, name,
  label, path, badge, children `GListStore`). Read fields directly, change them
  with the setters (they emit `"changed"` so the row redraws). A row that can
  expand must be added with `explorer_row_append_container()`: GTK decides
  once, when the row is inserted, whether it shows an expander.

**2. Shared types, events & leaf helpers — may use `AppData` and layer 1 only:**
- `app_types.h` — `AppData`, `EditorTab`, `VirtualLayout`, `FileEntry`,
  `SORT_*`, `SIDEBAR_VIEW_*`, `PREVIEW_*` constants.
- `explorer_tree` — the explorer widget (`app->tree`): root row, selection,
  expansion, scrolling and row drawing (git colours, badges). Signals
  `"selection-changed"`, `"row-toggled"` (expanded / collapsed, by the user or
  `explorer_tree_set_expanded()`), `"row-activated"` (double click). Rows are
  addressed by `ExplorerRow*`; positions count visible rows only.
  `explorer_tree_swap_state()` puts the content aside and shows another one.
- `icon_picker` — a grid of toggle buttons to choose one theme glyph
  (notebook and bookmark folder dialogs).
- `dialog` — `AppDialog`, the modal dialog drawn inside the main window: a
  card over a dimmed layer (window-level `GtkOverlay` made in `main.c` with
  `app_dialog_window_overlay_new()`). GtkDialog-like API: `app_dialog_new()`,
  `"response"`, `app_dialog_run()`, `app_dialog_present()`. Escape answers
  `APP_RESPONSE_DELETE_EVENT`; the caller removes it with
  `app_dialog_destroy()`. While one is open, window shortcuts are off
  (`app_dialog_is_open()`).
- `app_events` — event bus (`app_emit()`), see "Notifying the chrome" below;
  `app_notice()` shows a short message in the explorer bar (with Undo when asked).
- `panels` — fullscreen, focused-panel highlight, workspace width, split-handle
  drag feedback and fractions (`wire_paned_controllers()`). Every new
  `GtkPaned` must go through `wire_paned_controllers()` or at least
  `paned_use_thin_handle()`: a default handle grabs the mouse ~15 px into the
  neighbouring panels and blocks text selection near the edge.
- `focus_route` — where the keyboard focus is (`focus_route_get()`:
  explorer, preview, list page, editor tab; a focused tab bar counts as its
  current page), so the window key handler routes keys like the panels do.
  Tab switching for every mode (`focus_route_step_tab()`: skips hidden pages,
  puts the focus in the new page with `focus_route_focus_page()`); a click on
  a tab moves the focus from the notebook into the page
  (`focus_route_focus_changed()`, which also drops the explorer / viewer
  prefix key). Widgets taking the global mode keys are marked with
  `focus_route_mark_nav()`, the one focused in a page without them (a web
  page) with `focus_route_mark_focus()`; `focus_route_attach_list()` gives a list page
  (Git Commits, References) those keys.
- `text_style` — Dark+ GtkSourceView scheme, monospace, panel background,
  syntax language (`is_code_file()`, `apply_viewer_language()`).
- `theme` — global stylesheet (`apply_panel_borders()`); every module's CSS
  goes through `theme_add_css()` (palette colours defined, reloaded by
  `theme_reload()` after a palette or font change).
- `tree_nav` — explorer row traversal and selection (j/k, arrows, gg/G,
  Ctrl+F/B, zz/zt/zb, `tree_nav_selected()`, `select_row()`,
  `select_by_path()`), and suspending the preview handler while the tree is
  rebuilt (`tree_nav_block_selection_handler()`).
- `remote_store` — `RemoteMachine` list and `RemoteFolder`s, `remotes.xml`, `~/.ssh/config`
  discovery, lookups (unit-tested).
- `preview_summary` — text summaries shown in the preview for rows without
  file content (remote machine, notebook, bookmark folder, trash); columns
  padded for a monospace font (unit-tested).
- `fs_watch` — `GFileMonitor`s on expanded directories, the previewed file and
  `.git`, debounced `schedule_refresh()`; reports through events only.
- `diff_gutter` — left gutter of a text view: line numbers
  (`diff_gutter_set_line_numbers()`: code previews, editor toggle `Ctrl+Alt+N`)
  and git change bars
  (`diff_gutter_attach()` on the preview and editor views,
  `diff_gutter_set_file()`, `diff_gutter_get_marks()` for the minimap,
  which is redrawn through `diff_gutter_add_companion()`); HEAD version read with `git_read_head_blob()`
  in a worker thread, refreshed on `APP_EVENT_GIT_STATUS_CHANGED` /
  `APP_EVENT_FILES_CHANGED`.
- `color_column` — vertical line at a column of a text view (Vim's
  `colorcolumn`), drawn over the view in an overlay (`color_column_wrap()`
  around the editor's and the preview's scrolled windows, `color_column_set()`:
  Preferences, `:set colorcolumn`; in the preview for code only, hidden by
  any buffer change: `color_column_hide_on_change()`), colour `editor_ruler`.
- `tab_drop` — editor and terminal tabs dragged with the mouse (GtkNotebook
  tab DnD): drop target on a notebook (tab bar: insert; page: move in; edge:
  split), translucent highlight on the window overlay, end-of-drag report
  (`TabDragDoneFunc`, the source notebook's own) once GTK no longer uses the
  source notebook — remove an emptied notebook only then. Same group by
  default, `tab_drop_set_accept()` to take other tabs. Knows nothing of
  `EditorTab`; `editor` attaches it with `editor_notebook_enable_tab_drop()`,
  `terminal` to its own notebooks.
- `markdown`, `html` — rich renderers into a `GtkTextBuffer`.
  `markdown_table` — Markdown tables for `markdown`: rows split into cells, a
  grid widget anchored in the view, or a box-drawn text table without a view.
  `markdown_link` — links that can be followed, off by default (previews keep
  them as text): `markdown_link_attach()` on a view makes the renderer record the
  links of its buffer and calls a handler on click (table cells: `<a href>` labels).
- `preview_doc_view` — shows a `PreviewDoc` in the preview text view (style
  tags in the Dark+ palette, images, HTML / Markdown renderers).
- `minimap` — scaled file overview; draws only the part around what is shown
  (`MinimapCache`), geometry from `minimap_geometry`. `minimap_new_for_view()`
  makes a standalone one for any text view (the Help tab).

**3. Features:**
- `file_list` — async directory scan (`GTask`), tree population, sorting
  (`compare_entries_with_data()`), lazy expand/collapse, git badges in the tree
  (a second `GTask`, run once the listing is shown), navigation (`reload_directory()`, `navigate_to_path()`, `refresh_listing()`,
  `activate_selected_entry()`, `go_to_parent_directory()`), sidebar view toggles.
- `archive_tree` — archives as folders in the explorer: a file row of a
  browsable archive has children; Right / Enter list it in a worker thread
  into `arc_dir` / `arc_file` rows (path `<archive>!/<entry>`); selecting an
  `arc_file` extracts it to the cache (`archive_extract`) and previews that
  copy. Read-only.
- `explorer_keys` — explorer key handler (`on_tree_key_press()`): the
  navigation keys of the global mode (`nav_keys`, `app_nav_style()`), then
  the view actions, the same in every mode.
- `file_actions` — new file / new folder, rename and trash prompts.
- `files_menu` — right-click menu of the files view (the key actions,
  plus Copy Path, Open With Default App through `GtkFileLauncher`, Open in
  Browser Tab for HTML files through `browser_open()`, and Open Terminal
  Here through `terminal_open()`).
- `file_moves` — renames and moves to the trash with editor tabs and
  expansion following, remembered for one undo step (`u`, explorer bar Undo
  after the `app_notice()` message).
- `bookmarks`, `notes`, `remote` — the bookmarks, notes and SSH remote views
  (XML persistence: `bookmarks.xml`, `notes.xml`; remotes via `remote_store`).
- `canvas_view` — the **Canvas** page of the preview notebook (`app->canvas_page`):
  edits one Canvas note at a time (`canvas_doc`), text boxes and pictures on a
  `GtkFixed`; move / resize by dragging, pictures pasted (`Ctrl+V`) or dropped
  into `media/`; saved automatically, on close and at quit (never while the page
  is being destroyed). Not an editor tab: `app->editors` stays text only.
- `bookmark_dialog` — the add / edit dialogs of the bookmarks view (name,
  location or folder icon, parent folder) and the target helpers they share
  with `bookmarks` (`bookmark_display_location()`, `bookmark_default_name()`).
- `bookmark_icons` — bookmark link icons from the target's kind
  (`path_kind`, never read on the UI thread); relabels the rows and previews
  the selected link again once the kind is known.
- `viewer` — right-pane preview dispatch (`on_selection_changed()`,
  `load_file_content()`, `viewer_reload_preview()`), image/text/dir previews,
  viewer keys. Its worker detects the kind, reads text kinds, and runs the
  `preview_formats` builder (falling back to plain text or the hex dump);
  never add a format-specific branch to the viewer. Its results are kept in a
  `preview_cache`: a file selected again is shown at once, and the worker
  only checks the file's stamp (it reads and shows the file again if changed).
- `dir_preview` — directory preview (`dir_preview_show()`): one level read
  in a worker thread after a short delay (never recursive), header and table.
  A folder seen before is shown at once from a `preview_cache`, and drawn
  again only if the worker reads something different.
- `orgmode`, `pdf`, `video`, `audio`, `git_tab` — preview renderers / tabs.
  `orgmode` renders and edits Org files on disk; it knows nothing of editor tabs.
- `editor` — editor tabs in the notebook(s): open, save, close, move, split
  view (`editor_split_tab()`: a second tab on the same buffer), dedicated panel.
- `editor_session` — the editor tabs of a workspace for the session:
  `editor_session_capture()` describes the preview notebook and the
  dedicated panel (notebooks, splits, sizes, current pages, view last
  focused) as `tab_tree` text, `editor_session_restore()` reopens the files
  at the same place (`editor_notebook_split()`, `editor_move_tab_to()`,
  `editor_add_view()` for split views) and starts a new shell for each
  terminal tab there (`terminal_add_tab()`).
- `editor_layout` — the shape of the dedicated panel for saved layouts:
  `editor_layout_capture()` (splits, sizes, panes of files, terminal tabs),
  `editor_layout_apply()` rebuilds the splits and places the open file tabs
  and running terminals in them (`terminal_move_tab()`; missing shells
  started, extra ones closed after the caller's confirmation, or kept in a
  spare list for previews); `editor_layout_snapshot()` / `_restore()` put
  back the exact tabs of the panel and the preview notebook.
- `editor_view` — the widgets of an editor tab around its buffer (view,
  scroll, minimap, mode lines, view controllers); `editor_view_release()`
  disconnects a closing tab from a buffer that another view still shows.
  The buffer has one cursor: the view in use has it (`editor_view_activate()`
  on focus or click), the others keep theirs in `view_insert` / `view_bound`
  (`view_passive`); `editor_find_tab()` returns the view in use. In Vim mode
  the views share one `VimEngine` (kept on the buffer by `editor_vim`),
  hosted by the focused view.
- `editor_info` — what the bar of an editor tab tells about its file
  (`EditorInfo`: git branch and relative path, read once per file until
  `editor_info_forget()`, language, code navigation backend, diagnostics).
- `editor_modes` — per-tab mode state, shared editing operations (align,
  format, case, indent, find/replace, goto line), tab label, status (the
  `editor_info` of the tab in each mode's style; `editor_modes_refresh_bars()`
  on `APP_EVENT_STATUS_CHANGED`, `editor_modes_refresh_git()` on
  `APP_EVENT_GIT_STATUS_CHANGED`), key dispatch.
  Mode implementations: `editor_notepad`, `editor_emacs`, `editor_vim` (the
  host of `vim_engine`: status line, command line, `VimHost` callbacks), and
  `editor_org` (Emacs Org-mode keys in tabs: maps keys to `org_doc` /
  `org_table` commands, applies only the changed lines as one undo step,
  folding with an invisible tag). `editor_modes_private.h` (state
  struct, mode enums) is internal to these 5 files.
  Mode bar labels whose text can grow (file names, messages) must use
  `editor_bar_label_shrinkable()`, and filler text `editor_bar_clip()`: a bar
  wider than the panel pushes the minimap out of the window.
- `code_nav` — code navigation in editor tabs, shared by the three modes:
  definition / declaration lookups in a worker (`code_nav_goto_definition()`,
  `code_nav_goto_symbol()`), the jump with `editor_open_at()`, back /
  forward (`code_nav_back()`, `code_nav_forward()`); messages through
  `editor_mode_message()` passed by the caller. Vim reaches it through the
  `VimHost` callbacks `goto_definition` and `jump_history`.
- `code_intel` — language servers for code navigation: one `LspClient` per
  project and command, open tabs synchronized before each request, results
  with byte columns; FALSE when no server serves the tab (callers then use
  the index). `code_help` — help cards (`GtkPopover` parented to the tab
  container, never the text view) and signature help, from `code_intel` or
  `code_nav_find()`; also highlights the other occurrences of the word at
  the cursor. Diagnostics (`publishDiagnostics`) are underlined by
  `code_intel` (tags `lsp-error`...) and shown in help cards.
- `code_refs` — the References page of the preview notebook (after the Git
  tab; `editor_detach_tabs()` and `editor_current_tab()` only touch editor
  pages, wherever they are), from the server or `code_index_references()`;
  also the Search page (`app->search_page`) of `project_search`, next to it.
- `project_search` — Ctrl+Shift+F: asks for the text, its options and the
  files to include / exclude (`path_globs`), runs `text_search` over
  `code_index_some_files()` in a worker (a new search cancels the
  previous one) and lists the results in the Search page of `code_refs`
  (`code_refs_show_search()`, same rows and keys as References).
  Per workspace (`VirtualLayout.search_state`): the last search, the
  running one and the Search page, taken out of the notebook on a switch
  (`code_refs_detach_search()` / `code_refs_attach_search()`); a search
  that ends in a hidden workspace fills its page (`code_refs_set_search()`).
- `code_symbols` — symbols of the file in an `AppDialog` with a filter.
- `nav_history` — per-workspace folder history (`VirtualLayout.dir_history`,
  fed by `reload_directory()`; `go_back_directory()` / `go_forward_directory()`
  / `go_history_directory()` in `file_list` walk it), with the entry
  selected in each folder left (selected again on the way back), and the
  recent folders and files (editor tabs opened, quick open choices).
- `quick_open` — Ctrl+P / Emacs `C-x C-f`; Alt+R lists the recent places
  in the same dialog (`quick_open_show_recent()`). Ctrl+P: the project's files
  (`code_index_all_files()` in a worker) in an `AppDialog`, ranked by
  `fuzzy_match`; shows the file with `reveal_path_in_tree()` or edits it.
- `terminal` — embedded VTE terminals as tabs of terminal notebooks (group
  `TERM_TABS_GROUP`) split by `GtkPaned`s; tabs are dragged between them or
  to a pane edge through `tab_drop` (`Ctrl+T` new tab, `Ctrl+Shift+T` new
  pane below, `terminal_open()` with a working directory); ssh into the
  active remote machine. Terminal tabs also go into the editor and preview
  notebooks (`tab_drop_set_accept()` on both sides); `editor_remove_page()`
  takes them out when the shell exits, and the preview notebook's ones stay
  with their workspace (`terminal_workspace_hidden()` / `_shown()`).
  `term_snapshot_take()` / `_restore()` put back the exact panes of the
  panel after a layout preview; `terminal_detach_tab()` /
  `terminal_attach_tab()` keep a terminal running out of any notebook.
- `browser` — browser tabs in the preview notebook (WebKitGTK 6.0, optional:
  `HAVE_WEBKIT`; without it `browser_open()` hands the URI to the default
  browser): back / forward / reload, address bar (`web_address`), the
  `WebKitWebView`. Opened by `Ctrl+Shift+B` (`ui`), the Help tab's web links,
  web bookmarks and the files menu (HTML files). Everything stays in the
  window: a new window (`create`) is a new tab after its opener, `close`
  closes the tab; print, file chooser and fullscreen are refused,
  permissions denied; links to other schemes and files the page cannot show
  go to the default application (`GtkUriLauncher`). Per workspace
  (`VirtualLayout.browser_pages`): taken out of the notebook on a switch
  (`browser_workspace_hidden()` / `_shown()`). Not saved in the session.
- `search` — the search bars of the explorer and of the preview, one at the
  bottom of each panel (`AppSearchBar`, `search_bar_new()`), opened and
  driven by the keys of the global mode (`/` `n` `N`, `Ctrl+F` `F3`, `C-s`
  `C-r` `C-g`) and drawn like an editor tab's search in that mode (Notepad
  find bar, Emacs I-search minibuffer, Vim `/` command line: the mode bar
  stylesheet of `editor_modes_load_css()`).

**4. Window chrome — may call features:**
- `topbar` — back / forward arrows (folder history, right click: the list),
  breadcrumbs, global mode pill (`topbar_mode_button_new()`: it shows and sets
  the global mode only; a tab's mode is set from its tab menu) and toggle
  pills; `update_path_label()`.
- `status_bar` — panel status bars (no window-wide bar: editor tabs have
  their own, `editor_modes`): explorer bar (`create_explorer_bar()`: branch of the files
  view, `app_notice()` message with Undo; the message moves to the top bar,
  `create_topbar_notice()`, while the explorer is hidden), preview bar
  (`create_preview_bar()`, under the preview page only: name, size, type,
  encoding); `update_status_bar()` refreshes both.
- `sidebar` — explorer header: view switcher and per-view action toolbars.
- `help` — cheat sheet (`?`, right click on the Help button); `KEY_HELP` must stay in sync with `docs/keyboard.md`.
- `help_guide` — the **Help** page of the preview notebook (`app->help_page`, after
  the Git, result and Canvas pages): a `user_guide` page rendered by `markdown`,
  scrolled with the navigation keys of the global mode; links followed through
  `markdown_link` (`user_guide_resolve_link()`: guide pages and anchors, web pages
  in a browser tab, `browser_open()`), Tab / Enter, back / forward history (Shift+H / Shift+L,
  mouse buttons), a minimap (`minimap_new_for_view()`). F1 (`ui`) and a click on the Help button (`topbar`) open it,
  its close button, a middle click or Ctrl+W close it.
  Per workspace (`VirtualLayout.help_page`): the page, with its history and
  scroll position, is taken out of the notebook on a switch and put back
  when its workspace returns (`help_guide_workspace_hidden()` / `_shown()`).
- `workspace` — virtual workspaces (Alt+1..8): save/apply layout snapshots,
  top-bar workspace minimap. A new workspace (Alt++) starts from the default
  view (start directory, files view) with the panels of the default layout
  (`layout_store_load_default()`), never from a copy of the current one. The
  first workspace gets the same panels at startup
  (`workspace_apply_default_layout()`, only when the default is customized).
  Each workspace keeps its own explorer rows (`ExplorerTreeState`: rows,
  expansion, selection, scroll) and git badges, swapped in on switch;
  `check_listing()` then re-reads the directory in the background and
  rebuilds the rows only if it changed.
- `themes` — themes in the running application: `themes_apply()` (a
  theme from `theme_store` with the user's fonts of `theme.ini` over it,
  remembered or previewed), `themes_refresh()` after a palette or font
  change (stylesheets, text tags, syntax scheme, terminals, minimap caches,
  redraw, explorer and preview rebuilt through events), zoom
  (`Ctrl+=` / `Ctrl+-` / `Ctrl+0`: both font sizes, `fonts_zoom_percent()`
  in the Notepad status bar), the Appearance page of the Preferences
  (`ThemesPage`: live preview on selection, Cancel restores, fonts,
  Save As, Edit in a tab, Delete; its buttons report dialog responses
  handled by `themes_page_handle()`), and re-applying the current theme file when it is saved
  (`APP_EVENT_FILES_CHANGED`).
- `session` — saves the workspaces at quit (`session_save_now()`: Ctrl+Q
  in `ui`, window close in `main.c`) and restores them at a launch without
  argument (`session_restore()`: `workspace_add()` +
  `workspace_restore_state()` per workspace, then its editor tabs at their
  place (`editor_session`), then its panels, split sizes and terminal panes,
  with new shells). `session_save_workspace()` / `session_open_workspace()`
  do the same for one workspace in a file (workspace bookmarks).
- `workspace_menu` — the right-click menu of a box of the workspace map
  (Save as Bookmark…, Close Workspace); `workspace` passes `workspace_remove()`
  to it, so the two modules do not include each other.
- `session_bookmark` — workspace bookmarks: the bookmarks view and the
  workspace map emit `APP_EVENT_SAVE_WORKSPACE_BOOKMARK` for workspace
  `app->session_save_ws` (asks a name, saves it with `session_save_workspace()`, adds the link with
  `bookmarks_add_session()`) and, on a link whose target is a saved workspace,
  `APP_EVENT_OPEN_WORKSPACE_BOOKMARK` with `app->session_open_target`
  (`session_open_workspace()` into a new workspace, or a message saying why not).
  Deleting such a link deletes its file (`bookmarks`).
- `preferences` — the Preferences dialog (`Ctrl+,`, top-bar gear button;
  `Alt+T` opens Appearance): a `GtkStack` with a `GtkStackSidebar` in an
  `AppDialog` (Appearance = `ThemesPage`, Editor, Window, Code navigation),
  Apply saves `settings.ini` and applies the editor mode at once, Cancel
  restores the theme. Reads `settings.ini` at startup for `main.c`
  (`preferences_editor_mode()`, `preferences_apply_startup()`).
- `layout_switcher` — the Alt+L layout list (top-bar grid button): a
  `GtkPopover` listing the current arrangement, the default and the saved
  layouts; the hovered or selected row is shown in full after a short delay,
  Enter or a click applies it, Escape restores the start; **Edit Layouts…** opens
  the `layouts` dialog. The previews are `layouts_preview_begin()` / `_show()` /
  `_end()` (in `layouts`): the start is an `EditorSnapshot`
  (`editor_layout`) and a `TermSnapshot` (`terminal`) holding the very tabs
  and terminals with their panes, places and current pages; each preview
  restores the start, then arranges the layout with spares
  (`editor_layout_apply_spare()`, `term_layout_apply_spare()`): extra
  terminals are set aside instead of closed, two views of a file may share a
  pane, and the shells started for previews end with them. Applying asks
  over the preview, restores the start, then runs the normal apply.
- `layouts` — saved workspace layouts (Edit Layouts dialog): capture
  the current workspace's panels and editor splits (`editor_layout`), apply
  one from a list in an `AppDialog` (running terminals are kept; closing extra
  ones, in the terminal panel or the editor panel, asks first). The list starts
  with the default layout of new workspaces, which can be replaced (save
  under the name "Default") or reset. A click selects a row; double click,
  Enter or Apply applies it.
- `ui` — focus tracking and cycling, global key handler
  (`on_window_key_press()`, incl. `Ctrl+Q`), layout toggle.

**5. Entry point:** `main.c` — CLI (`-f`/`--fullscreen`, `-s`/`--size WxH`,
`-V`/`--version`, start directory), widget creation and assembly, signal wiring, event
subscriptions, `GMainLoop`.

### Notifying the chrome (app_events)

Lower layers never call the chrome or the workspace directly. They emit:

| Emit (`app_emit(app, ...)`)      | When                                         | Subscriber (main.c)            |
|----------------------------------|----------------------------------------------|--------------------------------|
| `APP_EVENT_CHROME_CHANGED`       | dir, sort, hidden, panels, view, remote changed | `update_path_label()`       |
| `APP_EVENT_STATUS_CHANGED`       | selection / file info / editor mode changed  | `update_status_bar()`, `editor_modes_refresh_bars()` |
| `APP_EVENT_LAYOUT_CHANGED`       | panel visibility or split fractions changed  | `workspace_save_current()`     |
| `APP_EVENT_LAYOUT_REBUILT`       | panes shown, hidden or rebuilt               | `workspace_apply_positions()`  |
| `APP_EVENT_FILES_CHANGED`        | files created/renamed/saved                  | `refresh_listing()`            |
| `APP_EVENT_PREVIEW_STALE`        | previewed file changed                       | `viewer_reload_preview()`      |
| `APP_EVENT_GIT_STATUS_CHANGED`   | `.git` changed                               | `refresh_git_status_in_place()`, `refresh_diff_gutters()`, `editor_modes_refresh_git()` |
| `APP_EVENT_SAVE_WORKSPACE_BOOKMARK` / `APP_EVENT_OPEN_WORKSPACE_BOOKMARK` | bookmarks view: save the workspace / open a saved one | `session_bookmark_save()` / `session_bookmark_open()` |
| `APP_EVENT_FOCUS_NEXT_PANEL` / `_PREV_PANEL` | `C-x o`, `Ctrl+W w` / `W`, `F6` / `Shift+F6` | `focus_next_panel()` / `focus_prev_panel()` |

Handlers run synchronously inside `app_emit()`. `make check` rejects direct
calls to the first five subscribers outside their module and `main.c`. A new
upward notification = a new `AppEvent` + one `app_events_subscribe()` in `main.c`.

### Known coupling (avoid making it worse)

`make check` fails on any include cycle (two `.c` files including each other's
header), except the two accepted ones: `file_list` ↔ `bookmarks` and
`file_list` ↔ `remote`. The explorer dispatches keys and rows to these sidebar
views, and the views ask the explorer to navigate (`navigate_to_path()`,
`reload_directory()`). Breaking a new cycle: emit an event, or move the shared
code down a layer (as done for `panels`, `fs_watch`, `remote_store`, `editor_org`).

Shortcut list to keep in sync with `docs/keyboard.md` and `help.c` when you change behavior.

