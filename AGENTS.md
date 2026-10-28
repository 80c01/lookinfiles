# AGENTS.md

Guidelines for AI coding agents working in this repository.

## Project overview

`file explorer` written in C with GTK 4 (API 4.0+). Modular architecture in `src/`,
no external dependencies beyond GTK (with optional VTE for GTK 4, GtkSourceView 5, and poppler-glib).

Layout: left = explorer panel (files, bookmarks, notes or remote view,
`GtkTreeView` + `GtkTreeStore`, expandable directory tree with lazy-loading),
right = preview notebook (viewer, editor tabs, Git tab) and terminal panes,
all inside resizable `GtkPaned`s.

## Build, test & run

```bash
make              # incremental build into build/ (gcc -Wall -Wextra, pkg-config gtk4)
make test         # GLib unit tests (headless, no display needed)
make check        # zero-warning build + unit tests + module layering rules
make clean        # remove binary and build/
./explorateur_fichiers   # run (requires a display; shows a warning without one)
```

- **Run `make check` before every commit.** It fails on any compiler warning,
  failing test, or layering violation (`tools/check_layers.sh`).
- The build compiles every `src/*.c` automatically (no file list to maintain)
  and tracks header dependencies, so editing a header rebuilds its users.
- There is no display in the sandbox (`Gtk-WARNING: cannot open display` is
  expected). UI behavior is tested interactively by the user; logic that can
  be tested without widgets belongs in a foundation module with unit tests.
- The binary `explorateur_fichiers` and `build/` are generated — never edit them.

## Architecture (keep it when editing)

Modules live in `src/<module>.c` + `src/<module>.h`. Layers, from bottom to top
(a module may only depend on modules in its own layer or below):

**1. Foundation — pure helpers, no `AppData`, no widgets, unit-tested in `tests/`:**
- `file_types` — name-based detection (`file_extension()`, `is_*_file()`,
  `is_renderable_file()`) and emoji icons (`file_icon()`).
- `git_utils` — `find_git_root()`, `git_resolve_dir()` (worktrees/submodules),
  `git_read_branch()`, `scan_git_statuses()` (spawns git: worker thread only).

**2. Shared types & leaf UI helpers — may use `AppData` and layer 1 only:**
- `app_types.h` — `AppData`, `EditorTab`, `VirtualLayout`, `FileEntry`,
  tree columns (`COL_*`), `SORT_*`, `SIDEBAR_VIEW_*`, `PREVIEW_*` constants.
- `text_style` — Dark+ GtkSourceView scheme, monospace, panel background,
  syntax language (`is_code_file()`, `apply_viewer_language()`).
- `theme` — global CSS provider (`apply_panel_borders()`).
- `tree_nav` — explorer row traversal and selection (j/k, gg/G, Ctrl+F/B,
  zz/zt/zb, `select_by_path()`).
- `markdown`, `html` — rich renderers into a `GtkTextBuffer`.
- `minimap` — scaled file overview.

**3. Features:**
- `file_list` — async directory scan (`GTask`), store population, sorting
  (`compare_entries_with_data()`), lazy expand/collapse, git badges in the tree,
  navigation (`reload_directory()`, `navigate_to_path()`, `refresh_listing()`,
  `activate_selected_entry()`), sidebar view toggles, tree key handler
  (`on_tree_key_press()`).
- `file_actions` — new file / new folder prompts.
- `topbar` — breadcrumbs and toggle pills; `update_path_label()`.
- `sidebar` — explorer header: view switcher and per-view action toolbars.
- `status_bar` — bottom bar (branch, selection, size, type, editor mode menu).
- `bookmarks`, `notes`, `remote` — the bookmarks, notes and SSH remote views
  (XML persistence: `bookmarks.xml`, `notes.xml`, `remotes.xml`).
