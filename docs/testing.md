# Testing

Details behind the test rules of [AGENTS.md](../AGENTS.md).

## Build, test & run

```bash
make              # incremental build into build/ (gcc -Wall -Wextra, pkg-config gtk4)
make test         # GLib unit tests (headless, no display needed)
make check        # zero-warning build + unit tests + module layering rules
make ui-test      # headless UI smoke test (needs xvfb xdotool imagemagick)
make clean        # remove binary and build/
./lookin   # run (requires a display; shows a warning without one)
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
