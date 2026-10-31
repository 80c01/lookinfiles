# Architecture

Module reference for Lookin. The short rules are in [AGENTS.md](../AGENTS.md).

## Architecture (keep it when editing)

Modules live in `src/<module>.c` + `src/<module>.h`. Layers, from bottom to top
(a module may only depend on modules in its own layer or below):

**1. Foundation — pure helpers, no `AppData`, no widgets, unit-tested in `tests/`:**
- `app_paths` — application name (`APP_NAME`), config and cache directories
  (`app_config_dir()`, `app_cache_dir()`: never hard-code the directory name).
- `file_types` — file type detection: by name (`file_extension()`,
  `is_*_file()`, `file_kind_from_name()`) and by content (`FileKind`,
  `file_kind_detect()` on the first `FILE_SNIFF_BYTES`: magic numbers, text vs
  binary; `file_kind_of_path()`), and emoji icons (`file_icon()`). The preview
  detects the kind in its worker thread (`app->viewed_kind`): decide on the
  displayed file with `viewed_kind`, not with its name.
- `git_utils` — `find_git_root()`, `git_resolve_dir()` (worktrees/submodules),
  `git_read_branch()`, `scan_git_statuses()`, `git_read_head_blob()` (spawns git: worker thread only).
- `layout_store` — saved workspace layouts (`SavedLayout`: panel visibility,
  split fractions, orientation, dedicated editor panel, terminal pane tree as
  text such as `H:0.500(T,V:0.400(T,T))`), `layouts.ini` load/save, and the
  default layout of new workspaces (group `[Default]`, built-in if absent).
- `gif_decoder` — GIF frames composed on an RGBA canvas (LZW, interlacing,
  transparency, disposal, delays, loop count), played by `animation`
  (GdkPixbufAnimation is deprecated in gdk-pixbuf 2.44: never use it).
- `zip_reader` — read-only ZIP archives (`zip_reader_open()`,
  `zip_reader_read()` with a size limit; stored / deflated via GIO's raw
  `GZlibDecompressor`, ZIP64, no encryption).
- `ole_reader`, `ole_props`, `office97` — OLE2 compound files (Office
  97-2003, `.msg`...): sectors, FAT / mini FAT and directory read from a
  mapped file with loop and range checks (`tests/ole_builder.h` builds them
  in tests), the property sets (`OleProperties`: card and HTML head), and the
  preview dispatch by stream names with the property card + stream list
  fallback. `doc_reader` — Word `.doc` to HTML (piece table, PAPX / CHPX
  FKPs, styles), like `docx`. `xls_reader` — BIFF8 / BIFF5 workbooks to
  the `Workbook` of `spreadsheet` (shown by `spreadsheet_append_sheets()`,
  shared with `.xlsx`). `ppt_reader` — `.ppt` records to HTML slide by
  slide (SlideListWithText, else the slide containers; masters skipped).
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
- `docx` — Word document (`.docx`) to simple HTML for the preview
  (`docx_file_to_html()`): styles, numbering, tables, images as `data:` URIs,
  text limit. Rendered by `html`; tests build archives with `tests/zip_builder.h`.
- `preview_doc` — `PreviewDoc`, a preview built in a worker thread: styled
  text (spans of `PreviewStyle`, inline images) or HTML / Markdown source for
  the existing renderers; summary card helpers (`preview_doc_title()`,
  `preview_doc_field()`, `preview_doc_section()`) and text tables
  (`PreviewTable`: aligned columns, numeric columns right-aligned, zebra rows).
- `preview_formats` — registry of structured previews, one builder per
  `FileKind` (`preview_format_build()` from a `PreviewInput`), the hex dump
  (`preview_format_hex_dump()`), and `preview_format_build_file()` for tests
  and tools. A new format = a parser module in this layer + one case in
  `builder_for()` + fixtures (see TASK.md, "Definition of done").
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
  prefix (`gg`, `zz`, `C-x o`, `Ctrl+W w`) to a `NavAction`,
  `nav_keys_search_lookup()` the keys of the search bar. Pure, tested in
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
  code index of code navigation (TASK_CODE_NAV.md): languages and the
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
- `jump_list` — back / forward history of navigation jumps across files.
- `palette` — every colour of the application, by role (`PaletteRole`:
  surfaces, text, accent, git, diff, syntax, editor, terminal ANSI...):
  `palette_color()` ("#rrggbb" for text tags and Pango markup),
  `palette_rgb()` (cairo), `palette_set()` / `palette_reset()` (themes),
  each role a CSS named colour (`@bg_window`, `alpha(@accent, 0.25)`), and
  `palette_expand()` for `@role` tokens in text templates.
