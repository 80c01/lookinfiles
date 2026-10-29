# AGENTS.md

Guidelines for AI coding agents working in this repository.

## Project overview

`file explorer` written in C with GTK 4 (4.12 or newer, no deprecated API). Modular architecture in `src/`,
no external dependencies beyond GTK (with optional VTE for GTK 4, GtkSourceView 5, and poppler-glib).

Layout: left = explorer panel (files, bookmarks, notes or remote view,
`GtkListView` over a `GtkTreeListModel` of `ExplorerRow`, expandable directory
tree with lazy-loading),
right = preview notebook (viewer, editor tabs, Git tab) and terminal panes,
all inside resizable `GtkPaned`s.

## Build, test & run

```bash
make              # incremental build into build/ (gcc -Wall -Wextra, pkg-config gtk4)
make test         # GLib unit tests (headless, no display needed)
make check        # zero-warning build + unit tests + module layering rules
make ui-test      # headless UI smoke test (needs xvfb xdotool imagemagick)
make clean        # remove binary and build/
./explorateur_fichiers   # run (requires a display; shows a warning without one)
```

- **Run `make check` before every commit.** It fails on any compiler warning,
  failing test, or layering violation (`tools/check_layers.sh`).
- The build compiles every `src/*.c` automatically (no file list to maintain)
  and tracks header dependencies, so editing a header rebuilds its users.
- There is no real display in the sandbox (`Gtk-WARNING: cannot open display`
  is expected when running the binary directly). Logic that can be tested
  without widgets belongs in a foundation module with unit tests.
- **After any UI change, run `make ui-test`** (`tests/ui/run_ui_test.sh`). It
  runs the app under Xvfb, drives a keyboard/mouse scenario with xdotool and
  saves one screenshot per step in `build/ui/shots/` — open the relevant ones
  to check the result. It fails on a missing window, any GTK warning or
  critical in the log (e.g. invalid Pango markup), or a dirty `Ctrl+Q` exit.
- To prove a refactoring changes nothing visible: run `make ui-test` on the
  reference commit (e.g. in a `git worktree`), copy `build/ui/shots` aside,
  then run `UI_BASELINE=<that copy> make ui-test` on your branch. The terminal
  step is not compared (shell prompt timing varies).
- When adding a feature with a shortcut, add a step to the scenario in
  `tests/ui/run_ui_test.sh` (`key ...; shot <name>`).
- Text interpolated into Pango markup must be escaped: use
  `g_markup_printf_escaped()`, never `g_strdup_printf()`, with names, titles,
  hosts, key labels or any user-provided string.
- The binary `explorateur_fichiers` and `build/` are generated — never edit them.

## Architecture (keep it when editing)

Modules live in `src/<module>.c` + `src/<module>.h`. Layers, from bottom to top
(a module may only depend on modules in its own layer or below):

**1. Foundation — pure helpers, no `AppData`, no widgets, unit-tested in `tests/`:**
- `file_types` — name-based detection (`file_extension()`, `is_*_file()`,
  `is_renderable_file()`) and emoji icons (`file_icon()`).
- `git_utils` — `find_git_root()`, `git_resolve_dir()` (worktrees/submodules),
  `git_read_branch()`, `scan_git_statuses()` (spawns git: worker thread only).
- `layout_store` — saved workspace layouts (`SavedLayout`: panel visibility,
  split fractions, orientation, dedicated editor panel, terminal pane tree as
  text such as `H:0.500(T,V:0.400(T,T))`), `layouts.ini` load/save, and the
  default layout of new workspaces (group `[Default]`, built-in if absent).
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
- `theme` — global CSS provider (`apply_panel_borders()`).
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
- `markdown`, `html` — rich renderers into a `GtkTextBuffer`.
- `minimap` — scaled file overview.

**3. Features:**
- `file_list` — async directory scan (`GTask`), tree population, sorting
  (`compare_entries_with_data()`), lazy expand/collapse, git badges in the tree,
  navigation (`reload_directory()`, `navigate_to_path()`, `refresh_listing()`,
  `activate_selected_entry()`), sidebar view toggles, tree key handler
  (`on_tree_key_press()`).
- `file_actions` — new file / new folder prompts.
- `bookmarks`, `notes`, `remote` — the bookmarks, notes and SSH remote views
  (XML persistence: `bookmarks.xml`, `notes.xml`; remotes via `remote_store`).
- `viewer` — right-pane preview dispatch (`on_selection_changed()`,
  `load_file_content()`, `viewer_reload_preview()`), image/text/dir previews,
  viewer keys.
- `orgmode`, `pdf`, `video`, `audio`, `git_tab` — preview renderers / tabs.
  `orgmode` renders and edits Org files on disk; it knows nothing of editor tabs.
