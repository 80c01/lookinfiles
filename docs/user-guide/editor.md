# The editor

LookinFiles edits text files in tabs. Each tab has its own keyboard mode:
**Notepad** (the usual desktop keys), **Emacs** or **Vim** (see
[Vim mode](vim.md)). `.org` files open in **Org-mode**, which uses the
Emacs keys (see [Org-mode](org-mode.md)).

## Opening a file

- `Enter` or a double click on a file in the explorer opens it in a tab
  next to the **Preview** tab.
- `e` in the preview opens the file shown there.
- `t` (Vim) or `C-o` (Emacs) in the explorer or the preview opens it in the
  **dedicated editor panel** (below).
- `Ctrl+Enter` in `Ctrl+P` or `Alt+R` opens the chosen file in a tab (see
  [search](search.md)).
- `lookinfiles path/to/file` opens the file in the dedicated editor panel.

A file already open is not opened twice: its tab is shown.

A bullet (●) in the tab title marks unsaved changes. Closing a tab or
quitting with unsaved changes asks first.

### Jupyter notebooks

A Jupyter notebook (`.ipynb`) opens in a **notebook tab** instead: one box
per cell, with its type (**Code**, **Markdown** or **Raw**, changed in its
menu), its source highlighted in the kernel's language or as Markdown, and
under a code cell its outputs (text, errors, images). Markdown cells are
shown rendered (headings, lists, tables, images, code): `Enter` or a double
click edits the source, and running the cell (`Ctrl+Enter`, `Shift+Enter`,
**Run All**) renders it again. A new or empty Markdown cell stays as source.

- `Shift+Enter` runs the cell and goes to the next one, `Ctrl+Enter` runs
  it. The cells run in the notebook's Jupyter kernel, started at the first
  run in the notebook's folder and stopped with the tab: Python 3 with
  `jupyter_client` and the kernel (`ipykernel` for Python) must be
  installed. The kernel's button in the bar shows its state; its menu lists
  the installed kernels: choosing one stops the kernel running, records it
  in the notebook and starts it. **Run All**, the stop and restart buttons
  of the bar run every cell, interrupt and restart the kernel; when the
  code asks for a line (`input()`), type it in the row under the bar.
- `Ctrl+Up` / `Ctrl+Down` go to the previous / next cell, `Alt+Up` /
  `Alt+Down` move the cell, `Alt+Enter` adds a code cell below,
  `Alt+Delete` deletes the cell (**Undo Delete** in the bar puts it back).
  The buttons of each cell do the same.
- The bar adds code or Markdown cells, clears every output (**Clear
  Outputs**), and opens the file as JSON text in an editor tab (**Edit as
  JSON**).
- The notebook saves itself shortly after each change, when the tab closes
  and at quit (`Ctrl+S` saves at once). It is written as Jupyter writes it,
  so an unchanged cell gives no difference in git.

An nbformat 3 or damaged notebook opens as text.

## Tabs and panels

| Key | Action |
|---|---|
| `Ctrl+S` | Save (Emacs `C-x C-s`, Vim `:w`) |
| `Ctrl+Shift+S` | Save As (Emacs `C-x C-w`, Vim `:saveas`) |
| `Ctrl+W` | Close the tab (Vim `Ctrl+W c`; Emacs and Org-mode tabs keep `C-w` for "kill region") |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Next / previous tab (Vim `gt` / `gT`, Emacs `C-x <right>` / `C-x <left>`) |
| `Ctrl+R` / `F2` | Rename the file of the tab (only `F2` in Emacs and Org-mode tabs) |
| `Ctrl+Alt+N` | Show or hide line numbers |
| Middle click on the title | Close the tab |
| Right click on the title | Change the mode, line numbers, show the tagbar (code and Markdown), show the minimap (this tab only), move, split or close the tab |

### The dedicated editor panel

By default editor tabs share the preview panel. The button at the start of
the tab bar (**Split Preview Tab from Main Panel**, shown once a tab is
open) moves them to a panel of their own, beside the preview, so you can
read one file while editing another. Canvas, notebook, terminal and video tabs
move with them. The same button (**Merge Tabs into Main Panel**) docks them all back.

### Splitting

`Ctrl+Alt+Arrows` (or `Ctrl+Alt+H` / `J` / `K` / `L`) moves the current tab
to a new group on that side, splitting the panel. From the main panel,
**right** opens the dedicated editor panel; from it, **left** docks the tab
back.

