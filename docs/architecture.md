# Architecture

Module reference for LookinFiles. The short rules are in [AGENTS.md](../AGENTS.md).

## Architecture (keep it when editing)

Modules live in `src/<dir>/<module>.c` + `src/<dir>/<module>.h`; only
`main.c` is at the top of `src/`. Module names are unique across directories
(the objects are flat in `build/`), and a header is included by its path from
`src/`: `#include "canvas/canvas_doc.h"`, also from the same directory.
The directories group modules by feature, not by layer:

| Directory | Contents |
|---|---|
| `core/` | application paths, i18n, `AppData` types, events, file types, text encodings, path helpers, settings |
| `theme/` | palette, fonts, theme files, text tags and styles, icons and icon sets |
| `widgets/` | shared widgets: dialogs, panels, focus routing, minimap, gutters, tagbar pane, tab drop |
| `explorer/` | explorer tree and rows, files view and actions, bookmarks, remote machines, filesystem watch |
| `git/` | libgit2 helpers, git state, the Git tab |
| `preview/` | the viewer, preview state and cache, `preview_formats`, directory preview, PDF, browser tabs |
| `markup/` | HTML and Markdown renderers |
| `media/` | images, GIF animation, EXIF, audio and video players |
| `formats/` | parsers of the structured previews (archives, binaries, data and text formats) |
| `office/` | office documents: Word, Excel, PowerPoint, OpenDocument, OLE2, EPUB, RTF |
| `mail/` | mail messages, mailboxes and stores |
| `editor/` | editor tabs, editing modes (Notepad, Emacs, Vim, Org), saving |
| `vim/` | the Vim engine |
| `code/` | code navigation: index, scanners, ctags, language servers, outline, jumps |
| `search/` | search bars, search in files, Quick Open |
| `org/` | Org-mode documents |
| `notes/` | notes and notebooks |
| `canvas/` | Canvas notes |
| `games/` | games mode |
| `jupyter/` | Jupyter notebooks edited cell by cell: the notebook model and its tab |
| `terminal/` | terminal panes and the built-in terminal |
| `workspace/` | workspaces, layouts, session |
| `clock/` | the clock and the timers of the top bar |
| `sysmon/` | the system monitor: top bar CPU chart and the System Monitor tab |
| `chrome/` | top bar, status bar, sidebar, help, preferences, applying themes, `ui` |

Layers, from bottom to top
(a module may only depend on modules in its own layer or below):

**1. Foundation — pure helpers, no `AppData`, no widgets, unit-tested in `tests/`:**
- `app_paths` — application name (`APP_NAME`), config and cache directories
  (`app_config_dir()`, `app_cache_dir()`: never hard-code the directory name).
- `i18n` — translations: `i18n_init()` (first call of `main()`: locale and
  catalog directory, `i18n_locale_dir()`), and the gettext macros `_()`,
  `N_()`, `C_()`, `ngettext()` (docs/translations.md).
- `file_types` — file type detection: by name (`file_extension()`,
  `is_*_file()`, `file_kind_from_name()`) and by content (`FileKind`,
  `file_kind_detect()` on the first `FILE_SNIFF_BYTES`: magic numbers, text vs
  binary; `file_kind_of_path()`). The preview
  detects the kind in its worker thread (`preview_state_kind()`): decide on
  the displayed file with it, not with its name.
- `sysstat` — system statistics of Linux read from `/proc` (Linux only:
  elsewhere the readers find nothing): pure parsers of `stat`, `meminfo`,
  `loadavg`, `net/dev`, `diskstats`, `self/mounts` and `<pid>/stat`;
  `sysstat_sample()` reads a snapshot (processes and filesystems on demand,
  whole disks of `/sys/block` only) in a worker thread (`statvfs()` on a
  network mount can block), `sysstat_sample_rates()` turns two snapshots into
  CPU percentages (a process: 100 = one core), network and disk rates.
- `countdown` — the countdown of the top bar timers: wall-clock deadline
  (right across a suspend), pause, immediate restart on expiry (cycles
  counted), a stop at 0 or an overtime below 0, the text shown and when it
  changes, reverse colours in the last minute (and at or below 0) and
  blinking in the last ten seconds.
- `ui_probe` — test hook for `make ui-shots`: with `LOOKIN_UI_PROBE=<file>`,
  each SIGUSR2 writes a new number to the file once the main loop is idle,
  so a screenshot waits for the application instead of a fixed delay
  (not SIGUSR1: WebKit takes it for its garbage collector).
- `ipynb_doc` — a Jupyter notebook (nbformat 4) held for editing
  (`IpynbDoc`): an owned JSON tree whose members keep their order and their
  number text, the cells read and changed (type, source, outputs as
  `IpynbOutput`: text, errors, decoded images, `text/latex` formulas in math delimiters), inserted, taken out and put
  back, moved; `ipynb_doc_to_json()` writes it as Jupyter does
  (`json.dumps(indent=1, ensure_ascii=False)`, sources as lists of lines),
  so an untouched notebook is written back byte for byte. Read by the
  tolerant parser of `json_view` (`json_value_dup_key()`). For running
  cells: a key per cell that survives moves (`ipynb_doc_cell_key()` /
  `ipynb_doc_find_cell()`), outputs added as Jupyter stores them
  (`ipynb_doc_append_output()`: lines, stream pieces joined), execution
  count, kernel metadata (`ipynb_doc_set_kernel_info()`).