- `editor` — editor tabs in the notebook(s): open, save, close, move, dedicated panel.
- `editor_modes` — per-tab mode state, shared editing operations (align,
  format, case, indent, find/replace, goto line), tab label, status, key dispatch.
  Mode implementations: `editor_notepad`, `editor_emacs`, `editor_vim`, and
  `editor_org` (Org editing keys in tabs). `editor_modes_private.h` (state
  struct, mode enums) is internal to these 5 files.
  Mode bar labels whose text can grow (file names, messages) must use
  `editor_bar_label_shrinkable()`, and filler text `editor_bar_clip()`: a bar
  wider than the panel pushes the minimap out of the window.
- `terminal` — embedded VTE panes (`Ctrl+T`), Dark+ ANSI palette; ssh into
  the active remote machine.
- `search` — live search entry (`/`, `n`, `N`) for list and viewer.

**4. Window chrome — may call features:**
- `topbar` — breadcrumbs and toggle pills; `update_path_label()`.
- `status_bar` — bottom bar (branch, selection, size, type, editor mode menu).
- `sidebar` — explorer header: view switcher and per-view action toolbars.
- `help` — cheat sheet (`?`); `KEY_HELP` must stay in sync with `README.md`.
- `workspace` — virtual workspaces (Alt+1..8): save/apply layout snapshots,
  top-bar workspace minimap. A new workspace (Alt++) starts from the default
  view (start directory, files view) with the panels of the default layout
  (`layout_store_load_default()`), never from a copy of the current one.
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

Handlers run synchronously inside `app_emit()`. `make check` rejects direct
calls to the first five subscribers outside their module and `main.c`. A new
upward notification = a new `AppEvent` + one `app_events_subscribe()` in `main.c`.

### Where to put new code

- Logic that does not need widgets (parsing, classification, persistence, path
  or git handling): a layer 1–2 module + a test in `tests/test_<module>.c`
  (register it in `TESTS` / `TEST_DEPS_*` in the Makefile, and its allowed
  includes in `tools/check_layers.sh`).
- A new feature: its own `src/<feature>.c/.h`, not appended to an already
  large file (`notes.c`, `html.c`, `orgmode.c`, `viewer.c` are > 1200 lines).
- Include the header of every module you call, directly. Do not rely on
  transitive includes, and remove includes you no longer need.
- Callbacks used in one file are `static`; never declare another module's
  function locally in a `.c` file — add it to that module's header.
- To select a row programmatically use `select_row()` (selecting the selected
  row again still refreshes the preview); to re-render the current preview, emit
  `APP_EVENT_PREVIEW_STALE`.

### Known coupling (avoid making it worse)

`make check` fails on any include cycle (two `.c` files including each other's
header), except the two accepted ones: `file_list` ↔ `bookmarks` and
`file_list` ↔ `remote`. The explorer dispatches keys and rows to these sidebar
views, and the views ask the explorer to navigate (`navigate_to_path()`,
`reload_directory()`). Breaking a new cycle: emit an event, or move the shared
code down a layer (as done for `panels`, `fs_watch`, `remote_store`, `editor_org`).

Shortcut list to keep in sync with `README.md` and `help.c` when you change behavior.

## UI conventions (keep views consistent)

The explorer views (files, bookmarks, notes, remote) share one vocabulary.
Check these before adding or changing an action, a tooltip or a dialog:

- **Same action, same key, in every view**: `a` add, `Shift+A` new folder /
  notebook, `F2 / r / e` edit or rename, `d / Delete` delete (with
  confirmation, or move to a trash), `Shift+K / Shift+J` move up / down,
  `o` cycle the sort of the current view, `Enter` open. A key that has no
  meaning in a view does nothing there (never acts on another view).
- **Tooltips**: Title Case action, then the keys in parentheses, primary key
  first: `Edit Selected (F2 / r / e)`, `Delete Selected (d / Delete)`.
- **Context menus**: same labels as tooltips, `…` (single character) when a
  dialog follows, key hint written `Shift+A`, `Delete`, `F2`.
- **No external windows**: every interaction stays inside the application
  window. Prompts, confirmations and messages are `AppDialog`s (drawn over
  the window); panels such as the help cheat sheet are overlays or panes;
  menus are `GtkPopover`s anchored to a widget of the window. Never open a
  top-level window (`gtk_window_new()`, `GtkAlertDialog`, `GtkFileDialog`,
  file choosers...): `make check` rejects it outside `main.c`.
- **Dialogs**: every button has a mnemonic (`_Cancel`, `_Save`); the confirm
  button is a verb (`_Create`, `_Rename`, `_Move to Trash`), never `OK`;
  information-only messages have a single `_Close` button; destructive
  confirmations default to Cancel.
- **Empty states**: an empty container shows `(empty)`; an empty view shows
  `No <items> yet — press a to add one`.
