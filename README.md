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
- ✅ Dedicated & Split Editor panels: editor tabs can either be docked inside the preview notebook or placed in dedicated panels (`Ctrl+E` or click the `◫` split button). Any editor tab can be split or moved in any direction—right, left, down, up (`Ctrl+Alt+Arrows` or `H/J/K/L`) or via right-click tab context menu—creating multi-pane split layouts across resizable dividers, preserved per virtual workspace. Empty panels collapse automatically.
- ✅ Notes & Notebooks Manager: a dedicated "NOTES" view accessible from the explorer header via the mini `📝` icon or `Ctrl+U`. Organizes notes hierarchically into notebooks (carnets de notes) and includes a trash bin (corbeille) with XML persistence (`~/.config/explorateur_fichiers/notes/notes.xml`):
  - **3 Note Types**:
    - **Plain Text (`📄 .txt`)**: lightweight unformatted text files.
    - **Rich Format (`✨ .md`)**: rich Markdown with formatted headings, bold/italic, blockquotes, code blocks, tables, task lists, and embedded local or base64 images.
    - **Org-mode (`🦄 .org`)**: authentic Emacs Org-mode display and behavior — interactive heading visibility folding (`Tab` / `Shift+Tab`), TODO keyword cycling (`TODO` -> `NEXT` -> `DONE` -> none), interactive checkboxes (`[ ]` <-> `[X]`) with parent progress cookie updates (`[1/2]` or `[50%]`), planning & timestamps (`SCHEDULED`, `DEADLINE`), drawers (`:PROPERTIES:`), code blocks (`#+BEGIN_SRC`), styled tables, and inline image rendering (`[[file:image.png]]`).
  - **Notebooks**: create, rename, reorder, and delete notebooks with custom icons (`📓`, `💼`, `💡`, `📚`, etc.).
  - **Trash Bin**: soft-delete notes and entire notebooks into `.trash/`, track original notebook origins, restore trashed notes back to their original notebook (`Shift+R`), or empty the trash bin (`🧹`).
  - **Note Management**: create new notes with format selection (`➕` or `a`), rename and convert note formats (`✎` or `F2`/`r`), move notes between notebooks (`📁→` or `m`), 1-click format conversion via context menu, and open in editor tabs with automatic background saving.
  - **Manual Ordering & Sorting**: place notes and notebooks in any custom order using `Shift+K` (up) and `Shift+J` (down), or cycle sorting modes (`⇅`: Manual, A-Z, Z-A).
  - **Context Menus**: comprehensive secondary-click context menus for notes, notebooks, and the trash bin.
- ✅ Full Bookmark Manager: store, name, and organize bookmarks to directories, files, or web URLs in hierarchical folders with XML persistence (`~/.config/explorateur_fichiers/bookmarks.xml`, with automatic migration of legacy flat bookmarks), inline child counts, missing-location alerts (`⚠`), and right-click context menu.
- ✅ Remote SSH Mode: a dedicated "REMOTE" mode accessible from the explorer header via the mini `🌐` icon or `Ctrl+Shift+R`. Browse, preview, and edit files on remote SSH servers as smoothly as local files:
  - **Machine Manager**: auto-discovers SSH servers from `~/.ssh/config` and supports adding, editing, and deleting custom remote hosts with XML persistence (`~/.config/explorateur_fichiers/remotes.xml`).
  - **Remote Browsing via SSHFS**: connecting mounts the remote machine via SSHFS and switches directly to the Explorer file tree view, enabling tree navigation, file previews, and full multi-tab editing on the remote server.
  - **Remote Terminal Integration**: in any workspace connected to a remote host, the terminal button and `Ctrl+T` / `Ctrl+Shift+T` automatically launch an interactive SSH shell directly into the remote machine.
  - **Per-Workspace Remote Binding**: virtual layouts/workspaces remember remote connections, allowing seamless switching between local and remote development environments.
