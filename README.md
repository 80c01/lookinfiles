# 📁 File Explorer in C with GTK 4

Simple graphical application to browse the file system.

## 🎯 Features (current version)

- ✅ Left panel: **real** listing of the current directory (`GDir` enumeration, alphabetical sort, directories first), single "Name" column with exact VS Code-matched **emoji icons** by file type and filename (`.c`, `.cpp`, `.rs`, `.go`, `.py`, `.js`, `.ts`, `.jsx`, `.tsx`, `.vue`, `.html`, `.css`, `.scss`, `Dockerfile`, `.gitignore`, `package.json`, `Cargo.lock`, `LICENSE`, etc.)
- ✅ Expandable directory tree: directories in the left panel can be expanded/collapsed (click expander arrow, press `Space`, `l` / `Right`, or `Enter`), displaying nested contents inline with tree hierarchy lines. Expanding/collapsing is preserved across sort and hidden toggles.
- ✅ Git status badges in Explorer panel: detects Git repositories automatically and marks changed files with VS Code Dark+ color badges adjacent to file and directory names in the treeview (`M` in `#e2c08d` yellow for modified, `U` in `#73c991` green for untracked, `A` in `#73c991` green for added/staged, `D` in `#c74e39` red for deleted, `R` in `#4fc1ff` blue for renamed, and `!` in `#e51400` red for conflicts). Folders containing modified or untracked files roll up the change badge so modified subtrees are immediately identifiable even when collapsed. Badges remain visible beside filenames when the panel is made compact. Updates automatically on disk changes, editor saves, or Git operations (`git commit`, `git add`, `git checkout`).
- ✅ Right panel: content viewer — clicking a file on the left shows its content on the right; selecting a** directory** lists its contents
- ✅ Syntax highlighting in the viewer for text files (optional `GtkSourceView`: C, C++, Python, JS, JSON, XML, Makefile, ...), with a **VS Code Dark+** color palette
- ✅ Tailored Dark theme palette: customized dark styling across panels — `#191A1B` for the file explorer panel with prominent vibrant blue selection (`#0e639c` with bold white text), `#121314` for the content preview / editor panel (with synchronized syntax highlighting and minimap background), and `#1f2021` for the integrated terminal, complemented by VS Code Dark+ syntax colors and icons
- ✅ Monospace font for code files: source code files (`*.c`, `*.py`, `*.js`, `*.sh`, `Makefile`, etc.) are displayed using a clean monospace / fixed-width font family in both the preview viewer and editor tabs, while rendered Markdown and HTML previews retain proportional typography
- ✅ Text viewer padding: text and source code files displayed in the preview panel (as well as editor tabs) feature a slight, comfortable padding (margins) rather than adhering flush to the pane edges, improving reading and editing ergonomics
- ✅ Large files: only a **preview** (first 64 KB) is loaded above 512 KB
- ✅ Non-UTF-8 files are sanitized before display (invalid bytes → U+FFFD)
- ✅ Minimap right of the viewer and editor: VS Code-like scaled overview of the displayed file (mini glyphs, or length-scaled bars for long files), with the viewport shown as a slider — click or drag on it to jump in the file
- ✅ Resizable panel: draggable `GtkPaned` divider (initial position 300 px, compact minimum width 80 px) — while a divider is dragged, a small popup shows both panes' sizes (`W × H | W × H`)
- ✅ Resizable terminal panes: multiple terminal panes (opened via `Ctrl+T` / `Ctrl+Shift+T`) are embedded in resizable `GtkPaned` dividers so each pane and the terminal panel itself can be dynamically resized
- ✅ Navigation in the tree: the `Directory: …` label follows the current directory
- ✅ Top bar shows the current directory, the sort mode and the hidden-files state
- ✅ Markdown & HTML preview: Markdown files (`.md`, `.markdown`, etc.) and HTML files (`.html`, `.htm`, `.xhtml`, `.shtml`) are rendered with rich styling (document title, author & description metadata header, headings, formatted & ordered/nested lists with hanging indent, task list checkboxes, description lists, details/summary disclosure, blockquotes, code blocks with language banners, tables with captions and numeric alignment, base64 data URI & local images, inline formatting, smart links, keyboard keycaps, and extended math/arrow/Greek HTML entities) rather than raw markup; press `m` to toggle between rendered and raw preview
- ✅ Hanging indent for list items: long lines in unordered, ordered, and task list items wrap automatically to fit available width, with wrapped lines and multi-paragraph items (`<p>` within `<li>` or continuation lines) staying cleanly aligned with the item text rather than falling back to the bullet or margin
- ✅ Image preview: image files (`.png`, `.jpg`, `.jpeg`, `.gif`, `.svg`, `.bmp`, `.ico`, `.webp`, `.tiff`, etc.) are rendered directly in the right preview pane with a metadata header (format, native dimensions in pixels, file size) and automatic scaling to fit the window; press `m` to toggle between fitted view and 1:1 original scale
- ✅ PDF document preview: PDF files (`.pdf`) are rendered directly in the right preview pane using `poppler-glib` and Cairo, featuring a metadata banner (title, author, PDF version, page count, file size), sharp page rendering with page dividers, and an `m` toggle to switch between high-resolution rendered pages and extracted text mode (searchable with `/`)
- ✅ Video preview: video files (`.mp4`, `.mkv`, `.webm`, `.mov`, `.avi`, `.flv`, `.wmv`, `.m4v`, `.ts`, `.mpg`, etc.) are previewed directly in the right preview pane using GTK 4's native `GtkVideo` and `GtkMediaStream` player, complete with an aligned Dark+ metadata header (title, container, duration, audio/video streams, file size), transport controls (play/pause, seek timeline, volume), and an `m` toggle to inspect raw file details and binary header hex dump
- ✅ Audio preview: audio files (`.mp3`, `.wav`, `.ogg`, `.flac`, `.aac`, `.m4a`, `.wma`, `.aiff`, `.opus`, etc.) are previewed directly in the right preview pane with a native player card using GTK 4's `GtkMediaControls` and `GtkMediaFile`, featuring a centered player card (title, container, duration, file size), transport controls (play/pause, seek timeline, volume), and an `m` toggle to inspect raw file details and binary header hex dump
- ✅ Git Commits tab in Preview panel: for directories inside a Git repository, a dedicated "📦 Git Commits" tab appears directly to the right of the Preview tab. It displays an interactive table of repository commits (short hash in cyan, commit message, author, and relative date) along with an interactive patch/diff viewer highlighting changes with VS Code Dark+ diff colors (added lines in green, deleted lines in red, hunk headers in blue, metadata in yellow).
- ✅ Dedicated Editor panel: editor tabs can either be docked inside the preview notebook or placed in a dedicated panel positioned to the right of the preview panel (`Ctrl+E` or click the `◫` split button at the top-left of the preview notebook tab strip). When in dedicated mode, browse and preview files on the center panel while editing code on the right panel across resizable dividers, preserved per virtual workspace.
- ✅ Notes view: a dedicated "NOTES" view accessible from the explorer header via the mini `📝` icon or `Ctrl+U`, featuring a mini icon bar in the notes header to create a note (`➕`), create a folder (`📁+`), collapse all (`▲`), and expand all (`▼`), enabling hierarchical organization in folders, instant preview, automatic background saving, and scratchpad editing.
- ✅ Full Bookmark Manager: store, name, and organize bookmarks to directories, files, or web URLs in hierarchical folders with XML persistence (`~/.config/explorateur_fichiers/bookmarks.xml`, with automatic migration of legacy flat bookmarks), inline child counts, missing-location alerts (`⚠`), and right-click context menu.
- ✅ Explorer & Bookmarks mini action bars: dedicated action toolbars positioned right beneath the header across sidebar views:
  - **Files Explorer**: create new file (`📄+`), create new folder (`📁+`), toggle hidden files (`👁`), cycle sort mode (`⇅`), and toggle collapse/expand folders (`▲▼`).
  - **Bookmarks view**: add bookmark (`🔖+` or `a`), create folder (`📁+` or `A`), edit/rename/move (`✎` or `F2`/`r`/`e`), delete (`🗑` or `d`/`Delete`), toggle sort order (`⇅`), and toggle collapse/expand bookmarks (`▲▼`).
  - **Notes view**: create new note (`➕`), create folder (`📁+`), collapse all (`▲`), and expand all (`▼`).