- `ipynb_outline` — the outline of a notebook for its tab's tagbar: the
  headings of its Markdown cells (`code_outline_markdown()`), each
  `CodeOutlineEntry` at the line of its cell's index (column: its line in
  the cell), so `code_outline_entry_at_line()` finds the current cell's.
- `ipynb_kernel` — a Jupyter kernel run through the bridge script
  `data/jupyter/ipynb_bridge.py` (bundled as a GResource, run as `python3
  -c <script>`): jupyter_client in a Python process, spoken to in JSON
  lines (commands on its stdin, events on its stdout; the kernel's own
  output goes to stderr). `GSubprocess` with asynchronous reads and a write
  queue, events as `IpynbKernelCallbacks` (state, started, failed, cell
  input / output / clear / done, input requests); the end of the bridge's
  stdin stops the kernel. `ipynb_kernel_list()` runs the bridge once with
  `--list` for the installed kernelspecs (`IpynbKernelSpec`). Tested with
  `tests/fixtures/fake_ipynb_bridge.py`.
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
  `git_read_branch()` (plain file reads); through libgit2, never the git
  program (worker thread only): `scan_git_statuses()`, `git_read_head_blob()`,
  `git_read_log()`, `git_show_commit()` (laid out like `git show --stat
  --patch`), `git_list_files()` (like `git ls-files --cached --others`).
- `git_state` — `GitState`, the result of the explorer's git scan: badge of
  each changed path, repository root, uncommitted changes, upstream ahead /
  behind counts, and comparisons that skip a redraw when nothing changed. The
  shown workspace's is `app->git_state`; each other workspace keeps its own
  in `VirtualLayout`.
- `layout_store` — saved workspace layouts (`SavedLayout`: panel visibility, tagbars,
  split fractions, orientation, dedicated editor panel and its splits as
  `tab_tree` text without files (`0` marks a pane of files, `t` a terminal
  tab, e.g. `H:0.500([0],[t,t])`), terminal pane tree as text such as
  `H:0.500(T,V:0.400(T,T))`), `layouts.ini` load/save, and the
  default layout of new workspaces (group `[Default]`, built-in if absent);
  `layout_store_presets()`: the built-in Code, Terminal and Focus layouts,
  never written, each hidden by a saved layout of the same name;
  `layout_store_auto()` / `layout_store_grid_splits()`: the Auto layout,
  the current panels with the editor panel split in a grid for the number
  of open files and the panel's size (`layout_store_editor_area()`): side
  by side when wide, stacked when tall, 2×2 or one row / column for four,
  panes of at least `LAYOUT_GRID_MIN_W` × `LAYOUT_GRID_MIN_H`.
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
  previews: `PreviewDoc.pages` + `PreviewInput.page` (`preview_state_page()`,
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
  `user_guide_anchor()` (GitHub heading ids); translated pages from
  `docs/l10n/<lang>/` (prefix `/org/lookin/guide-l10n/`): `user_guide_read_in()`,
  `user_guide_read_localized()` (the Help tab). Its test checks every link of the
  guide and of each translation.
- `org_time` — Org timestamps (`<2026-10-29 Thu 10:00 +1w>`): parse, format,
  field arithmetic, repeaters, the date prompt (`org_timestamp_read()`).
- `org_doc` — Org document commands on an array of lines + cursor (`OrgPos`):
  headings, subtrees, TODO keywords (`#+TODO`), priorities, tags, planning,
  clock, lists, checkboxes, statistics cookies, links, `<s` templates. The
  editor (`editor_org`) and the preview (`orgmode`) both edit through it.
  Lists, checkboxes and statistics cookies are in `org_list.c` (same header);
  both files share the line helpers of `org_doc_private.h`.
- `org_table` — Org tables: align, cell navigation, row / column editing.
- `dunnet` — Ron Schnell's Dunnet text adventure state machine (rooms,
  items, VAX `pokey` UNIX shell, FTP, rlogin to `gamma`, IBM PC XT DOS prompt,
  Endgame riddles), driven by `editor_dunnet` (`M-x dunnet`).
- `line_diff` — Myers line diff of a text against its previous version,
  reduced to per-line gutter marks (`LineDiffMark`: added, modified, deleted).
- `pane_geometry` — `pane_find_neighbor()`: the pane next to another one in a
  direction (same row or column only), used to move editor tabs between
  notebooks; `pane_drop_zone()` / `pane_zone_rect()`: the side of a pane a
  dragged tab is dropped on, and the area it would take.
- `minimap_geometry` — minimap scale, scroll offset and slider (VS Code
  style: fixed minimum scale, the minimap scrolls with the view over long
  documents), slider drag and click-to-center mapping.
- `file_save` — `file_save_text()`: edited text written in place (the target
  of a symbolic link, permissions kept, read-only files refused, atomic).