You can also drag a tab: drop it on another tab bar to move it there, or
near the edge of an editor to split that side.

To see two places of one file at once, right-click its tab title and pick
**Split Right** or **Split Down** (Emacs `C-x 3` / `C-x 2`, Vim `Ctrl+W v` /
`Ctrl+W s`, `:vs` / `:sp`): a second tab shows the same file beside or
below. Both show the same text as you type and share the undo history; each
keeps its own cursor and selection. Closing one (Emacs `C-x 0`, Vim
`Ctrl+W q` / `:clo`) keeps the other open, and the unsaved changes with it;
Emacs `C-x 1`, Vim `Ctrl+W o` / `:only` close the other views of the file.
In Vim mode the views share one Vim: mode, registers and undo history.

## Choosing the mode of a tab

A new tab takes the **global mode** shown in the top bar. To change the
mode of one tab, right-click its title and pick **Notepad**, **Emacs** or
**Vim**; the current mode is marked. The top bar keeps showing the
global mode.

The **Editor** section of the Preferences (`Ctrl+,`) sets the global mode
kept for the next launches, whether new tabs show line numbers and wrap
long lines (`-e` on the command line wins for one launch), and whether the
code and Markdown tabs show the tagbar (the open tabs too), and whether the
editor tabs (the open tabs too), the preview and Help show the minimap.

In Emacs mode, `M-x org-mode` turns on Org-mode editing for any file, and
`M-x text-mode` turns it off.

The bar at the bottom of a tab tells about its file in the style of its
mode: the git branch, the path from the repository root (or the current
folder), a mark when there are unsaved changes, the language, the line
ending and encoding, the code navigation source and, with a language
server, the number of errors and warnings.

- Notepad: `⎇ main   src/main.c •` on the left, then
  `Ln 3, Col 7 | 100% | C | Unix (LF) | UTF-8 | clangd: 1 error, 0 warnings`.
- Emacs: `-U:**-  src/main.c  Top (0%)  L3 C7  Git-main  (C clangd Flymake[1 0])`.
- Vim (airline style): `NORMAL  ᚠ main  src/main.c [+]` on the left, then
  `word  c  utf-8[unix]    42% ☰   42/100 ㏑ :  7` on the right (the word under
  the cursor, filetype, encoding and format, percent, line/total, column).

## Notepad mode

The keys of a usual desktop editor:

| Action | Keys |
|---|---|
| Cut, copy, paste, select all | `Ctrl+X`, `Ctrl+C`, `Ctrl+V`, `Ctrl+A` |
| Undo, redo | `Ctrl+Z`, `Ctrl+Y` or `Ctrl+Shift+Z` |
| Find, replace | `Ctrl+F`, `Ctrl+H` (`F3` / `Shift+F3`: next / previous, `Esc`: close) |
| Go to line | `Ctrl+G` |
| Indent, unindent | `Tab`, `Shift+Tab` |
| Align left, center, right, justify | `Ctrl+L`, `Ctrl+E`, `Ctrl+R`, `Ctrl+J` |

Every match of a search is highlighted; the case is ignored.

## Emacs mode

A GNU Emacs look: a mode line and a minibuffer (echo area) at the bottom of
the tab.

| Action | Keys |
|---|---|
| Move | `C-f` `C-b` `C-n` `C-p`, `M-f` `M-b`, `C-a` `C-e`, `M-<` `M->`, `C-v` `M-v` |
| Mark, kill, copy, yank | `C-SPC`, `C-w`, `M-w`, `C-y`; `C-k` kill line, `M-d` kill word |
| Incremental search | `C-s` / `C-r` (`RET` stops, `C-g` goes back) |
| Query replace | `M-%` |
| Undo, redo | `C-/` or `C-_`, `M-_` |
| Fill paragraph, case | `M-q`, `M-u` / `M-l` / `M-c` |
| Go to line | `M-g g` |
| Commands by name | `M-x` (`Tab` completes) |

`C-g` or `Esc` cancels a command in the minibuffer.

## Code features

In source files every mode can go to a definition, list references and show
help cards. See [code navigation](code-navigation.md).

Lines changed since the last commit get a coloured bar in the margin
(green: added, blue: modified, red: deleted), updated as you type. See
[Git](git.md).

The full key lists of each mode are in the
[keyboard reference](../keyboard.md#editor-tabs).