- **Icons**: buttons and fixed tab headers use GTK theme icons through
  `icons.c` (`icon_button_new()`, `icon_button_set_content()`,
  `icon_label_new()`), never emoji or text symbols; use `edit-delete` for a
  permanent delete and `user-trash` for a move to trash. Emoji are kept only
  for file-type icons in the tree and editor tabs. The CSS colours `label`
  and `image` nodes together (symbolic icons follow the text colour).
  The icons are bundled in the binary (`icons/`, compiled as a GResource by
  the Makefile, licence in `icons/COPYING`) so they render the same with any
  icon theme. To add one: copy a *current* Adwaita symbolic SVG (no
  `transform`, enforced by `make check`) as
  `icons/scalable/actions/explorateur-<name>-symbolic.svg`, list it in
  `icons/explorateur.gresource.xml`, and if `<name>` is not built into libgtk
  add a `FALLBACKS` entry in `icons.c` ending with a built-in icon (used when
  SVG cannot be loaded). Check with `make ui-test`, `UI_ICON_THEME=hicolor
  make ui-test` and `EXPLORATEUR_DEBUG_ICONS=1 ./explorateur_fichiers`.
- **Preview text views** all use `set_view_text_padding()` (10 px sides).
- **Names**: `Org-mode`, `Rich Format`, `Plain Text`; view titles EXPLORER,
  BOOKMARKS, NOTES, REMOTE.
- Keep `help.c` (`KEY_HELP`) and the README shortcut tables in sync.

## Conventions and hard rules

1. **English only** — every user-visible string, code comment, commit message,
   and documentation file must be in English. No French anywhere.
2. **GTK API correctness**:
   - `GtkScrolledWindow` has one child only (`gtk_scrolled_window_set_child()`).
     Put extra widgets in a `GtkBox`/vbox, or above the pane.
   - No deprecated GTK API: the build does not hide deprecation warnings, so
     `make check` fails on them. Lists are `GListStore` + `GtkListView` /
     `GtkColumnView` (never `GtkTreeView`, `GtkTreeStore`, `GtkListStore` or
     cell renderers), dialogs are `AppDialog` (`dialog.h`, never `GtkDialog`
     or `GtkMessageDialog`), URIs open with `GtkUriLauncher`, CSS providers go
     on the display (`gtk_style_context_add_provider_for_display()`).
   - `GtkTextBuffer` accepts valid UTF-8 only. Validate content with
     `g_utf8_validate()` / sanitize with `g_utf8_make_valid()` before inserting
     file contents.
   - Rebuild the explorer with `explorer_tree_clear()` + repopulation from
     `explorer_tree_get_root()`, between `tree_nav_block_selection_handler()`
     and `tree_nav_unblock_selection_handler()`, then select a row.
3. **Performance**: never load a whole huge file into the right pane; keep the
   preview path (`fread` of `PREVIEW_BYTES` only). Directory enumeration is
   O(n) with one `g_stat` per entry — fine for typical directories; do not add
   recursive scans on the UI thread.
4. **Top bar is single source of truth** for directory/sort/hidden state via
   `update_path_label()` (`topbar.c`); do not duplicate that state elsewhere.
5. `Ctrl+Q` quits from `on_window_key_press()` (`ui.c`, quits the `GMainLoop`);
   keep it working when adding key handlers (do not swallow it).
6. **Keep the current layout** (GtkPaned + GtkSearchEntry + outer_vbox). Do not
   switch to GtkGrid fixed cells or plain GtkBox: resizability is a user
   requirement.
7. Language: C99-ish; `-Wall -Wextra` must stay clean (zero warnings).
8. **Always validate after modification**: run `make check` after every
   modification (zero warnings, tests passing, layering OK) before completing
   a task or committing.

## Git

- The user expects a commit after each accepted change (one change = one commit).
- Commit messages: short imperative summary, change-focused (subject only,
  no body needed). Example: `Add Ctrl-F / Ctrl-B paging in left panel`.
- `git add` only files you deliberately changed. Amend only your own
  immediately-prior commit (e.g. forgotten fix), never user work.

## Known status

Recent features are stable (v1.17 in README.md): modular architecture in `src/`,
hanging indent and aligned paragraph wrapping for HTML & Markdown lists,
enhanced HTML renderer (base64 images, code blocks, task lists, description lists, details/summary, table captions, math/arrow/Greek entities, metadata banner),
custom panel backgrounds (#191A1B file panel, #121314 preview, #1f2021 terminal),
VS Code Dark+ full theme & icon matching, text viewer padding for text/code files, PDF document preview in right panel, image preview in right panel, monospace font for code files, expandable directory tree (`GtkTreeListModel`), rendered Markdown & HTML previews, resizable terminal panes (`GtkPaned`), resizable layout, vim bindings, navigation, hidden toggle,
sort cycle, previews, dark theme. UI language is English end-to-end, including README.md.