- ✅ Explorer, Bookmarks, Notes & Remote mini action bars: dedicated action toolbars positioned right beneath the header across sidebar views:
  - **Files Explorer**: create new file (`📄+`), create new folder (`📁+`), toggle hidden files (`👁`), cycle sort mode (`⇅`), and toggle collapse/expand folders (`▲▼`).
  - **Bookmarks view**: add bookmark (`🔖+` or `a`), create folder (`📁+` or `A`), edit/rename/move (`✎` or `F2`/`r`/`e`), delete (`🗑` or `d`/`Delete`), toggle sort order (`⇅`), and toggle collapse/expand bookmarks (`▲▼`).
  - **Notes view**: create note (`➕` or `a`), create notebook (`📓+` or `Shift+A`), rename (`✎` or `F2`/`r`), move to notebook (`📁→` or `m`), delete to trash (`🗑` or `d`/`Delete`), toggle sort order (`⇅`), toggle collapse/expand (`▲▼`), and empty trash (`🧹`).
  - **Remote view**: connect/mount (`🔌` or `c`/`Enter`), open remote SSH terminal (`>_` or `t`), add machine (`➕` or `a`), edit machine (`✎` or `F2`/`r`/`e`), delete machine (`🗑` or `d`/`Delete`), and re-scan `~/.ssh/config` (`↻` or `F5`).