- `theme_store` — themes: built-in ones (Dark Modern = the defaults,
  Light Modern, High Contrast Dark; a test keeps their main text pairs at a
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
- `dialog` — `AppDialog`, the modal dialog drawn inside the main window: a
  card over a dimmed layer (window-level `GtkOverlay` made in `main.c` with
  `app_dialog_window_overlay_new()`). GtkDialog-like API: `app_dialog_new()`,
  `"response"`, `app_dialog_run()`, `app_dialog_present()`. Escape answers
  `APP_RESPONSE_DELETE_EVENT`; the caller removes it with
  `app_dialog_destroy()`. While one is open, window shortcuts are off
  (`app_dialog_is_open()`).
- `app_events` — event bus (`app_emit()`), see "Notifying the chrome" below.
- `panels` — fullscreen, focused-panel highlight, workspace width, split-handle
  drag feedback and fractions (`wire_paned_controllers()`). Every new
  `GtkPaned` must go through `wire_paned_controllers()` or at least
  `paned_use_thin_handle()`: a default handle grabs the mouse ~15 px into the
  neighbouring panels and blocks text selection near the edge.
- `text_style` — Dark+ GtkSourceView scheme, monospace, panel background,
  syntax language (`is_code_file()`, `apply_viewer_language()`).
- `theme` — global stylesheet (`apply_panel_borders()`); every module's CSS
  goes through `theme_add_css()` (palette colours defined, reloaded by
  `theme_reload()` after a palette or font change).
- `tree_nav` — explorer row traversal and selection (j/k, arrows, gg/G,
  Ctrl+F/B, zz/zt/zb, `tree_nav_selected()`, `select_row()`,
  `select_by_path()`), and suspending the preview handler while the tree is
  rebuilt (`tree_nav_block_selection_handler()`).
- `remote_store` — `RemoteMachine` list, `remotes.xml`, `~/.ssh/config`
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
- `tab_drop` — editor tabs dragged with the mouse (GtkNotebook tab DnD):
  drop target on a notebook (tab bar: insert; page: move in; edge: split),
  translucent highlight on the window overlay, end-of-drag report
  (`TabDragDoneFunc`) once GTK no longer uses the source notebook — remove an
  emptied notebook only then. Knows nothing of `EditorTab`; `editor` attaches
  it with `editor_notebook_enable_tab_drop()`.
- `markdown`, `html` — rich renderers into a `GtkTextBuffer`.
- `preview_doc_view` — shows a `PreviewDoc` in the preview text view (style
  tags in the Dark+ palette, images, HTML / Markdown renderers).
- `minimap` — scaled file overview; draws only the part around what is shown
  (`MinimapCache`), geometry from `minimap_geometry`.

**3. Features:**
- `file_list` — async directory scan (`GTask`), tree population, sorting
  (`compare_entries_with_data()`), lazy expand/collapse, git badges in the tree
  (a second `GTask`, run once the listing is shown), navigation (`reload_directory()`, `navigate_to_path()`, `refresh_listing()`,
  `activate_selected_entry()`, `go_to_parent_directory()`), sidebar view toggles.
- `explorer_keys` — explorer key handler (`on_tree_key_press()`): the
  navigation keys of the global mode (`nav_keys`, `app_nav_style()`), then
  the view actions, the same in every mode.
- `file_actions` — new file / new folder prompts.
- `bookmarks`, `notes`, `remote` — the bookmarks, notes and SSH remote views
  (XML persistence: `bookmarks.xml`, `notes.xml`; remotes via `remote_store`).
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
- `editor` — editor tabs in the notebook(s): open, save, close, move, dedicated panel.
- `editor_modes` — per-tab mode state, shared editing operations (align,
  format, case, indent, find/replace, goto line), tab label, status, key dispatch.
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
  pages, wherever they are), from the server or `code_index_references()`.
- `code_symbols` — symbols of the file in an `AppDialog` with a filter.
- `terminal` — embedded VTE panes (`Ctrl+T`), Dark+ ANSI palette; ssh into
  the active remote machine.
- `search` — live search entry for list and viewer, opened and driven by the
  keys of the global mode (`/` `n` `N`, `Ctrl+F` `F3`, `C-s` `C-r` `C-g`).

**4. Window chrome — may call features:**
- `topbar` — breadcrumbs and toggle pills; `update_path_label()`.
- `status_bar` — bottom bar (branch, selection, size, type, editor mode menu).
- `sidebar` — explorer header: view switcher and per-view action toolbars.
- `help` — cheat sheet (`?`); `KEY_HELP` must stay in sync with `README.md`.
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
  in the Notepad status bar), the Themes dialog (`Alt+T`, top-bar button:
  live preview on selection, Cancel restores, fonts, Save As, Edit in a
  tab, Delete), and re-applying the current theme file when it is saved
  (`APP_EVENT_FILES_CHANGED`).
- `layouts` — saved workspace layouts (Alt+L, top-bar grid button): capture
  the current workspace's panels, apply one from a list in an `AppDialog`
  (running terminals are kept; closing extra ones asks first). The list starts
  with the default layout of new workspaces, which can be replaced (save
  under the name "Default") or reset. A click selects a row; double click,
  Enter or Apply applies it.
- `ui` — focus tracking and cycling, global key handler
  (`on_window_key_press()`, incl. `Ctrl+Q`), layout toggle.

**5. Entry point:** `main.c` — CLI (`-f`/`--fullscreen`, `-s`/`--size WxH`,
start directory), widget creation and assembly, signal wiring, event
subscriptions, `GMainLoop`.

### Notifying the chrome (app_events)

Lower layers never call the chrome or the workspace directly. They emit:

| Emit (`app_emit(app, ...)`)      | When                                         | Subscriber (main.c)            |
|----------------------------------|----------------------------------------------|--------------------------------|
| `APP_EVENT_CHROME_CHANGED`       | dir, sort, hidden, panels, view, remote changed | `update_path_label()`       |
| `APP_EVENT_STATUS_CHANGED`       | selection / file info / editor mode changed  | `update_status_bar()`          |
| `APP_EVENT_LAYOUT_CHANGED`       | panel visibility or split fractions changed  | `workspace_save_current()`     |
| `APP_EVENT_LAYOUT_REBUILT`       | panes shown, hidden or rebuilt               | `workspace_apply_positions()`  |
| `APP_EVENT_FILES_CHANGED`        | files created/renamed/saved                  | `refresh_listing()`            |
| `APP_EVENT_PREVIEW_STALE`        | previewed file changed                       | `viewer_reload_preview()`      |
| `APP_EVENT_GIT_STATUS_CHANGED`   | `.git` changed                               | `refresh_git_status_in_place()`|
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

Shortcut list to keep in sync with `README.md` and `help.c` when you change behavior.

