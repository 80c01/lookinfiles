# 📁 File Explorer in C with GTK+ 3

Simple graphical application to browse the file system.

## 🎯 Features (current version)

- ✅ Left panel: **real** listing of the current directory (`GDir` enumeration, alphabetical sort, directories first), single "Name" column with VS Code-like **emoji icons** by file type
- ✅ Right panel: content viewer — clicking a file on the left shows its content on the right; selecting a** directory** lists its contents
- ✅ Syntax highlighting in the viewer for text files (optional `GtkSourceView`: C, C++, Python, JS, JSON, XML, Makefile, ...), with a **VS Code Dark+** color palette
- ✅ Large files: only a **preview** (first 64 KB) is loaded above 512 KB
- ✅ Non-UTF-8 files are sanitized before display (invalid bytes → U+FFFD)
- ✅ Resizable panel: draggable `GtkPaned` divider (initial position 300 px, minimum width 150 px)
- ✅ Navigation in the tree: the `Directory: …` label follows the current directory
- ✅ Top bar shows the current directory, the sort mode and the hidden-files state

## ⌨️ Keyboard shortcuts

### Global

| Key | Action |
|---|---|
| `Ctrl+Q` | Quit the application |
| `Ctrl+T` | Toggle a terminal panel right of the viewer (needs `libvte-2.91-dev`; each press spawns a shell in the current directory, `Ctrl+D` exits the shell) |
| `Alt+Left` / `Alt+Right` | Move the focus between the open panels: file list → viewer → terminal panel (cycles) |

### Left panel (when the list has focus)

| Key | Action |
|---|---|
| `j` / `k` | Select next / previous entry |
| `Ctrl+F` | Jump one page forward (like Page Down) |
| `Ctrl+B` | Jump one page backward (like Page Up) |
| `gg` | Go to the top of the list |
| `G` | Go to the bottom of the list |
| `/` | Open the search bar (live search, case-insensitive) |
| `n` / `N` | Next / previous match of the searched term |
| `Enter` | Enter the selected directory |
| `Double-click` | Enter the double-clicked directory (same as `Enter`) |
| `Backspace` | Go to the parent directory |
| `I` | Toggle hidden files (dot-prefixed) |
| `o` | Cycle sort: name → date (newest first) → size (largest first) → extension |
| `h` | Show / hide the key-binding help panel (any key inside it closes it) |
| `Esc` | Close the search bar |

### Right panel (when the viewer has focus)

Same vim bindings as the list, applied to the displayed content:

| Key | Action |
|---|---|
| `j` / `k` | Scroll one line down / up |
| `Ctrl+F` | Scroll one page forward (like Page Down) |
| `Ctrl+B` | Scroll one page backward (like Page Up) |
| `gg` | Go to the top of the file |
| `G` | Go to the bottom of the file |
| `/` | Open the search bar (searches in the displayed content) |
| `n` / `N` | Next / previous occurrence (selected and centered, wraps around) |
| `Esc` | Close the search bar |

## 📝 Build

```bash
make                           # or
gcc -o explorateur_fichiers explorateur_fichiers.c `pkg-config --cflags --libs gtk+-3.0`
```

## 🚀 Run

```bash
./explorateur_fichiers            # windowed mode (default)
./explorateur_fichiers -f         # start in fullscreen (same as --fullscreen)
```

## 📦 Prerequisites

- GTK+ 3 Development: `libgtk-3-dev`
- GCC: `gcc`

Install on Debian/Ubuntu:
```bash
sudo apt install libgtk-3-dev build-essential
```

Optional (embedded terminal panel, recommended):
```bash
sudo apt install libvte-2.91-dev
```
The Makefile detects it automatically; without it the build still works and
`Ctrl+T` shows a notice instead of the terminal.

Optional (syntax highlighting in the viewer):
```bash
sudo apt install libgtksourceview-3.0-dev
```
Detected the same way; without it the viewer stays plain text.

## 🏗️ Project structure

```
explorateur_fichiers.c  # Main source file
Makefile                 # Build file
README.md                # Documentation
explorateur_fichiers     # Compiled binary
```

## 📝 Release notes

- **v1.0**: Base version with two main panels
- **v1.1**: Real current-directory listing, resizable panel (`GtkPaned`), content viewer linked to selection, `Ctrl+Q`
- **v1.2**: Vim keybindings (`j`/`k`, `gg`, `G`, `/`, `n`/`N`) and tree navigation (`Enter`/`Backspace`)
- **v1.3**: Hidden-files toggle (`I`), sort cycling (`o`), directory listing in the right panel, preview of large files, dark theme
- **v1.4**: VS Code-like emoji icons per file type in the tree, `Ctrl+T` embedded terminal panel, `-f` fullscreen start, `Alt+←/→` panel focus cycling, read-only viewer with vim bindings, in-content search and optional syntax highlighting
- Compatible with GTK+ 3.10+ (needed for `GtkSearchEntry`)
