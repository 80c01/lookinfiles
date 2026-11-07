# TODO

## Features

### Must do

- [ ] OS support: Windows/macOS, maybe using the meson build system (VTE has no Windows port: the terminal would become optional)

### Preview

Rules, coverage and the checklist for a new format: [docs/previews.md](docs/previews.md).

- [ ] JSON / XML / YAML / TOML tree view with folding
- [ ] lay `.docx` text out at the page width (fonts, colours and headers are shown)
- [ ] a placeholder (name, size, kind) when a structured preview takes longer than ~100 ms (none measured over the budget so far)
- [x] previews of TeX fonts (`.tfm`, `.pfb`) and Python bytecode (`.pyc`), the largest remaining L0 groups
- [ ] folders and messages of Outlook stores (`.pst`, `.ost`, `.dbx`): would need libpff
- [ ] find the cause of the GTK warning "Trying to snapshot GtkGizmo" after wide table previews (see docs/previews.md, "Known issues")

### Canvas editor

Excalidraw-like rework. Done: floating tool bar (theme icons), properties
panel, mouse drawing (shapes, arrows, lines, pencil, eraser), tool keys
(commit 757c298). Code: `canvas_view.c`, `canvas_toolbar.c`,
`canvas_props.c`, `canvas_draw.c`, `canvas_rough_draw.c`, `canvas_history.c`,
`canvas_viewport.c`, and the page split into `canvas_view.c` (setup, API),
`canvas_page.c` (core), `canvas_keys.c`, `canvas_drag.c`. Next, in this order:

- [x] undo / redo (`Ctrl+Z` / `Ctrl+Shift+Z`): add, delete, move, resize,
  style changes; text being edited keeps its own GtkTextBuffer history
- [x] zoom (`Ctrl+wheel`, `Ctrl+0` back to 100 %) and pan (Space + drag or
  middle button)
- [x] multi-selection (rubber band, `Shift+click`): move, delete, align
  together (the align items of the `⋯` menu then become useful)
- [x] arrows bound to shapes (they follow when a shape moves)
- [x] text inside shapes (double-click a shape to type in it); a label
  longer than its shape is cut, the shape does not grow yet
- [x] floating text-format bar near the selection; the left panel keeps the
  shape style only
- [x] small items: duplicate (`Ctrl+D`), copy / paste of shapes, lock a
  block (`Ctrl+Shift+K`: `Ctrl+Shift+L` is the window layout), bring to
  front / send to back, export the canvas as PNG / SVG (tool bar menu; no
  UI test step, the menu has no key)
- [x] run `make ui-shots` and check the new canvas screenshots
  (`canvas_tool_*`, `canvas_text_bold` (the format bar), `canvas_rect_drawn`, `canvas_pencil_drawn`,
  `canvas_checkbox_toggled`, `canvas_undo_delete`, `canvas_redo_delete`, `canvas_zoom_in`,
  `canvas_zoom_reset`, `canvas_band_*`, `canvas_group_*`, `canvas_shift_click`, `canvas_arrow_*`, `canvas_shape_text_*`, `canvas_duplicate`, `canvas_paste`,
  `canvas_send_to_back`, `canvas_locked`, `canvas_unlocked`);
  checked: each step shows the expected state

### Wish list

- [ ] change file icons to match VS Code (`.c`, `.h`, `.md` files)

### Won't implement

- container explorer: list containers, connect and list files, open terminal
- systemd services explorer: list services, show status and logs in preview when selecting the service, also on remote machines
