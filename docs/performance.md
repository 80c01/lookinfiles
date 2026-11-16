# Performance

How LookinFiles is measured, and the budgets it must keep. The rules behind
them are in [AGENTS.md](../AGENTS.md) (hard rule 8) and
[previews.md](previews.md) (preview latency).

## Measure first

```bash
make bench           # the three benchmarks below, in turn
make bench-scan      # explorer: listing and rows of a folder of 100 000 entries (headless)
make bench-preview   # every structured preview on the fixtures (headless, docs/previews.md)
make bench-app       # the application under Xvfb: start-up, idle cost, browsing
LOOKIN_PROFILE=1 ./lookinfiles          # timings on stderr
LOOKIN_PROFILE=/tmp/p.log ./lookinfiles # ... or in a file
```

Each benchmark prints its results and fails when one exceeds its budget.
Run the one that matches a change before and after it, on the same machine:
the numbers are comparable between two runs, not between two machines.
`bench-app` draws on Xvfb with the CPU renderer (`GSK_RENDERER=cairo`), a
slow display: its frame times are higher than on a real screen.

## `LOOKIN_PROFILE`

The probe of `core/profile` (layer 1), off unless the variable is set: every
call then returns at once, so the probes stay in release builds. One line per
event, all starting with `profile: `:

| Line | Written by |
|---|---|
| `mark <total ms> +<since last> <label>` | start-up steps of `main.c` (`profile_mark()`); `first frame` and `startup done` (window drawn, main loop idle) by `widgets/frame_watch` |
| `time <ms> <label>` | timed operations (`profile_start()` / `profile_done()`, also from worker threads): directory scans and rows (`file_list.c`), git status |
| `preview <file> [<kind>]: <ms> (worker, display)` | the viewer and the folder overview (`dir_preview.c`, its 50 ms debounce included), from the selection to the preview on screen (the viewer's also with `LOOKIN_DEBUG_PREVIEW=1`, on stderr) |
| `frame <ms> at <total ms>[ slow][ unpainted]` | each frame, from the frame clock's `before-paint` to `after-paint`; `slow` over 16.7 ms (one frame at 60 Hz), `unpainted` when nothing was drawn |
| `report key=value...` | at exit: uptime, CPU time, resident and peak memory, frames of the start-up and after it (painted, without drawing, slow, worst, mean) |

To time something new: `gint64 t = profile_start(); ...; profile_done(t,
"what %s", name);` (include `core/profile.h`; a module of any layer may).

## Budgets

| Measure | Budget | Benchmark (variable to change it) |
|---|---|---|
| List 100 000 entries, sorted by name | 300 ms | `bench-scan` (`BENCH_SCAN_MS`) |
| Rows of 100 000 entries | 100 ms | `bench-scan` (`BENCH_ROWS_MS`) |
| Memory per explorer entry (listing + row) | 700 B | `bench-scan` (`BENCH_ROW_BYTES`) |
| Text preview / binary or rendered preview (worker) | 50 ms / 150 ms | `bench-preview` |
| Start-up to idle | 600 ms | `bench-app` (`BENCH_STARTUP_MS`) |
| CPU in the 2 s after the start-up (end of the loading) | 100 ms | `bench-app` (`BENCH_SETTLE_CPU_MS`) |
| CPU while idle | 0.5 % of a core | `bench-app` (`BENCH_IDLE_CPU`) |
| Frames painted while idle | 2 per second (the default top bar: the running timer once a second, the CPU chart every 2 s) | `bench-app` (`BENCH_IDLE_FPS`) |
| Resident memory while idle | 150 MB | `bench-app` (`BENCH_IDLE_RSS_MB`) |
| Slowest preview while browsing (on screen) | 100 ms | `bench-app` (`BENCH_PREVIEW_MS`) |
| Resident memory after browsing | 200 MB | `bench-app` (`BENCH_BROWSE_RSS_MB`) |

`bench-app` also takes `BENCH_RUNS` (start-ups, median; default 3),
`BENCH_SETTLE` (seconds after the start-up counted as the end of the
loading, not as idle; default 2) and `BENCH_IDLE` (seconds idle after that;
default 5).

The memory budgets are targets: the application does not meet them yet
(November 2026: about 170 MB resident while idle). Lower a budget once the
code is below it, never raise it to make a change pass.

## Rules that keep it light

- **Nothing hidden works**: a tab or view not on screen must not load its
  content; it marks it stale and loads it when shown. Example: the Git tab
  reads the history at its `map` (`git_tab.c`); it took 190 ms of CPU at
  each start-up before.
- **No wake-up without a change on screen**: a timer must run only while
  what it updates is shown, and set a label only when its text changes.
