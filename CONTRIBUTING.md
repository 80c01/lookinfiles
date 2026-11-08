# Contributing to LookinFiles

Thanks for your interest. Bug reports, preview formats, fixes and
documentation are all welcome.

## Reporting a bug or asking for a feature

Open an [issue](https://github.com/80c01/lookinfiles/issues/new/choose) and
fill in the form. For a bug, give the output of `lookinfiles --version`,
your system and, when built from source, the output of `make deps`.
Security issues go through a private report instead (see
[SECURITY.md](SECURITY.md)).

## Building

See [Installation](README.md#installation) for the libraries. Then:

```bash
make -j"$(nproc)"        # build ./lookinfiles
make deps                # list required / optional libraries found
make -j"$(nproc)" check  # zero warnings, unit tests, layering rules
make ui-test             # fast headless UI smoke test (needs xvfb, xdotool)
make ui-shots            # full UI scenario with screenshots (also imagemagick)
```

## Before you write code

Read [docs/architecture.md](docs/architecture.md) (modules, layers, the event
bus) and [docs/ui-conventions.md](docs/ui-conventions.md) (keys, menus,
dialogs, colours, fonts). A new preview format follows
[docs/previews.md](docs/previews.md).

The main rules, all checked by `make check` where possible:

- C99, GTK 4.12 or newer, no deprecated GTK API, `-Wall -Wextra` clean.
- A module depends only on its own layer or below; no new include cycles.
- Logic without widgets goes in a foundation module with a unit test in
  `tests/test_<module>.c`.
- No top-level windows: prompts are `AppDialog`s, menus are `GtkPopover`s.
- Colours come from the palette and fonts from the font roles: no colour
  literals, font families or pixel sizes in the code.
- The preview reads only the first `PREVIEW_BYTES` of a file; nothing slow
  (recursive scans, blocking git calls) on the UI thread.
- Keep files under 1200 lines: a new feature gets its own module.
- English only, in code, comments, commits and docs.

## Shortcuts

Use the same key for the same action in every explorer view. A shortcut
change updates [docs/keyboard.md](docs/keyboard.md) and `KEY_HELP` in
`src/chrome/help.c` together, and a new shortcut gets a step in
`tests/ui/run_ui_test.sh`. `Ctrl+Q` must always quit.

## Pull requests

- One change per pull request, one focused commit per change, with a short
  imperative subject (`Add a sort by extension`, `Fix ...`).
- Add an entry under **Unreleased** in [CHANGELOG.md](CHANGELOG.md).
- `make check` must pass; run `make ui-test` when you touch the UI.

By contributing, you agree that your work is released under the
[MIT License](LICENSE).
