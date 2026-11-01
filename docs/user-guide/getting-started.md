# Getting started

## Installing

LookinFiles needs GTK 4.12 or newer, a C compiler, `make` and `pkg-config`.
On Debian or Ubuntu:

```bash
sudo apt install build-essential pkg-config libgtk-4-dev
make
./lookinfiles .
```

The other libraries are optional: the terminal (VTE), syntax highlighting
(GtkSourceView), PDF pages (poppler), SQLite databases and extra archive
formats (libarchive). `make` uses whatever is installed; a feature whose
library is missing shows a notice instead; see
[Troubleshooting](troubleshooting.md#a-feature-is-missing) for the packages.

## Launching

```bash
lookinfiles                     # reopen the workspaces of the last session
lookinfiles .                   # show the current directory
lookinfiles ~/projects/site     # show a directory
lookinfiles notes.txt           # edit a file
lookinfiles -n Work/Ideas       # open (or create) a note
```

| Option | Effect |
|---|---|
| `-e`, `--editor notepad\|emacs\|vim` | Global mode (see below); `.org` files always open in Org-mode |
| `-n`, `--note` | The argument is a note: `Title` or `Notebook/Title`; a `.md`, `.org` or `.txt` extension sets the format of a new note |
| `-f`, `--fullscreen` | Start in fullscreen |
| `-s`, `--size WIDTHxHEIGHT` | Window size, for example `1920x1080` |
| `-h`, `--help` | Show the options and exit |
| `-V`, `--version` | Show the version and exit |

Without an argument, LookinFiles reopens the workspaces you had when you
last quit: their folders, expanded folders, selection, sort and open files.
A folder that no longer exists is skipped. A file given on the command line
opens in the dedicated editor panel, with the explorer and the preview
hidden.

## The window

```
┌──────────────────────────────────────────────────────────────────────┐
│ ~ › projects › site      [1][2][+]  Layouts Themes │ Explorer Preview │
│                                                    │ Terminal  Help   │  ← top bar
├───────────────┬──────────────────────────────────────────────────────┤
│ Explorer      │ Preview │ main.c ● │ Git Commits                     │
│ [▢][▢][▢][▢]  │                                                      │
│ ▸ src         │   rendered file, editor tabs,                        │
│ ▾ docs        │   commit history, search results                     │
│   README.md   │                                                      │
│               ├──────────────────────────────────────────────────────┤
│               │ terminal panes                                       │
├───────────────┴──────────────────────────────────────────────────────┤
│ ⎇ main  README.md  Moved to trash [Undo]   2.1 kB Markdown UTF-8 Vim │  ← status bar
└──────────────────────────────────────────────────────────────────────┘
```

- **Top bar.** The current folder as breadcrumbs (click a part to go
  there), the workspace map, the **Layouts** and **Themes** buttons, then
  buttons that show or hide the **Explorer**, **Preview** and **Terminal**
  panels, and **Help**: a click opens this guide, a right click the
  keyboard cheat sheet.
- **Explorer panel** (left). Four views, picked with the buttons at its
  top: files, bookmarks, notes and remote machines. Below them, a small
  toolbar with the actions of the view.
- **Preview panel** (right). The **Preview** tab shows the selected file.
  Editor tabs open next to it, and so do the **Git Commits**, **Search**,
  **References** and **Help** tabs.
- **Terminal** (below or beside the preview). Shell panes, opened with
  `Ctrl+T`.
- **Status bar** (bottom). On the left, the git branch, the name of the
  selected entry and short messages (with an **Undo** button after a move
  to the trash or a rename). On the right, the size and type of the
  selection, the code navigation backend of the current editor tab, the
  text encoding and the global mode.

Every border between panels can be dragged. The panel that has the
keyboard focus is outlined; `Alt+Left` and `Alt+Right` move the focus from
panel to panel.

## The global mode: Notepad, Emacs or Vim

LookinFiles speaks three keyboard dialects. The **global mode**, shown at
the right end of the status bar, decides:

- the navigation keys of the explorer and the preview (`j` / `k` in Vim,
  `C-n` / `C-p` in Emacs, the arrows everywhere);
- the look of their search bar (a Notepad find bar, an Emacs I-search
  minibuffer or a Vim `/` command line);
- the mode of new editor tabs.

Click the mode in the status bar to change it, or start with `-e vim` /
`-e emacs`. An editor tab can then be switched to another mode on its own
(right click on its title). The keys that act on a selection (`a`, `d`,
`r`, `m`, `o`, `i`, `?`...) are the same in every mode.

## First steps

1. Move in the tree with the arrows (or the keys of your mode) and watch
   the preview follow the selection.
2. Press `Right` to open a folder, `Left` to close it, `Backspace` to go to
   the parent folder.
3. Press `Enter` on a text file to edit it in a tab; `Ctrl+S` saves it.
4. Press `m` on a Markdown file to switch between the rendered page and its
   source.
5. Press `Ctrl+P` and type part of a file name to jump to it.
6. Press `Ctrl+T` for a terminal.
7. Press `?` for the cheat sheet of the current mode, `F1` for this guide,
   `Ctrl+Q` to quit.
