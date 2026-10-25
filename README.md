# 📁 File Explorer in C with GTK+ 3

Simple graphical application to browse the file system.

## 🎯 Features (current version)

- ✅ Left panel: **real** listing of the current directory (`GDir` enumeration, alphabetical sort, directories first), single "Name" column with VS Code-like **emoji icons** by file type
- ✅ Expandable directory tree: directories in the left panel can be expanded/collapsed (click expander arrow, press `Space`, `l` / `Right`, or `Enter`), displaying nested contents inline with tree hierarchy lines. Expanding/collapsing is preserved across sort and hidden toggles.
- ✅ Right panel: content viewer — clicking a file on the left shows its content on the right; selecting a** directory** lists its contents
- ✅ Syntax highlighting in the viewer for text files (optional `GtkSourceView`: C, C++, Python, JS, JSON, XML, Makefile, ...), with a **VS Code Dark+** color palette
- ✅ Monospace font for code files: source code files (`*.c`, `*.py`, `*.js`, `*.sh`, `Makefile`, etc.) are displayed using a clean monospace / fixed-width font family in both the preview viewer and editor tabs, while rendered Markdown and HTML previews retain proportional typography
- ✅ Large files: only a **preview** (first 64 KB) is loaded above 512 KB
- ✅ Non-UTF-8 files are sanitized before display (invalid bytes → U+FFFD)
- ✅ Minimap right of the viewer: VS Code-like scaled overview of the displayed file (mini glyphs, or length-scaled bars for long files), with the viewport shown as a slider — click or drag on it to jump in the file
- ✅ Resizable panel: draggable `GtkPaned` divider (initial position 300 px, minimum width 150 px) — while a divider is dragged, a small popup shows both panes' sizes (`W × H | W × H`)
- ✅ Resizable terminal panes: multiple terminal panes (opened via `Ctrl+T` / `Ctrl+Shift+T`) are embedded in resizable `GtkPaned` dividers so each pane and the terminal panel itself can be dynamically resized
- ✅ Navigation in the tree: the `Directory: …` label follows the current directory
- ✅ Top bar shows the current directory, the sort mode and the hidden-files state
- ✅ Markdown & HTML preview: Markdown files (`.md`, `.markdown`, etc.) and HTML files (`.html`, `.htm`, `.xhtml`, `.shtml`) are rendered with rich styling (document title, headings, formatted lists, blockquotes, code blocks, tables with Unicode borders, inline formatting, links, and images) rather than raw markup; press `m` to toggle between rendered and raw preview

## ⌨️ Keyboard shortcuts

### Global

| Key | Action |
|---|---|
| `Ctrl+Q` | Quit the application |
| `f` | Toggle fullscreen (works with either panel focused; `Ctrl+F` keeps its paging role) |
| `Ctrl+Shift+L` | Stack the terminal pane below the viewer, or put it back beside it; the file list panel stays fixed and the viewer/terminal split ratio is kept |
| `Ctrl+T` | Terminal pane beside the viewer (needs `libvte-2.91-dev`): `Ctrl+T` opens a pane right of the previous ones, `Ctrl+Shift+T` opens a new pane below them — each with a fresh shell in the current directory; exit the shell (`Ctrl+D` / `exit`) to close its pane |
| `Alt+Left` / `Alt+Right` | Move the focus between the open panels: file list → viewer → terminal panel (cycles) |

### Left panel (when the list has focus)

| Key | Action |
|---|---|
| `j` / `k` | Select next / previous visible entry (navigates through expanded subdirectories) |
| `Space` | Expand / collapse the selected directory |
| `l` / `Right` | Expand the selected directory, or step into its first child |
| `Left` | Collapse the selected directory, or jump to its parent directory row |
| `Ctrl+F` | Jump one page forward (like Page Down) |
| `Ctrl+B` | Jump one page backward (like Page Up) |
| `gg` | Go to the top of the list |
| `G` | Go to the bottom of the list |
| `/` | Open the search bar (live search, case-insensitive) |
| `n` / `N` | Next / previous match of the searched term |
| `Enter` / `Double-click` | Expand / collapse directory, or open selected file in a new editing tab of the right panel (`Ctrl+Enter` to enter directory as root) |
| `Backspace` | Go to the parent directory |
| `I` | Toggle hidden files (dot-prefixed) |
| `o` | Cycle sort: name → date (newest first) → size (largest first) → extension |
| `m` | Toggle rendered preview / raw markup view (for Markdown and HTML files) |
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
| `m` | Toggle rendered preview / raw markup view (for Markdown and HTML files) |
| `Esc` | Close the search bar |

