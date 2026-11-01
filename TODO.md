# TODO

## Features

### Must do

- [ ] split view: show the same file in two views side by side or stacked, sharing one buffer (today a tab can only be moved to another panel, or another file opened there)
- [ ] emacs: `C-x 0` / `C-x 1` / `C-x 2` / `C-x 3` to close the view, keep only it, or split it below / to the right (needs the split view)
- [ ] OS support: Windows/macOS, maybe using the meson build system (VTE has no Windows port: the terminal would become optional)

### Preview

Rules, coverage and the checklist for a new format: [docs/previews.md](docs/previews.md).

- [ ] JSON / XML / YAML / TOML tree view with folding
- [ ] lay `.docx` text out at the page width (fonts, colours and headers are shown)
- [ ] a placeholder (name, size, kind) when a structured preview takes longer than ~100 ms (none measured over the budget so far)
- [ ] previews of TeX fonts (`.tfm`, `.pfb`) and Python bytecode (`.pyc`), the largest remaining L0 groups
- [ ] folders and messages of Outlook stores (`.pst`, `.ost`, `.dbx`): would need libpff
- [ ] find the cause of the GTK warning "Trying to snapshot GtkGizmo" after wide table previews (see docs/previews.md, "Known issues")

### Wish list

- [ ] change file icons to match VS Code (`.c`, `.h`, `.md` files)

### Won't implement

- container explorer: list containers, connect and list files, open terminal
- systemd services explorer: list services, show status and logs in preview when selecting the service, also on remote machines