- ✅ Focused panel highlight: the panel that owns active keyboard focus (Explorer sidebar, Preview panel, Editor panel, Terminal panel, or Help cheatsheet) is dynamically highlighted with a subtle VS Code-style blue accent outline (`#0078d4`) and header emphasis, updating live as focus cycles (`Alt+Left` / `Alt+Right`) or when clicking widgets.

## ⌨️ Keyboard shortcuts

### Global

| Key | Action |
|---|---|
| `Ctrl+Q` | Quit the application |
| `f` | Toggle fullscreen (works with either panel focused; `Ctrl+F` keeps its paging role) |
| `Ctrl+Shift+L` | Stack the terminal pane below the viewer, or put it back beside it; the explorer panel stays fixed and the viewer/terminal split ratio is kept |
| `Ctrl+T` | Terminal pane beside the viewer (needs `libvte-2.91-gtk4-dev`): `Ctrl+T` opens a pane right of the previous ones, `Ctrl+Shift+T` opens a new pane below them — each with a fresh shell in the current directory; exit the shell (`Ctrl+D` / `exit`) to close its pane |
| `Alt+Left` / `Alt+Right` | Move the focus between the open panels: explorer panel → viewer → terminal panel (cycles) |

### Explorer panel (when the list has focus)

| Key | Action |
|---|---|
| `j` / `k` | Select next / previous visible entry (navigates through expanded subdirectories) |
| `Space` | Expand / collapse the selected directory |
| `l` / `Right` | Expand the selected directory, or step into its first child |
| `h` / `Left` | Collapse the selected directory, or jump to its parent directory row |
| `Ctrl+F` | Jump one page forward (like Page Down) |
| `Ctrl+B` | Jump one page backward (like Page Up) |
| `gg` | Go to the top of the list |
| `G` | Go to the bottom of the list |
| `zz` | Center the selected entry vertically in the panel |
| `zt` / `zb` | Scroll the selected entry to the top / bottom of the panel |
| `/` | Open the search bar (live search, case-insensitive) |
| `n` / `N` | Next / previous match of the searched term |
| `Enter` / `Double-click` | Expand / collapse directory or bookmark folder, open selected file in editor, open bookmark (directory, file, or URL; `Ctrl+Enter` to enter directory as root) |
| `Backspace` | Go to the parent directory (or return to files view from bookmarks / notes) |
| `Ctrl+D` | Bookmark selected entry or current directory (edit / remove if already bookmarked); add bookmark in bookmarks view |
| `Ctrl+A` | Switch between files and bookmarks views in the explorer panel |
| `a` / `A` | Add bookmark / new folder in the bookmarks view (inside the selected folder) |
| `F2` / `r` / `e` | Edit selected bookmark or folder (name, location, and folder) in bookmarks view |
| `d` / `Delete` | Delete selected bookmark or folder in bookmarks view |
| `J` / `K` (Shift) | Move selected bookmark or folder down / up within its folder |
| `Ctrl+U` | Switch between files and notes view in the explorer panel |
| `r` / `F2` | Rename selected buffer (in Buffers view) or current editor buffer (`Ctrl+R`) |
| `I` | Toggle hidden files (dot-prefixed) |
| `o` | Cycle sort: name → date (newest first) → size (largest first) → extension |
| `m` | Toggle rendered preview / raw markup view (Markdown/HTML), fitted / 1:1 scale (images), rendered pages / text extraction (PDF), or player / raw header mode (video/audio) |
| `?` | Show / hide the key-binding help panel (any key inside it closes it) |
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
| `m` | Toggle rendered preview / raw markup view (Markdown/HTML), fitted / 1:1 scale (images), rendered pages / text extraction (PDF), or player / raw header mode (video/audio) |
| `Space` / `p` | Play / pause media playback when previewing a video or audio file |
| `Esc` | Close the search bar |

