# AGENTS.md

Guidelines for AI coding agents working in this repository.

`Lookin` ("look in your files") is a file explorer in C with GTK 4 (4.12+,
no deprecated API). Optional deps: VTE for GTK 4, GtkSourceView 5,
poppler-glib. Left = explorer panel (`GtkListView` over a `GtkTreeListModel`
of `ExplorerRow`); right = preview notebook and terminal panes; all inside
resizable `GtkPaned`s.

More detail, read when the task touches it:
- [docs/architecture.md](docs/architecture.md): every module by layer, the
  event bus, accepted include cycles.
- [docs/ui-conventions.md](docs/ui-conventions.md): keys, tooltips, menus,
  dialogs, empty states, icons (and how to add one), colours, fonts.
- [docs/testing.md](docs/testing.md): UI test scenario, screenshot baselines.
- [docs/previews.md](docs/previews.md): preview rules, coverage, adding a
  format.
- [docs/code-nav.md](docs/code-nav.md): code navigation (backends, keys).

## Build and test

```bash
make            # build into build/
make test       # GLib unit tests (headless)
make check      # zero warnings + unit tests + layering rules
make ui-test    # headless UI smoke test under Xvfb, screenshots in build/ui/shots/
./lookin        # needs a display ("cannot open display" is expected in the sandbox)
```

- **After a modification, only run `make`** and fix every warning and error.
- **Run `make test`, `make check` or `make ui-test` only when the user asks.**
  Not after an edit, not before a commit. The "`make check` passes" and
  `make ui-test` items in the docs checklists also wait for the user.
- A new shortcut gets a step in `tests/ui/run_ui_test.sh` (`key ...; shot <name>`).
- `lookin` and `build/` are generated: never edit them.

## Architecture

Modules are `src/<module>.c` + `.h`. A module may depend only on its own
layer or below (`tools/check_layers.sh` enforces it):

1. **Foundation**: pure helpers, no `AppData`, no widgets, unit-tested.
2. **Shared types and helpers**: `app_types.h`, events, leaf widgets; layer 1 only.
3. **Features**: explorer views, viewer, editor, terminal, search...
4. **Window chrome**: top bar, status bar, sidebar, help, workspaces, themes, `ui`.
5. **Entry point**: `main.c`.

- Lower layers never call the chrome: they `app_emit()` an `AppEvent`
  (subscribers wired in `main.c`). To re-render the preview, emit
  `APP_EVENT_PREVIEW_STALE`; to select a row, use `select_row()`.
- Logic without widgets goes in a layer 1–2 module with
  `tests/test_<module>.c` (register it in the Makefile `TESTS` / `TEST_DEPS_*`
  and in `tools/check_layers.sh`).
- A new feature gets its own module; do not grow files over 1200 lines
  (`notes.c`, `html.c`, `orgmode.c`, `viewer.c`).
- Include the header of every module you call; drop unused includes.
  One-file callbacks are `static`; never declare another module's function
  locally.
- No new include cycles (`make check` fails on them).
- Never add a format-specific branch to the viewer: formats go through
  `preview_formats` (see docs/previews.md).

## Hard rules

1. **English only**: strings, comments, commits, docs.
2. **No deprecated GTK API**: lists are `GListStore` + `GtkListView` /
   `GtkColumnView`; dialogs are `AppDialog` (`dialog.h`); URIs open with
   `GtkUriLauncher`; CSS providers go on the display.
3. **No external windows**: prompts are `AppDialog`s, menus `GtkPopover`s
   (`make check` rejects top-level windows outside `main.c`).
4. `GtkScrolledWindow` has one child. `GtkTextBuffer` takes valid UTF-8 only
   (`g_utf8_validate()` / `g_utf8_make_valid()`).
5. Escape text put into Pango markup with `g_markup_printf_escaped()`.
6. **Colours from the palette** (`palette_color(PAL_...)`, `@role` in CSS),
   **fonts from the font roles** (`fonts.h`, `@font_*` / `@size_*`); text tags
   via `text_tag_create()` / `text_tag_ensure()` with role values. `make check`
   rejects colour literals, font families and pixel sizes elsewhere.
7. Every new `GtkPaned` goes through `wire_paned_controllers()` or
   `paned_use_thin_handle()`. Keep the `GtkPaned` layout (resizability is
   required).
8. **Performance**: preview reads only `PREVIEW_BYTES`; no recursive scans
   or blocking git calls on the UI thread.
9. The top bar (`update_path_label()`) is the single source of truth for
   directory, sort and hidden state.
10. `Ctrl+Q` must keep quitting: never swallow it in a key handler.
11. Shortcut changes: keep `README.md` and `help.c` (`KEY_HELP`) in sync, and
    use the same key for the same action in every explorer view.
12. C99, `-Wall -Wextra` clean.

## Git

- **Always commit**: once a change builds with `make`, commit it right away,
  without waiting to be asked.
- One accepted change = one commit; short imperative subject, no body
  (e.g. `Add Ctrl-F / Ctrl-B paging in left panel`).
- `git add` only files you changed on purpose. Amend only your own
  immediately-prior commit.
