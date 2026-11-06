# Testing

Details behind the test rules of [AGENTS.md](../AGENTS.md).

## Build, test & run

```bash
make              # incremental build into build/ (gcc -Wall -Wextra, pkg-config gtk4)
make test         # GLib unit tests (headless, no display needed)
make check        # zero-warning build + unit tests + module layering rules
make ui-test      # fast headless UI smoke test, about a second (needs xvfb xdotool)
make ui-shots     # full UI scenario with screenshots (needs xvfb xdotool imagemagick)
make asan         # AddressSanitizer + UBSan build in build-asan/ (crash stacks)
make clean        # remove binary, build/ and build-asan/
./lookinfiles   # run (requires a display; shows a warning without one)
```

- A crash (segfault): run `build-asan/lookinfiles` from `make asan` with
  `ASAN_OPTIONS=detect_leaks=0`; the report names the bad access and both
  stacks (where the memory was freed, where it was used).

- **Agents only run `make` after a change**; the test targets below run when
  the user asks. `make check` fails on any compiler warning, failing test, or
  layering violation (`tools/check_layers.sh`).
- The build compiles every `src/*.c` automatically (no file list to maintain)
  and tracks header dependencies, so editing a header rebuilds its users.
- There is no real display in the sandbox (`Gtk-WARNING: cannot open display`
  is expected when running the binary directly). Logic that can be tested
  without widgets belongs in a foundation module with unit tests.
- `make ui-test` (`tests/ui/smoke.sh`) is the quick check, about a second:
  it starts the app under Xvfb, drives a few steps without screenshots and
  fails on a missing window, a GTK warning or critical, a dirty `Ctrl+Q` exit,
  or a minimap shown / hidden more often than once per change (from the
  `LOOKIN_DEBUG_MINIMAP` log: catches a flicker no screenshot would show).
- `make ui-shots` (`tests/ui/run_ui_test.sh`) checks UI changes in depth. It
  runs the app under Xvfb, drives a keyboard/mouse scenario with xdotool and
  saves one screenshot per step in `build/ui/shots/` — open the relevant ones
  to check the result. It fails on a missing window, any GTK warning or
  critical in the log (e.g. invalid Pango markup), or a dirty `Ctrl+Q` exit.
- To prove a refactoring changes nothing visible: run `make ui-shots` on the
  reference commit (e.g. in a `git worktree`), copy `build/ui/shots` aside,
  then run `UI_BASELINE=<that copy> make ui-shots` on your branch. The terminal
  step is not compared (shell prompt timing varies).
- When adding a feature with a shortcut, add a step to the scenario in
  `tests/ui/run_ui_test.sh` (`key ...; shot <name>`).