- `dir_scan` — explorer listings read in a worker thread (`dir_scan_read()`):
  one level sorted like the tree (folders first, then the sort mode) and the
  levels of its expanded folders, sorted on keys made once per entry; the
  same on its own worker (`dir_scan_read_async()`, used when a folder is
  expanded); the folders "expand all" opens (`dir_scan_collect_expand_all()`).
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
- `vim_engine` (+ `vim_engine_motions.c`, `_insert.c`, `_ops.c`, `_normal.c`,
  `_visual.c`, `_ex.c`, internal `vim_engine_private.h`) — the
  Vim editor on a `GtkTextBuffer`: modes, operators, registers, marks, dot
  repeat, macros, Visual block, its own undo list (one step per command,
  GTK's history merges adjacent edits), search and Ex commands. No widgets:
  the host (`editor_vim`) implements `VimHost` (messages, command line
  entry, scroll, clipboard, `:w` / `:q`). In Insert mode text keys are left
  to the text view and recorded from the buffer signals. Tested in
  `tests/test_vim_engine.c` by typing keys on a headless buffer.
- `code_outline_tree` — outline of a file (code-nav.md): entries from a
  language server or the scanner, Markdown headings (ATX, Setext, outside
  fences and front matter), headings from the tag names of rendered
  previews (`code_outline_heading_tag_level()`), the tree (nested in
  containers, grouped by kind) and the entry at a line. Tested in `tests/test_code_outline_tree.c`.
- `code_symbol`, `code_scan_c`, `code_scan`, `code_index` — the built-in
  code index of code navigation (code-nav.md): languages and the
  identifier under the cursor (`code_word_at()`), symbol scanners
  (`code_scan_text()`: a C-family state machine, line regexes for the other
  languages; no parser, locals not listed), the project's files (`git_list_files()`,
  else a bounded walk) and the ranked definition lookup
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
- `session_store` — `session.ini` (workspaces at quit, with their explorer
  view, their panels as a `SavedLayout` and their editor tabs as `tab_tree`
  text) and
  `recent.ini` (recent folders and files, most recent first, `RECENT_MAX`),
  and the saved workspaces of the workspace bookmarks (`sessions/<id>.ini`,
  target `lookinfiles-session://<id>`: `session_target_make()` /
  `session_target_id()` accept lowercase hex ids only, `session_snapshot_path()`).
- `tab_tree` — the editor tabs of a workspace as text (`TabTreeNode`): file
  indices per notebook (`t` for a terminal tab), current page, view last
  focused, and the splits with their share, e.g.
  `H:0.500([0*,2],V:0.400([1!],[0,t]))`.
- `settings_store` — `settings.ini` (`Settings`: editor mode, line numbers
  and wrap of new editor tabs, column line of the editors, tagbars, window
  size at startup, terminal, its place, preview and editor tagbar of the Auto layout), defaults for missing or invalid values, other keys kept
  on save. `app->settings` holds the values in use; the live editor mode is
  `app->default_editor_mode` (top bar, `-e`).
