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
| `preview <file> [<kind>]: <ms> (worker, display)` | the viewer, from the selection to the preview on screen (also `LOOKIN_DEBUG_PREVIEW=1`, on stderr) |
| `frame <ms> slow at <total ms>` | a frame over 16.7 ms (one frame at 60 Hz), from the frame clock's `before-paint` to `after-paint` |
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
| CPU while idle | 1 % of a core | `bench-app` (`BENCH_IDLE_CPU`) |
| Frames painted while idle | 1.5 per second | `bench-app` (`BENCH_IDLE_FPS`) |
| Resident memory while idle | 150 MB | `bench-app` (`BENCH_IDLE_RSS_MB`) |
| Slowest preview while browsing (on screen) | 100 ms | `bench-app` (`BENCH_PREVIEW_MS`) |
| Resident memory after browsing | 200 MB | `bench-app` (`BENCH_BROWSE_RSS_MB`) |

`bench-app` also takes `BENCH_RUNS` (start-ups, median; default 3) and
`BENCH_IDLE` (seconds idle per run; default 5).

The idle budgets are targets: the application does not meet them yet
(November 2026: about 4 % of a core, 5 frames painted per second and
190 MB while idle). Lower a budget once the code is below it, never raise
it to make a change pass.