### Editor tabs (when the viewer zone shows a file opened for editing)

Double-clicking / pressing `Enter` on a file in the list opens it in a new tab
of the right viewer zone (page "Preview" keeps the read-only browsing view).
Each editor tab features its own interactive minimap. A bullet before
the tab title marks unsaved changes.

| Key | Action |
|---|---|
| `Ctrl+S` | Save the file open in the current editor tab |
| `Ctrl+W` | Close the current editor tab (asks first when unsaved) |
| `Ctrl+E` | Move editor tabs to dedicated panel right of preview, or dock back |
| `Ctrl+R` / `F2` | Rename active buffer tab |

## 📝 Build

```bash
make                           # build with make (recommended)
make bundle                    # create self-contained portable bundle (bundle/explorateur_fichiers)
make static                    # build static binary (requires static GTK 4, or STATIC=1 make)
make clean                     # remove binary and bundles
```

## 🚀 Run

```bash
./explorateur_fichiers                        # windowed mode (default 1000x600)
./explorateur_fichiers --size 1920x1080       # start with specific window size (or -s 1920x1080)
./explorateur_fichiers -f                     # start in fullscreen (same as --fullscreen)
./explorateur_fichiers /path/to/directory     # start in specific directory
```

## 📦 Prerequisites

- GTK 4 Development: `libgtk-4-dev`
- GCC: `gcc`