- `viewer` — right-pane preview dispatch (`on_selection_changed()`,
  `load_file_content()`), image/text/dir previews, viewer keys, file and
  directory monitors (`GFileMonitor`, debounced `schedule_refresh()`).
- `orgmode`, `pdf`, `video`, `audio`, `git_tab` — preview renderers / tabs.
- `editor` — editor tabs in the notebook(s): open, save, close, move, dedicated panel.
- `editor_modes` — per-tab mode state, shared editing operations (align,
  format, case, indent, find/replace, goto line), status, key dispatch.
  Mode implementations: `editor_notepad`, `editor_emacs`, `editor_vim`.
  `editor_modes_private.h` (state struct, mode enums) is internal to these 4 files.
- `terminal` — embedded VTE panes (`Ctrl+T`), Dark+ ANSI palette.
- `search` — live search entry (`/`, `n`, `N`) for list and viewer.
- `help` — cheat sheet (`?`); `KEY_HELP` must stay in sync with `README.md`.
- `workspace` — virtual workspaces (Alt+1..8): save/apply layout snapshots,
  top-bar workspace minimap.
- `ui` — fullscreen, focus tracking and cycling, global key handler
  (`on_window_key_press()`, incl. `Ctrl+Q`), pane resize popup, layout toggle.

**4. Entry point:** `main.c` — CLI (`-f`/`--fullscreen`, `-s`/`--size WxH`,
start directory), widget creation and assembly, signal wiring, `GMainLoop`.

### Where to put new code

- Logic that does not need widgets (parsing, classification, path or git
  handling): a foundation module + a test in `tests/test_<module>.c`
  (register it in `TESTS` / `TEST_DEPS_*` in the Makefile, and in
  `tools/check_layers.sh` for its allowed includes).
- A new feature: its own `src/<feature>.c/.h`, not appended to an already
  large file (`notes.c`, `html.c`, `orgmode.c`, `viewer.c` are > 1200 lines).
- Include the header of every module you call, directly. Do not rely on
  transitive includes, and remove includes you no longer need.
- Callbacks used in one file are `static`; never declare another module's
  function locally in a `.c` file — add it to that module's header.
- To refresh the preview after changing the selection programmatically, emit
  the selection `"changed"` signal rather than calling viewer functions from
  lower layers.

### Known coupling (avoid making it worse)

`AppData` is one shared mutable struct; most feature modules call each other
(`update_path_label()`, `workspace_save_current()`, `update_status_bar()`,
`refresh_listing()`), so several pairs include each other (e.g. `file_list` ↔
`viewer`, `editor` ↔ `workspace`). Do not add new cross-calls from layer 1–2
modules into features; `make check` enforces this for the listed modules.

Shortcut list to keep in sync with `README.md` and `help.c` when you change behavior.

## Conventions and hard rules

1. **English only** — every user-visible string, code comment, commit message,
   and documentation file must be in English. No French anywhere.
2. **GTK API correctness**:
   - `GtkScrolledWindow` has one child only (`gtk_scrolled_window_set_child()`).
     Put extra widgets in a `GtkBox`/vbox, or above the pane.
   - `gtk_list_store_set(store, &iter, col, value, -1)`: the terminator is the
     integer `-1`, never the string `"-1"`.
   - `GtkTextBuffer` accepts valid UTF-8 only. Validate content with
     `g_utf8_validate()` / sanitize with `g_utf8_make_valid()` before inserting
     file contents.
   - Change list contents with `gtk_list_store_clear()` + repopulation; the
     tree view holds its own reference to the model, so unref'ing `store` once
     is enough.
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
VS Code Dark+ full theme & icon matching, text viewer padding for text/code files, PDF document preview in right panel, image preview in right panel, monospace font for code files, expandable directory tree (`GtkTreeStore`), rendered Markdown & HTML previews, resizable terminal panes (`GtkPaned`), resizable layout, vim bindings, navigation, hidden toggle,
sort cycle, previews, dark theme. UI language is English end-to-end, including README.md.