- `trash_restore` — puts back a file moved to the trash: finds its newest
  `.trashinfo` (home trash, then the mount's `.Trash-$uid` / `.Trash/$uid`)
  and moves it back.
- `html_text` — text helpers of the HTML preview: entities (names are
  case-sensitive), attributes, tag stripping, code language, `<meta>`.
- `tex_math` — TeX math (the MathJax / KaTeX subset of notebooks and
  Markdown) parsed into a tree (`TexNode`): symbols as Unicode, letters
  italic, functions upright, fractions, roots, scripts, big operators and
  their limits, `\left` / `\right` and `\big` delimiters, accents, boxes,
  matrices, `cases` and `aligned` environments; never fails (unknown
  commands kept as text, nesting capped). `tex_math_to_text()` gives a
  linear form, for the tests. Its symbol tables (Greek, operators,
  relations, arrows, delimiters, big operators, functions, accents) are
  `tex_symbols`.
- `note_types` — note types of the notes view (`NoteType`: Plain Text,
  Markdown, Org-mode): extension, display name, icon, row badge and the
  name stored in `notes.xml` (the former `rich` reads as Markdown).
- `notes_store` — the notes on disk: notebooks (directories), notes in user
  order, the trash, and the `notes.xml` manifest (`notes_store_load()`
  reconciles it with the disk, `notes_store_save()`). The Notes view is `notes`.
- `canvas_doc` — Canvas notes (`<title>.canvas/`: `page.xml` and
  `media/`): text boxes with styled runs and pictures placed freely, read and
  written as XML (each block with a page-unique `id`; line and arrow ends
  bound to blocks by `start` / `end`; a shape's text in `label`; `locked`;
  a turned shape's `angle` in degrees), new picture names, unused pictures pruned, Markdown export
  (`canvas_doc_export_markdown()`: a `.md` note next to it, pictures linked
  inside the Canvas).
- `canvas_history` — undo / redo of a Canvas note: page states (`page.xml`
  content) recorded before each change, with a step limit.
- `palette` — every colour of the application, by role (`PaletteRole`:
  surfaces, text, accent, git, diff, syntax, editor, terminal ANSI...):
  `palette_color()` ("#rrggbb" for text tags and Pango markup),
  `palette_rgb()` (cairo), `palette_set()` / `palette_reset()` (themes),
  each role a CSS named colour (`@bg_window`, `alpha(@accent, 0.25)`), and
  `palette_expand()` for `@role` tokens in text templates.
- `image_theme` — a theme's colours from an image: main colours of the
  pixels (weighted k-means over a colour histogram, in OKLab), then every
  palette role derived in OKLCH (surfaces tinted by the main colour, dark
  or light by the mean lightness, accent from the most vivid colour, syntax
  from the other hues, colours with a meaning pulled toward a near hue),
  text kept at a readable WCAG contrast.
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
  rendered documents; `fonts_math_family()` for TeX formulas (serif, not
  themed).
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
  once, when the row is inserted, whether it shows an expander. A whole level
  is made with `explorer_row_new_child()` and added with
  `explorer_row_insert_rows()`: one model change instead of one per row.
- `vt_screen` — screen of the built-in terminal: xterm output (UTF-8, C0,
  ESC / CSI / OSC: cursor moves, erase, insert / delete, SGR colours (16,
  256, RGB), scroll region, alternate screen, DEC line drawing, title) on a
  grid of `VtCell`s with a scrollback; answers to cursor position and device
  attribute queries (`vt_screen_take_reply()`). No reflow on resize.
- `vt_pty` — a program on a pseudo-terminal: ConPTY on Windows (output read
  by a thread, handed to the main context), POSIX pty elsewhere
  (`TERM=xterm-256color`).

- `media_tracks` — helpers of the video track menus: track labels
  (`media_track_label()`: language, title or "Track N") and the subtitle
  files of a video among its directory's entries (`media_subtitle_files()`).

**2. Shared types, events & leaf helpers — may use `AppData` and layer 1 only:**
- `app_types.h` — `AppData`, `EditorTab`, `VirtualLayout`, `FileEntry`,
  `SORT_*`, `SIDEBAR_VIEW_*`, `PREVIEW_*` constants.
- `explorer_tree` — the explorer widget (`app->tree`): root row, selection,
  expansion, scrolling and row drawing (git colours, badges). Signals
  `"selection-changed"`, `"row-toggled"` (expanded / collapsed, by the user or
  `explorer_tree_set_expanded()`), `"row-activated"` (double click). Rows are
  addressed by `ExplorerRow*`; positions count visible rows only.
  `explorer_tree_swap_state()` puts the content aside and shows another one.
- `term_widget` — the built-in terminal widget, for builds without VTE
  (Windows; `make BUILTIN_TERM=yes` elsewhere): the VTE API `terminal`
  uses, under VTE's names (`VteTerminal`, `vte_terminal_spawn_async()`...),
  drawing a `vt_screen` fed by a `vt_pty`. With it `HAVE_VTE` and
  `HAVE_BUILTIN_TERM` are both defined, and `app_types.h` includes it
  instead of `<vte/vte.h>`. Keys as xterm sends them, IM text input, drag /
  double click selection, wheel and `Shift+PgUp` / `Shift+PgDn` scrollback.
- `media_player` — the media stream of both video players: a `GtkMediaFile`
  subclass on GStreamer `playbin` (optional, `HAVE_GSTREAMER`) whose frames
  come from an `appsink` as `GdkMemoryTexture`s (subtitles drawn in them by
  playbin). Lists and switches the audio and subtitle tracks
  (`media_player_track_labels()`, `_set_track()`, `_cycle_track()`, signal
  `"tracks-changed"`); subtitle files beside the video are loaded as playbin's
  `suburi` (another one reopens the pipeline at the same position). Without
  GStreamer, `media_player_new()` is a plain `GtkMediaFile` with no tracks.
- `track_menu` — the **Audio** / **Subtitles** menu buttons of a player,
  bound to a `media_player` stream (popovers, current track marked).
- `sparkline` — a small line chart (`GtkDrawingArea`) of the last values of
  one or more series, each in a palette colour, the newest at the right;
  fixed or automatic scale. Used by the system monitor.
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
  The focused panel and the drag state stay private (`struct Panels`); split
  fractions live only in the workspace (`VirtualLayout`), not in `AppData`.
- `outline_pane` — a tagbar beside some content: a `GtkPaned` with a
  `GtkListView` over a `GtkTreeListModel` of an outline (`code_outline_tree`),
  its width shared through an `int*`, collapsed rows kept across refreshes;
  the owner gives the entries and its cursor line, and is called back to go
  to an entry, leave the list or hide the tagbar.
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
  rebuilt (`tree_nav_block_selection_handler()`). `explorer_set_view()` is
  the only way to change the explorer view (`make check` rejects other writes
  of `app->sidebar_view` outside the workspace switch).
- `remote_store` — `RemoteMachine` list and `RemoteFolder`s, `remotes.xml`, `~/.ssh/config`
  discovery, lookups (unit-tested).
- `preview_summary` — summaries shown in the preview for rows without
  file content (remote machine, notebook, bookmark folder, trash, Canvas
  note) as typed lines (title, fields, headings, items, keys), and their
  plain text with columns padded for a monospace font (unit-tested).
- `preview_state` — what the preview shows, in a private struct: the file
  (`preview_state_path()`, `_name()`, `_set_file()`), its detected kind,
  content type and encoding (`_set_detected()`), raw or rendered view
  (`_raw()`), image / PDF zoom, page of a paged preview. Read anywhere;
  written by the viewer and the few places that change what is shown
  (unit-tested).
- `fs_watch` — `GFileMonitor`s on expanded directories, the previewed file and
  `.git`, debounced `schedule_refresh()` (state in a private `struct FsWatch`,
  `fs_watch_free()` at exit); reports through events only.
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
- `after_frame` — runs a function once a widget's next frame is on screen
  (or after a short wait when none comes): the selected row is highlighted
  before its preview is built (`viewer`), the "Rendering…" notice is drawn
  before a display that freezes the window (`preview_busy`).
- `tab_drop` — editor and terminal tabs dragged with the mouse (GtkNotebook
  tab DnD): drop target on a notebook (tab bar: insert; page: move in; edge:
  split), translucent highlight on the window overlay, end-of-drag report
  (`TabDragDoneFunc`, the source notebook's own) once GTK no longer uses the
  source notebook — remove an emptied notebook only then. Same group by
  default, `tab_drop_set_accept()` to take other tabs. Knows nothing of
  `EditorTab`; `editor` attaches it with `editor_notebook_enable_tab_drop()`,
  `terminal` to its own notebooks. On Windows GTK's tab DnD crashes (its
  drag thread, in `DoDragDrop`): a capture gesture on each notebook follows
  the drag instead and drops through the same targets
  (`LOOKIN_INTERNAL_TAB_DRAG=1` turns it on elsewhere, to test it).
- `markdown`, `html` — rich renderers into a `GtkTextBuffer`. The HTML tags are
  created by `html_tags` (`ensure_html_tags()`), its string helpers are `html_text`.
  Markdown (`markdown_parse`), HTML (`html_parse()`) and Org (`org_render`)
  are parsed without widgets into an `MdOut` (`md_out`: the text, tag
  ranges, links, and objects standing as one U+FFFC character: pictures,
  glyphs, tables, highlighted code, TeX formulas), in the preview's worker
  thread;
  `md_out_show()` (`md_show`) puts it in the buffer with one insertion, the
  objects, then the tags (thousands of small insertions into a buffer shown
  in a view cost seconds of signal handlers). `render_markdown_to_buffer()`,
  `render_html_to_buffer()` and `render_orgmode_to_buffer()` do both, for
  the other views.
  `markdown_table` — Markdown tables for `markdown`: rows split into cells, a
  grid widget anchored in the view (scrolling horizontally in its own frame
  when wider than the text, as tall as the table at the width it is shown at;
  the wheel over it scrolls the text around it; the tables of one rendering
  are measured together before the next frame), or a box-drawn text table
  without a view.
  `math_paintable` — a `tex_math` tree laid out as TeX does (classes and
  their spacing, script sizes, fraction and radical rules on the math axis,
  delimiters stretched around it) into a `GdkPaintable` drawn with Pango and
  Cairo in the palette's text colour at snapshot time. `md_show` inserts it
  with a `rise` tag of its depth, so inline formulas share the text's
  baseline; display formulas (`$$`, `\[`, environments, ```` ```math ````)
  are centred lines (`md_math_display`). Markdown finds them in
  `markdown_parse` (`$` needs a non-space inside and no digit after the
  closing one, so prices stay text).
  `markdown_link` — links that can be followed, off by default (previews keep
  them as text): `markdown_link_attach()` on a view makes the renderer record the
  links of its buffer and calls a handler on click (table cells: `<a href>` labels).
- `preview_doc_view` — shows a `PreviewDoc` in the preview text view (style
  tags in the Dark+ palette, images, HTML / Markdown renderers).
- `minimap` — scaled file overview; draws only the part around what is shown
  (`MinimapCache`), geometry from `minimap_geometry`. `minimap_new_for_view()`
  makes the minimap of any text view: editor tabs, the Help tab, and the
  preview (`minimap_viewer_new()`, with `minimap_view_set_shown_if()`:
  Preview page only); its state goes with the widget. Shows itself only
  while the text overflows; a change that may come from a height estimate is
  confirmed on the next frame (no visible delay). While GtkTextView is still
  laying out changed text (lines not measured yet have no height), the
  previous render stands in, so the minimap never shows a cut-off document.
  `LOOKIN_DEBUG_MINIMAP=1`
  prints each show / hide with its time, to check for flicker.

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
  (XML persistence: `bookmarks.xml`, `notes.xml` via `notes_store`; remotes via
  `remote_store`). The notes view is `notes.c` (rows, selection, keys, menu),
  `notes_dialogs.c` and `notes_actions.c`, sharing `notes_private.h`.
- `canvas_view` — the **Canvas** tabs (`app->canvas_pages`, every workspace):
  each edits one Canvas note (`canvas_doc`), one tab per note, text boxes and pictures on a
  `GtkFixed`; move / resize by dragging, pictures pasted (`Ctrl+V`) or dropped
  into `media/`; saved automatically, on close and at quit (never while the page
  is being destroyed). Undo / redo restore a page state from `canvas_history`;
  unused pictures are pruned only when a note is loaded or closed, so an undo
  can bring a deleted picture back. Not an editor tab: `app->editors` stays text only;
  it opens in the editor panel (`editor_add_page()`) and moves like a terminal tab
  (`editor_page_set_movable()`: drag, splits, layouts with the files). It belongs to
  its workspace: in the editor panel it goes with the panel, in the preview notebook
  through `canvas_view_workspace_hidden()` / `_shown()` / `_removed()`.
  Its files: `canvas_view` (widgets, tool bar and panel actions, public API),
  `canvas_page` (the page state in `canvas_page_private.h`, internal to these
  files: focus, selection, undo, saving, new blocks, the tool), `canvas_keys`
  (keys), `canvas_clip` (copy / cut / paste / duplicate of blocks: a
  private clipboard type holding the source note and the blocks' XML, plus
  their Markdown as text; picture paste), `canvas_menu` (right-click menu
  of the page: Paste at the click), `canvas_drag` (drawing tools, moving / resizing with
  snapping, line points (added, moved, removed), clicks, drops).
  Its parts: `canvas_toolbar` (floating tool bar: select, shapes, pencil,
  text, table, eraser, and the grid / export menu), `canvas_export` (PNG / SVG of the page, written to the home folder: its
  render node drawn with cairo, cropped to the blocks), `canvas_props`
  (floating panel on the right, with room for it right of the page: shape style; the actions on the selected blocks: align
  and distribute, drawing order, group, lock, crop a picture), `canvas_format_bar` (text formatting, a row
  floating above the text box), `canvas_table_bar` (insert / delete rows
  and columns at the current cell, floating above the table),
  `canvas_float` (what the two bars share: placement next to the block,
  following it each frame, and their look), `canvas_table` (the cells of a
  table block), `canvas_draw` (a shape
  following the pointer while it is drawn), `canvas_item` (the block widgets),
  `canvas_viewport` (the scrolled view: the page drawn at a scale by a
  transform, so its coordinates stay page coordinates; Ctrl+wheel zoom, pan),
  `canvas_bind` (arrow and line ends bound to blocks by their ids, kept on
  their outlines), `canvas_line` (lines and arrows edited as in Excalidraw:
  picked on their stroke, handles on their points and segment middles),
  `canvas_rotate` (the round handle above the selected rectangle, diamond
  or ellipse that turns it around its centre, as in Excalidraw: a child of
  the surface on top of the blocks, following the shape each frame while
  shown; the shape's frame turned by a `GtkFixed` child transform),
  `canvas_rough_draw` (hand-drawn rendering, line handles),
  `canvas_geometry` (snap, align, outline points, drag box, 45-degree lines,
  turned boxes and the rotation handle angle,
  the curve through the points of a line; unit-tested), `canvas_preview`
  (the preview of a Canvas note: the same block widgets, read-only, in the
  "canvas" page of the preview stack).
- `video_tab` — the **Video** tabs (`app->video_pages`, every workspace): a
  video opened with `Enter` in the explorer plays in a tab of the editor panel,
  one per file, with its own `media_player` stream (cleared on close: GStreamer frees
  its pipeline only then); separate from the preview player (`video`). Opened
  with `editor_add_page()`, movable like a Canvas tab, kept with its workspace
  through `video_tab_workspace_hidden()` / `_shown()` / `_removed()`.
- `ipynb_tab` — the Jupyter notebook tabs (`app->ipynb_pages`, every
  workspace): `ipynb_tab_open()` is the `EditorOpenHook` of `editor`
  (`editor_set_open_hook()`, set in `main.c`), so a `.ipynb` file opened as
  an editor file gets this tab instead (an nbformat 3 or broken one stays
  text; `editor_open_text_file()` skips the hook: **Edit as JSON**). One box
  per cell (type menu, source view highlighted with the kernel's language,
  outputs, `text/latex` formulas as a `math_paintable`; a Markdown cell also has a rendered view, `render_markdown_to_buffer()`,
  shown instead of its source unless its key is in `editing`: run renders it,
  `Enter` / double click edits it; the rendered view sits in a scrolled window
  of its own, horizontal only, so its tables follow the cell's width and
  never hold the cells wider than the tab) built again after each change of the cells' number, order or
  type; saved by itself after each change (`file_save_text()`), at close,
  workspace removal (`ipynb_tab_workspace_removed()`) and quit
  (`ipynb_tab_save_all()`). Movable like a Video tab, kept with its
  workspace through `ipynb_tab_workspace_hidden()` / `_shown()`; its file is
  the page's `editor_page_file()`, which `editor_session` saves and reopens.
  `ipynb_run.c` runs the cells (it shares `ipynb_tab_private.h` with
  `ipynb_tab.c`): an `ipynb_kernel` started at the first run, the bridge
  script from the GResource, outputs added to the `IpynbDoc` by cell key
  and drawn once per main loop turn (`ipynb_tab_refresh_cell()`: the other
  cells and the source are left alone), the input row, interrupt, restart,
  the kernel menu (labelled with the notebook's kernel;
  `ipynb_kernel_list()` each time it opens; a choice sets the notebook's
  kernelspec and starts that kernel) and the kernel state in the tab's
  status bar.
  `ipynb_tagbar.c` (also on `ipynb_tab_private.h`) puts an `outline_pane`
  beside the cells (the editor tagbar's width), filled by `ipynb_outline`
  600 ms after the last change (`ipynb_tab_mark_changed()`), set again only
  when the headings changed (`ipynb_outline_same()`: outputs coming in
  leave it alone), following `IpynbTab.current`; `ui.c` finds its list for
  `Alt+Left` / `Alt+Right` with `ipynb_tab_tagbar_list()`.
- `sysmon_tab` — the System Monitor tab in the preview notebook (one per
  workspace, kept like the Help tab): cards with `sparkline` charts (CPU and
  a bar per core, memory and swap, load, network, disk I/O), the mounted
  filesystems and the processes (`sysmon_procs`: a sorted `GtkColumnView`,
  read only), from `sysstat` samples read in a worker each second (processes
  and filesystems only while the page is mapped). `sysmon_indicator` — the
  CPU chart of the top bar (`/proc/stat` every 2 s; `LOOKIN_SYSMON_FROZEN`
  stops it for the UI tests), whose click opens or closes the tab.
- `clock_pill` — the clock and three timers (`countdown`; the time format
  and timer 1 from `settings.ini`, changed by the Clock page of the
  Preferences through `topbar_set_clock()`, `topbar_set_timer()` and
  `topbar_set_timer_expiry()`; a hidden time leaves an icon while no timer
  shows), before the CPU chart. A click on a timer or the
  clock opens a popover with a stack page per timer (state, durations,
  Start, expiry action Restart / Stop / Count On, Pause, Reset, Off), a
  right click on a timer pauses it (in overtime too). One timeout at the next change of all of them (a
  second, half a second while blinking, else the second or the minute of
  the clock);
  `LOOKIN_CLOCK_FROZEN` stops everything for the UI tests.
- `games` — the hidden Games view (unlocked in the help panel, for the
  current workspace only: `VirtualLayout.games_unlocked`) and its game tabs
  (`game_*`) in the preview notebook, kept with their workspace through
  `games_workspace_hidden()` / `_shown()` / `_removed()`; the real-time games
  wait while their tab is not on screen.
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
- `summary_view` — draws a `preview_summary` in the preview in the style of
  the directory preview (icons, section bands, columns on tab stops).
- `dir_preview` — directory preview (`dir_preview_show()`): one level read
  in a worker thread after a short delay (never recursive), header and table.
  A folder seen before is shown at once from a `preview_cache`, and drawn
  again only if the worker reads something different.
- `orgmode`, `pdf`, `video`, `audio`, `git_tab` — preview renderers / tabs.
  `orgmode` renders and edits Org files on disk; it knows nothing of editor tabs.
  `org_fold` keeps the folding of the Org preview (headings folded by hand,
  the global Shift+Tab state) that `orgmode` toggles and `viewer` renders with
  (a copy, `org_fold_state_copy()`, goes to the worker). `org_render` parses
  the document for the preview (`MdOut`) with the Org line of each preview
  line (`OrgLineInfo`), which `orgmode_show()` keeps on the buffer for clicks
  and keys.
- `editor` — editor tabs in the notebook(s): open, save, close, move, split
  view (`editor_split_tab()`: a second tab on the same buffer), dedicated panel.
  `editor_notebooks.c` holds the notebook tree (moving, splitting, dropping
  tabs and movable pages), `editor_tab_menu.c` the tab menus and the clicks on
  a tab title; they share `editor_private.h`, the public API stays `editor.h`.
  `editor_open_file()` first offers the file to the open hook (a page of its
  own: `ipynb_tab`) when no editor tab has it; a movable page that edits a
  file names it with `editor_page_set_file()`.
- `editor_session` — the editor tabs of a workspace for the session:
  `editor_session_capture()` describes the preview notebook and the
  dedicated panel (notebooks, splits, sizes, current pages, view last
  focused) as `tab_tree` text (the files of the tabs, notes included, and
  the directories of the Canvas tabs), `editor_session_restore()` reopens
  the files and Canvas notes (`canvas_view_open()`) at the same place (`editor_notebook_split()`, `editor_move_tab_to()`,
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
  Mode implementations: `editor_notepad`, `editor_emacs` (with
  `editor_block_cursor`, its block cursor), `editor_vim` (the
  host of `vim_engine`: status line, command line, `VimHost` callbacks), and
  `editor_org` (Emacs Org-mode keys in tabs: maps keys to `org_doc` /
  `org_table` commands, applies only the changed lines as one undo step,
  folding with an invisible tag). `editor_modes_private.h` (state
  struct, mode enums) is internal to these 6 files.
  Mode bar labels whose text can grow (file names, messages) must use
  `editor_bar_label_shrinkable()`, and filler text `editor_bar_clip()`: a bar
  wider than the panel pushes the minimap out of the window.
- `editor_dunnet` — the `*dungeon*` buffer tab opened by `M-x dunnet` in Emacs
  mode (`dunnet` state attached to the `GtkTextBuffer`, prompt protection,
  `M-p` / `M-n` command history).
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
- `code_outline` — the outline of an editor tab (server symbols, else the
  scanner, or Markdown headings) collected in a worker, for the picker and
  the tagbar.
- `code_symbols` — symbols of the file in an `AppDialog` with a filter.
- `editor_tagbar` — the tagbar of an editor tab: an `outline_pane` beside
  the view, fed by `code_outline` after a pause in the edits, following the
  cursor and jumping through `code_nav`.
- `preview_tagbar` — the tagbar of the preview: an `outline_pane` around
  the preview text, listing the heading tags of the rendered buffer after
  each render, plus the headings a renderer hands over
  (`preview_tagbar_set_outline()`: the PDF bookmarks from `pdf`, the
  symbols from `preview_symbols`), following the scrolling; `o` /
  `Ctrl+K Ctrl+O` from `on_viewer_key_press()`.
- `preview_menu` — the right-click menu of the preview: a section added to
  the text view's extra menu (after Copy Image) that shows or hides the
  tagbar (`preview_tagbar_toggle()`) and the preview's minimap
  (`minimap_viewer_set_enabled()`) in the current workspace, labels
  updated on each right click. `workspace` keeps both per workspace
  (`VirtualLayout.preview_tagbar` / `preview_minimap`).
- `preview_symbols` — the symbols of a source code preview: the built-in
  scanner (`code_scan`) on the text shown, in a worker, handed to
  `preview_tagbar_set_outline()` while the preview is current.
- `nav_history` — per-workspace folder history (`VirtualLayout.dir_history`,
  fed by `reload_directory()`; `go_back_directory()` / `go_forward_directory()`
  / `go_history_directory()` in `file_list` walk it), with the entry
  selected in each folder left (selected again on the way back), and the
  recent folders and files (editor tabs opened, quick open choices).
- `quick_open` — Ctrl+P / Emacs `C-x C-f`; Alt+R lists the recent places
  in the same dialog (`quick_open_show_recent()`). Ctrl+P: the project's files
  (`code_index_all_files()` in a worker) in an `AppDialog`, ranked by
  `fuzzy_match`; shows the file with `reveal_path_in_tree()` or edits it.
- `terminal` — embedded terminals (VTE, or `term_widget` on Windows) as tabs of terminal notebooks (group
  `TERM_TABS_GROUP`) split by `GtkPaned`s; tabs are dragged between them or
  to a pane edge through `tab_drop` (`Ctrl+T` new tab, `Ctrl+Shift+T` new
  pane below, `terminal_open()` with a working directory); ssh into the
  active remote machine; the shell is `$SHELL` (on Windows PowerShell 7,
  Windows PowerShell, then `%COMSPEC%`); `Ctrl+Shift+C` / `Ctrl+Shift+V`
  copy the selection / paste. Terminal tabs also go into the editor and preview
  notebooks (`tab_drop_set_accept()` on both sides); `editor_remove_page()`
  takes them out when the shell exits, and the preview notebook's ones stay
  with their workspace (`terminal_workspace_hidden()` / `_shown()`). Each
  workspace keeps its own panes, last focused pane and set-aside tabs in
  `VirtualLayout.term` (private `struct TermWorkspace`); the shown one is
  read from there, never copied into `AppData`.
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
  bottom of each panel (`search_bar_new()`; bars and state in the private `struct Search`), opened and
  driven by the keys of the global mode (`/` `n` `N`, `Ctrl+F` `F3`, `C-s`
  `C-r` `C-g`) and drawn like an editor tab's search in that mode (Notepad
  find bar, Emacs I-search minibuffer, Vim `/` command line: the mode bar
  stylesheet of `editor_modes_load_css()`).

**4. Window chrome — may call features:**
- `topbar` — the top bar row built by `topbar_new()`: back / forward arrows
  (folder history, right click: the list), breadcrumbs, global mode pill (it
  shows and sets the global mode only; a tab's mode is set from its tab menu)
  and toggle pills; the split / dock button of the preview notebook
  (`topbar_split_button_new()`); `update_path_label()`. Its widgets are
  private (`struct TopBar`).
- `status_bar` — panel status bars (no window-wide bar: editor tabs have
  their own, `editor_modes`): explorer bar (`create_explorer_bar()`: Git branch of the files
  view with modification `*` and ahead/behind `↑` / `↓` indicators, `app_notice()` message with Undo, sort order of the files view (click cycles it, like `o`); the message moves to the top bar,
  `create_topbar_notice()`, while the explorer is hidden), preview bar
  (`create_preview_bar()`, under the preview page only: name, size, modification
  date, type, encoding); `update_status_bar()` refreshes both. Their widgets are private
  (`struct StatusBars`).
- `sidebar` — explorer header: view switcher and per-view action toolbars;
  `sidebar_header_update()` (called by `update_path_label()`) shows the
  current view. Its widgets are private (`struct SidebarHeader`).
- `help` — cheat sheet (`?`, right click on the Help button); `KEY_HELP` must stay in sync with `docs/keyboard.md`.
- `help_guide` — the **Help** page of the preview notebook (`app->help_page`, after
  the Git and result pages): a `user_guide` page rendered by `markdown`,
  scrolled with the navigation keys of the global mode; links followed through
  `markdown_link` (`user_guide_resolve_link()`: guide pages and anchors, web pages
  in a browser tab, `browser_open()`), Tab / Enter, back / forward history (Shift+H / Shift+L,
  mouse buttons), a minimap (`minimap_new_for_view()`). F1 (`ui`) and a click on the Help button (`topbar`) open it,
  its close button, a middle click or Ctrl+W close it.
  Per workspace (`VirtualLayout.help_page`): the page, with its history and
  scroll position, is taken out of the notebook on a switch and put back
  when its workspace returns (`help_guide_workspace_hidden()` / `_shown()`).
- `workspace` — virtual workspaces (Alt+1..8): save/apply layout snapshots,
  top-bar workspace minimap (`workspace_minimap_redraw()`; the minimap and
  the layout being applied are private, `struct WorkspaceView`). A new workspace (Alt++) starts from the default
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
  Save As, From Image (`image_theme` colours of the selected or typed
  image file), Edit in a tab, Delete; its buttons report dialog responses
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
  `GtkPopover` listing the current arrangement, the default, the saved
  layouts, then below a separator Auto (with files open:
  `editor_layout_file_count()`) and the built-in presets; the hovered or selected row is shown in full after a short delay,
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
  under the name "Default") or reset, and ends with the built-in presets
  (not deletable; saving under a preset's name replaces it). A click selects a row; double click,
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

To show or hide a panel, call `app_layout_begin()` (snapshot + hold), change
the widgets, then emit `APP_EVENT_LAYOUT_REBUILT`. While the layout is held
(`app_layout_held()`), pane moves are not recorded as user drags; the
workspace releases it (`app_layout_release()`) once the saved positions are
applied. `app_layout_hold()` holds without the snapshot.

### Feature state (keep it out of `AppData`)

A feature keeps its widgets and state in a private struct defined in its own
`.c` file, with a typed pointer in `AppData` (`struct AudioPreview* audio`,
`audio.c`): created by its `*_init()`, freed by its `*_free()`, never read
by other modules. Another module that needs something gets a function in the
feature's header, not a field. Do not add feature fields to `AppData`.
The pointer is typed, never a `gpointer`: name the struct
(`typedef struct Foo { ... } Foo;` in the `.c` file) and declare the field
`struct Foo* foo` in `app_types.h`, so the compiler checks every use.

### Known coupling (avoid making it worse)

The dependencies between the directories of `src/` are frozen in
`DIR_DEPS` (`tools/check_layers.sh`): `make check` fails when a file includes
a header of a directory its own directory does not depend on yet. Prefer an
event or moving the shared code down a layer; list a new dependency only
when neither fits, and drop one from the list once it is gone.

`make check` fails on any include cycle (two `.c` files including each other's
header), except the two accepted ones: `file_list` ↔ `bookmarks` and
`file_list` ↔ `remote`. The explorer dispatches keys and rows to these sidebar
views, and the views ask the explorer to navigate (`navigate_to_path()`,
`reload_directory()`). Breaking a new cycle: emit an event, or move the shared
code down a layer (as done for `panels`, `fs_watch`, `remote_store`, `editor_org`).

Shortcut list to keep in sync with `docs/keyboard.md` and `help.c` when you change behavior.