Install on Debian/Ubuntu:
```bash
sudo apt install libgtk-4-dev build-essential
```

Optional (embedded terminal panel, recommended):
```bash
sudo apt install libvte-2.91-gtk4-dev
```
The Makefile detects it automatically; without it the build still works and
`Ctrl+T` shows a notice instead of the terminal.

Optional (syntax highlighting in the viewer):
```bash
sudo apt install libgtksourceview-5-dev
```
Detected the same way; without it the viewer stays plain text.

Optional (PDF document preview):
```bash
sudo apt install libpoppler-glib-dev
```
Detected the same way; without it, PDF files display an informative placeholder prompting to install the library.

## 🏗️ Project structure

```
src/
├── app_types.h          # Shared structs, enums, constants
├── main.c               # Entry point and widget assembly
├── file_list.c/.h       # Directory enumeration, sorting, navigation
├── viewer.c/.h          # File content viewer and disk monitoring
├── markdown.c/.h        # Markdown rendering engine
├── html.c/.h            # HTML rendering engine
├── pdf.c/.h             # PDF document renderer and text extractor
├── git_tab.c/.h         # Git repository commits tab and diff viewer
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
- **v1.11**: Image preview in right panel: direct rendering of image formats (`.png`, `.jpg`, `.svg`, `.gif`, `.bmp`, `.ico`, `.webp`, etc.) with image metadata (format, dimensions, size), centered layout, automatic window fitting, and `m` toggle for 1:1 original scale
- **v1.12**: PDF preview in right panel: direct document rendering for `.pdf` files using `poppler-glib` and Cairo with metadata banner (title, author, version, page count, file size), sharp page-by-page rendering, text extraction mode via `m` toggle, and dedicated icon (`📕`)
- **v1.13**: Text viewer padding: subtle, comfortable margins for text and source code files in the preview panel and editor tabs for improved readability
- **v1.14**: VS Code Dark+ theme & icon matching: authentic VS Code Dark+ palette across all panels and exact VS Code file icon mappings for languages, frameworks, lockfiles, and config files
- **v1.15**: Custom panel backgrounds & selection highlight: `#191A1B` for the file explorer sidebar with high-contrast active (`#0e639c`) and backdrop (`#094771`) selection with bold white text and comfortable padding, `#121314` for the preview and editor panel (with synchronized GtkSourceView syntax scheme and minimap background), and `#1f2021` for the integrated terminal
- **v1.16**: Enhanced HTML preview rendering: base64 data URI image support (`data:image/...;base64`), code blocks with language extraction and banners, description lists (`<dl>`, `<dt>`, `<dd>`), `<details>`/`<summary>` disclosure, task list checkboxes (`☑`/`☐`), keyboard badges (`<kbd>`), `<abbr>` tooltips, table captions (`<caption>`) & numeric cell alignment, document metadata banner (author & description), smart links, and over 40 math, Greek, and arrow HTML entities
- **v1.17**: Hanging indent and aligned paragraph wrapping for list items: automatic line wrapping in rendered HTML and Markdown previews with hanging indentation (`left-margin` and negative first-line `indent`) across unordered, ordered, and task lists at all nesting depths ($d=0..7$), ensuring wrapped continuation lines and multi-paragraph list items (`<p>` within `<li>` or indented continuation blocks) align under the item text
- **v2.0**: Migrated to **GTK 4** (`gtk4`, `gtksourceview-5`, `vte-2.91-gtk4`) with GSK GPU-accelerated rendering, `GdkPaintable`/`GdkTexture` image & PDF rendering, GTK 4 event controllers (`GtkEventControllerKey`, `GtkGestureClick`, `GtkEventControllerMotion`, `GtkEventControllerFocus`), and non-blocking `GTask` + `GCancellable` background threads for directory scanning, file reading, image scaling, and PDF rasterization
- Compatible with GTK 4.0+