- ✅ Focused panel highlight: the panel that owns active keyboard focus (Explorer sidebar, Preview panel, Editor panel, Terminal panel, or Help cheatsheet) is dynamically highlighted with a subtle VS Code-style blue accent outline (`#0078d4`) and header emphasis, updating live as focus cycles (`Alt+Left` / `Alt+Right`) or when clicking widgets.
- ✅ Multi-Mode Editor Engine (Notepad, Emacs, Vim) with Visual Clones:
  Each editor tab supports switching between 3 distinct editor modes, complete with visual clone fidelity and full feature parity:
  - **Notepad Mode (Default)**: Windows Notepad aesthetic with clean bottom status bar (`Ln X, Col Y | 100% | Windows (CRLF) | UTF-8`), docked search & replace bar (`Ctrl+F` / `Ctrl+H`), in-app Save As bar (`Ctrl+Shift+S`), paragraph alignment shortcuts (`Ctrl+L/E/R/J`), undo/redo (`Ctrl+Z` / `Ctrl+Y`), and goto line (`Ctrl+G`).
  - **Emacs Mode (Visual Clone of GNU Emacs)**: Authentic GNU Emacs mode-line (`-U:---  *filename*  (Text Fill)  Top  L1 C0  ----...`) with 3D/shadow border styling and live minibuffer / echo area at the bottom. Full Emacs editing model: `C-x C-s` (save buffer), `C-x C-w` (write file / Save As with chosen name), `C-w` (kill region), `M-w` (kill ring save), `C-y` (yank), `C-d` / `M-d` / `C-k` (delete char, kill word, kill line), `C-SPC` (set mark), `C-s` / `C-r` (I-search forward/backward in minibuffer), `M-%` (query replace in minibuffer), `M-q` (fill paragraph 72 cols), `M-u`/`M-l`/`M-c` (case changes), `C-/` / `C-Shift-_` (undo/redo), `C-f`/`C-b`/`C-n`/`C-p` (navigation), `M-g g` (goto line), and `M-x` (command prompt).
  - **Vim Mode (Visual Clone of Vim)**: Authentic Vim inverted statusline with mode badges (`-- NORMAL --`, `-- INSERT --`, `-- VISUAL --`, `-- VISUAL LINE --`, `COMMAND`), file status (`"file.txt" [+] [utf-8]`), ruler (`line,col  percentage`), dynamic cursor styling (block cursor in Normal/Visual, I-beam in Insert), and bottom command-line / echo area (`:`, `/`, `?`). Full modal editing: `i`/`a`/`o`/`O`/`s`/`S`/`C` (insert), `Esc` / `Ctrl+[` (normal mode), `x`/`dd`/`dw`/`d$`/`D` (delete/cut), `yy`/`yw` (yank), `p`/`P` (put), `v`/`V` (visual selection), `h`/`j`/`k`/`l`/`w`/`b`/`e`/`0`/`^`/`$`/`gg`/`G` (navigation), `/` and `?` (search forward/backward with `n`/`N`/`*`), `:%s/old/new/g` (substitute/replace), `==`/`>>`/`<<`/`gq` (auto-indent and paragraph format), `:center [width]`/`:right [width]`/`:left` (alignment), `u`/`Ctrl+R` (undo/redo), `:w`/`:w <file>`/`:saveas <file>` (save and save as with chosen file name), and `ZZ`/`:wq`/`:q`/`:q!` (quit).
  - **Seamless Mode Switching**: switch anytime via the bottom status bar mode popover button, tab header right-click context menu, Vim `:set mode=vim/emacs/notepad`, or Emacs `M-x vim-mode/emacs-mode/notepad-mode`.

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
| `Ctrl+Shift+R` | Switch between files and remote SSH machines view in the explorer panel |
| `a` / `A` | In Notes view: create new note (`a`) / new notebook with custom icon (`Shift+A`) |
| `F2` / `r` / `e` | In Notes view: edit/rename note or notebook (name and custom icon) |
| `i` | In Notes view: change notebook custom icon |
| `m` | In Notes view: move note to another notebook |
| `d` / `Delete` | In Notes view: move note to trash (or permanently delete from trash); delete notebook |
| `R` (Shift) | In Notes view: restore trashed note to original notebook |
| `J` / `K` (Shift) | In Notes view: manually reorder selected note or notebook down / up |
| `o` | In Bookmarks / Notes view: cycle sort (A-Z / Z-A; Notes also manual order) |
| `c` / `Enter` | In Remote view: connect / mount selected machine and browse in Explorer |
| `t` | In Remote view: open SSH terminal to selected machine |
| `a` | In Remote view: add new SSH machine |
| `F2` / `r` / `e` | In Remote view: edit selected machine configuration |
| `d` / `Delete` | In Remote view: delete selected machine configuration |
| `F5` | In Remote view: re-scan `~/.ssh/config` |
| `a` / `A` | In Files view: new file (opened in the editor) / new folder (`Shift+A`) in the selected directory |
| `F2` / `r` / `e` | In Files view: rename selected file or folder (open editor tabs follow) |
| `d` / `Delete` | In Files view: move selected file or folder to the trash, after confirmation |
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
| `m` | Toggle rendered preview / raw markup view (Markdown/HTML/Org-mode), fitted / 1:1 scale (images), rendered pages / text extraction (PDF), or player / raw header mode (video/audio) |
| `Tab` / `Shift+Tab` | In Org-mode preview: fold / unfold current heading or cycle global visibility (Overview → Contents → Show All) |
| `t` | In Org-mode preview: cycle TODO keyword (`TODO` → `NEXT` → `DONE` → none) |
| `Space` / `Return` | In Org-mode preview: toggle checkbox `[ ]` <-> `[X]` (or play / pause media playback when previewing a video or audio file) |
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
| `Ctrl+E` | Move active editor tab to dedicated panel right of preview, or dock back |
| `Ctrl+Alt+Right` / `L` | Move / split active editor tab to the right |
| `Ctrl+Alt+Left` / `H` | Move / split active editor tab to the left |
| `Ctrl+Alt+Down` / `J` | Move / split active editor tab downwards |
| `Ctrl+Alt+Up` / `K` | Move / split active editor tab upwards |
| `Tab` | In Org-mode editor: fold / unfold heading visibility (or normal indent on text lines) |
| `Shift+Tab` | In Org-mode editor: cycle global visibility (Overview → Contents → Show All) |
| `Shift+Right` / `Shift+Left` | In Org-mode editor: cycle heading TODO keyword (`TODO` → `NEXT` → `DONE` → none) |
| `Alt+Enter` | In Org-mode editor: insert new heading at current heading level (`M-RET`) |
| `Ctrl+Enter` / `Ctrl+Space` | In Org-mode editor: toggle checkbox `[ ]` <-> `[X]` and update parent progress statistics |
| `Right-click tab` | Context menu to change mode (Notepad/Emacs/Vim), move, split, or close the editor tab |
| `Ctrl+R` / `F2` | Rename active buffer tab |

### Key Capabilities by Editor Mode

The editor provides 3 dedicated editing modes, with Emacs and Vim functioning as visual clones of their respective editors.

#### Mode Switching

