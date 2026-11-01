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
| Right click on the title | Change the mode, line numbers, move, split or close the tab |

### The dedicated editor panel

By default editor tabs share the preview panel. The button at the start of
the tab bar (**Move Editor Tabs to a Dedicated Panel**, shown once an
editor tab is open) moves them to a
panel of their own, beside the preview, so you can read one file while
editing another. The same button docks them back.

### Splitting

`Ctrl+Alt+Arrows` (or `Ctrl+Alt+H` / `J` / `K` / `L`) moves the current tab
to a new group on that side, splitting the panel. From the main panel,
**right** opens the dedicated editor panel; from it, **left** docks the tab
back.

You can also drag a tab: drop it on another tab bar to move it there, or
near the edge of an editor to split that side.

## Choosing the mode of a tab

A new tab takes the **global mode** shown in the status bar. To change the
mode of one tab, right-click its title and pick **Notepad**, **Emacs** or
**Vim**; the current mode is marked. The status bar keeps showing the
global mode.

In Emacs mode, `M-x org-mode` turns on Org-mode editing for any file, and
`M-x text-mode` turns it off.

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