### Editor tabs (when the viewer zone shows a file opened for editing)

Double-clicking / pressing `Enter` on a file in the list opens it in a new tab
of the right viewer zone (page "Preview" keeps the read-only browsing view;
the minimap stays in place while it is the selected page). A bullet before
the tab title marks unsaved changes.

| Key | Action |
|---|---|
| `Ctrl+S` | Save the file open in the current editor tab |
| `Ctrl+W` | Close the current editor tab (asks first when unsaved) |

## 📝 Build

```bash
make                           # build with make (recommended)
make clean                     # remove binary
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
src/
├── app_types.h          # Shared structs, enums, constants
├── main.c               # Entry point and widget assembly
├── file_list.c/.h       # Directory enumeration, sorting, navigation
├── viewer.c/.h          # File content viewer and disk monitoring
├── markdown.c/.h        # Markdown rendering engine
├── html.c/.h            # HTML rendering engine
├── editor.c/.h          # Multi-tab file editor
├── minimap.c/.h         # Scaled file overview minimap
├── terminal.c/.h        # Embedded VTE terminal panel
├── search.c/.h          # Interactive search bar
└── ui.c/.h              # Paned layouts, help popup, fullscreen, borders
Makefile                 # Build configuration
README.md                # Documentation
AGENTS.md                # AI coding agent guidelines
explorateur_fichiers     # Compiled binary
```

## 📝 Release notes

- **v1.0**: Base version with two main panels
- **v1.1**: Real current-directory listing, resizable panel (`GtkPaned`), content viewer linked to selection, `Ctrl+Q`
- **v1.2**: Vim keybindings (`j`/`k`, `gg`, `G`, `/`, `n`/`N`) and tree navigation (`Enter`/`Backspace`)
- **v1.3**: Hidden-files toggle (`I`), sort cycling (`o`), directory listing in the right panel, preview of large files, dark theme
- **v1.4**: VS Code-like emoji icons per file type in the tree, `Ctrl+T` embedded terminal panel, `-f` fullscreen start, `Alt+←/→` panel focus cycling, read-only viewer with vim bindings, in-content search and optional syntax highlighting
- **v1.5**: Rendered Markdown preview in right panel (headings, task lists, tables, styled quotes, code blocks, inline formatting, and images) with `m` toggle for raw source preview
- **v1.6**: Modular code architecture: split monolithic codebase into dedicated modules in `src/` (`main`, `file_list`, `viewer`, `markdown`, `editor`, `minimap`, `terminal`, `search`, `ui`)
- **v1.7**: Rich HTML preview rendering in right panel (document title, headings, lists, blockquotes, preformatted code, Unicode border tables, inline formatting, links, and images) with `m` toggle for raw markup view
- **v1.8**: Resizable multi-terminal layout: dynamic `GtkPaned` split hierarchy for side-by-side (`Ctrl+T`) and stacked (`Ctrl+Shift+T`) terminals, allowing smooth divider resizing across multiple open terminal panes
- **v1.9**: Expandable & collapsible directory tree: hierarchical `GtkTreeStore` with lazy-loading subdirectories, folder icons (`📁` / `📂`), tree hierarchy guide lines, keyboard expand/collapse controls (`Space`, `l`/`Right`, `Left`, `Enter`), and multi-level tree navigation
- **v1.10**: Monospace font for code files: automatic fixed-width font styling for source code files (`*.c`, `*.py`, etc.) and raw markup views in both the right-pane viewer and editor tabs
- Compatible with GTK+ 3.10+ (needed for `GtkSearchEntry`)