- **Status bar button**: click the `NOTEPAD`, `EMACS`, or `VIM` button in the bottom status bar to open the mode selector popover.
- **Tab context menu**: right-click any editor tab title -> select `Mode: Notepad`, `Mode: Emacs`, or `Mode: Vim`.
- **Vim command line**: `:set mode=vim`, `:set mode=emacs`, or `:set mode=notepad`.
- **Emacs minibuffer**: `M-x vim-mode`, `M-x emacs-mode`, or `M-x notepad-mode`.

#### 1. Notepad Mode (Default)

Clean Windows Notepad styling with bottom status bar (`Ln X, Col Y | 100% | Windows (CRLF) | UTF-8`).

| Operation | Shortcuts & Actions |
|---|---|
| **Enter and edit text** | Direct typing, Backspace, Delete, Select all (`Ctrl+A`), Cut (`Ctrl+X`), Copy (`Ctrl+C`), Paste (`Ctrl+V`) |
| **Move around document** | Arrow keys, `Ctrl+Left` / `Ctrl+Right` (word jump), `Home` / `End`, `Ctrl+Home` / `Ctrl+End`, `Page Up` / `Page Down`, Go to line (`Ctrl+G`) |
| **Find and replace** | `Ctrl+F` (Find bar), `Ctrl+H` (Replace bar), `F3` (Find next), `Shift+F3` (Find prev), `Enter` to find, `Esc` to close |
| **Format text** | Align Left (`Ctrl+L`), Align Center (`Ctrl+E`), Align Right (`Ctrl+R`), Align Justify (`Ctrl+J`); quick align buttons in toolbar; indent (`Tab`), unindent (`Shift+Tab`) |
| **Undo and redo** | Undo (`Ctrl+Z`), Redo (`Ctrl+Y` and `Ctrl+Shift+Z`) |
| **Save document** | Save file (`Ctrl+S`), Save As with chosen file name (`Ctrl+Shift+S` with inline filename bar) |

#### 2. Emacs Mode (Visual Clone of GNU Emacs)

Authentic GNU Emacs visual clone featuring the classic modeline (`-U:---  *filename*  (Text Fill)  Top  L1 C0  ----...`) with 3D shadow border and the bottom echo area / minibuffer.

| Operation | Shortcuts & Actions |
|---|---|
| **Enter and edit text** | Direct typing; Delete char (`C-d`), Kill word forward (`M-d`), Kill word backward (`M-DEL` / `M-Backspace`), Kill line (`C-k`); Set mark (`C-SPC` or `C-@`), Cancel mark (`C-g`); Cut / Kill region (`C-w`), Copy / Kill ring save (`M-w`), Paste / Yank (`C-y`) |
| **Move around document** | Forward/backward char (`C-f` / `C-b`), Next/previous line (`C-n` / `C-p`), Forward/backward word (`M-f` / `M-b`), Line start/end (`C-a` / `C-e`), Buffer start/end (`M-<` / `M->`), Scroll down/up (`C-v` / `M-v`), Recenter cursor (`C-l`), Goto line prompt in minibuffer (`M-g g` or `M-g M-g`) |
| **Find and replace** | Incremental search forward (`C-s`: `I-search: ` prompt in minibuffer with live match jumping), Incremental search backward (`C-r`: `I-search backward: `); Query replace (`M-%`: prompts for target and replacement in minibuffer, then replaces occurrences with count feedback) |
| **Format text** | Fill paragraph (`M-q`: wrap lines to 72 columns); Case conversion: Upcase word (`M-u`), Downcase word (`M-l`), Capitalize word (`M-c`); Minibuffer commands (`M-x set-justification-center`, `set-justification-left`, `set-justification-right`, `set-justification-full`) |
| **Undo and redo** | Undo (`C-/`, `C-_`, or `C-x u`), Redo (`C-Shift-_`, `M-_`, or `C-Shift-Z`) |
| **Save document** | Save buffer (`C-x C-s`: echoes `Wrote <path>`), Write file / Save As with chosen file name (`C-x C-w`: prompts in minibuffer `Write file: `, saves to chosen path and updates tab) |
| **Commands & Minibuffer** | `M-x` command prompt (`goto-line`, `query-replace`, `fill-paragraph`, `save-buffer`, `write-file`, `set-justification-center`, `vim-mode`, `notepad-mode`, `undo`, `redo`); cancel with `C-g` (`Quit`) |

#### 3. Vim Mode (Visual Clone of Vim)

Authentic Vim visual clone featuring the inverted console statusline (`-- NORMAL --`, `-- INSERT --`, `-- VISUAL --`, `-- VISUAL LINE --`, `COMMAND`), file status `[+] [utf-8]`, position ruler (`line,col  Top/Bot/All`), cursor styles (block in Normal/Visual, I-beam in Insert), and bottom command-line / echo area (`:`, `/`, `?`).

| Operation | Shortcuts & Actions |
|---|---|
| **Enter and edit text** | Insert mode (`i` before cursor, `I` first non-blank, `a` after cursor, `A` line end, `o` open line below, `O` open line above, `s` substitute char, `S`/`cc` change line, `C`/`c$` change to line end, `cw` change word); Exit to Normal mode (`Esc` or `Ctrl+[`); Insert mode delete word (`Ctrl+W`), delete line (`Ctrl+U`); Normal mode delete char (`x`), delete char back (`X`), delete line (`dd`), delete word (`dw`), delete to line end (`d$` / `D`), delete to line start (`d0`), delete to document end (`dG`), replace char (`r <char>`); Visual mode (`v` char, `V` line): cut (`d` / `x`), yank (`y`), change (`c`); Yank line (`yy` / `Y`), Yank word (`yw`); Put / paste after cursor (`p`), Put before cursor (`P`) |
| **Move around document** | Left/down/up/right (`h` / `j` / `k` / `l`), Word forward/backward/end (`w` / `b` / `e`), Line start/non-blank/end (`0` / `^` / `$`), Document start/end (`gg` / `G`), Goto line (`[count]G` or `:<line><CR>`), Page down/up (`Ctrl+F` / `Ctrl+B`), Half page down/up (`Ctrl+D` / `Ctrl+U`), Recenter cursor (`zz` or `z z`) |
| **Find and replace** | Search forward (`/pattern<CR>`), Search backward (`?pattern<CR>`), Next/previous match (`n` / `N`), Search word under cursor forward (`*`), Substitute / replace in buffer (`:%s/old/new/g` or `:s/old/new/g` with substitution count feedback) |
| **Format text** | Auto-indent current line (`==`), Indent line/selection (`>` / `>>`), Unindent line/selection (`<` / `<<`), Format paragraph (`gq`: wrap to 80 columns); Toggle case (`~`), Visual mode lowercase (`u`) / uppercase (`U`); Ex-command alignment (`:center [width]`, `:right [width]`, `:left`) |
| **Undo and redo** | Undo (`u` in Normal mode, echoes change count), Redo (`Ctrl+R` in Normal mode) |
| **Save document** | Save document (`:w` or `:write`), Save to chosen file name (`:w <filename>`, `:write <filename>`, or `:saveas <filename>`), Save and quit (`ZZ` or `:wq` or `:x`), Quit tab (`:q`), Quit discarding changes (`:q!`) |

## 📝 Build

```bash
make                           # incremental build (objects in build/)
make test                      # run unit tests (no display needed)
make check                     # zero-warning build + unit tests + module layering rules
make ui-test                   # headless UI smoke test (screenshots in build/ui/shots)
make clean                     # remove binary and build/
```

## 🚀 Run

```bash
./explorateur_fichiers                        # windowed mode (default 1000x600)
./explorateur_fichiers --size 1920x1080       # start with specific window size (or -s 1920x1080)
./explorateur_fichiers -f                     # start in fullscreen (same as --fullscreen)
./explorateur_fichiers /path/to/directory     # start in specific directory
```

## 📦 Prerequisites

- GTK 4.12 or newer, development files: `libgtk-4-dev`
- GCC: `gcc`

Install on Debian/Ubuntu:
```bash
sudo apt install libgtk-4-dev build-essential
```

Toolbar and tab icons are Adwaita symbolic icons bundled into the binary
(`icons/`, licence in `icons/COPYING`), so they look the same with any icon
theme. They are SVG files drawn through librsvg (present on any GTK desktop;
`sudo apt install librsvg2-common` on a minimal system); without it, GTK's
built-in icons are used instead.
If some buttons have no icon, run `EXPLORATEUR_DEBUG_ICONS=1 ./explorateur_fichiers`:
it prints the GTK version, the icon theme, whether SVG icons can be loaded, and
which icon file each button uses.

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

Optional (headless UI test, `make ui-test`):
```bash
sudo apt install xvfb xdotool imagemagick
```
The test runs the application on a virtual X display (Xvfb), drives it with
keyboard and mouse events (xdotool), and takes a screenshot of every step
(ImageMagick). It fails if the window does not open, GTK logs a warning or
critical, or `Ctrl+Q` does not quit cleanly. Set `UI_BASELINE=<directory>` to
compare the screenshots with a reference run (for example one from another branch).

## 🏗️ Project structure

```
src/
├── app_types.h              # Shared structs (AppData, EditorTab), enums, constants
├── main.c                   # Entry point, CLI options, widget assembly
│   Foundation (pure, unit-tested)
├── file_types.c/.h          # File type detection and emoji icons
├── git_utils.c/.h           # Git root/dir/branch discovery and status scan
├── explorer_row.c/.h        # Explorer tree rows (type, name, path, badge, children)
│   Shared UI helpers
├── app_events.c/.h          # Event bus: lower layers notify the window chrome
├── panels.c/.h              # Fullscreen, focused panel, split handle feedback
├── fs_watch.c/.h            # File/directory/.git monitors, debounced refresh
├── remote_store.c/.h        # Remote machines: remotes.xml, ~/.ssh/config
├── preview_summary.c/.h     # Preview summaries: machine, notebook, bookmark folder, trash
├── text_style.c/.h          # Dark+ scheme, monospace, syntax language
├── theme.c/.h               # Global CSS theme
├── icons.c/.h               # Theme icon buttons and tab headers
├── dialog.c/.h              # Modal dialogs drawn inside the main window
├── explorer_tree.c/.h       # Explorer list view: selection, expansion, row drawing
├── tree_nav.c/.h            # Explorer row navigation and selection
├── markdown.c/.h            # Markdown rendering engine
├── html.c/.h                # HTML rendering engine
├── minimap.c/.h             # Scaled file overview minimap
│   Explorer panel
├── file_list.c/.h           # Async directory scan, tree rows, expand/collapse, keys
├── file_actions.c/.h        # New file / new folder
├── sidebar.c/.h             # Explorer header and per-view toolbars
├── bookmarks.c/.h           # Hierarchical bookmark manager with XML persistence
├── notes.c/.h               # Notebooks, notes, and trash bin with custom reordering
├── remote.c/.h              # SSH remote view, dialogs and SSHFS mounting
│   Preview and editing
├── viewer.c/.h              # Preview dispatch, file/dir/image previews
├── orgmode.c/.h             # Org-mode rendering and interaction
├── pdf.c/.h                 # PDF document renderer and text extractor
├── video.c/.h, audio.c/.h   # Media previews
├── git_tab.c/.h             # Git repository commits tab and diff viewer
├── editor.c/.h              # Multi-tab file editor
├── editor_modes.c/.h        # Editor mode state, shared editing operations
├── editor_notepad.c         # Notepad mode
├── editor_emacs.c           # Emacs mode
├── editor_vim.c             # Vim mode
├── editor_org.c             # Org-mode editing keys in editor tabs
│   Window chrome
├── topbar.c/.h              # Breadcrumbs and toggle pills
├── status_bar.c/.h          # Bottom status bar
├── help.c/.h                # Key-binding cheat sheet
├── workspace.c/.h           # Virtual workspaces and workspace minimap
├── terminal.c/.h            # Embedded VTE terminal panel
├── search.c/.h              # Interactive search bar
└── ui.c/.h                  # Focus, global keys, layout toggle
icons/                       # Bundled toolbar icons (GResource, see icons/COPYING)
tests/                       # GLib unit tests (make test)
tests/ui/run_ui_test.sh      # Headless UI smoke test (make ui-test)
tools/check_layers.sh        # Module layering rules (make check)
Makefile                     # Build configuration
README.md                    # Documentation
AGENTS.md                    # AI coding agent guidelines
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
- Requires GTK 4.12+ and uses no deprecated GTK API (list views over `GListStore` / `GtkTreeListModel`, own dialog window)
