# The explorer

The explorer panel on the left shows a tree. It has four views, picked with
the buttons at its top:

| Button | View | Key |
|---|---|---|
| Folder | Files: the current folder and its subfolders | `Ctrl+A`, `Ctrl+U`, `Ctrl+Shift+R` go back to it |
| Bookmark | [Bookmarks](bookmarks.md) | `Ctrl+A` |
| Page | [Notes](notes.md) | `Ctrl+U` |
| Server | [Remote machines](remote.md) | `Ctrl+Shift+R` |

Each key switches between the files view and its own view. This page is
about the files view; the keys to move around are the same in all four.

## Moving around

The selection follows the arrows in every mode, plus the keys of the
global mode (see [Getting started](getting-started.md#the-global-mode-notepad-emacs-or-vim)):

| Action | Every mode | Vim | Emacs |
|---|---|---|---|
| Next / previous entry | `Down` / `Up` | `j` / `k` | `C-n` / `C-p` |
| Open a folder, or step into it | `Right` | `l` | `C-f` |
| Close a folder, or jump to its parent | `Left` | `h` | `C-b` |
| First / last entry | `Home` / `End` | `gg` / `G` | `M-<` / `M->` |
| One page down / up | `Page Down` / `Page Up` | `Ctrl+F` / `Ctrl+B` | `C-v` / `M-v` |

`Space` opens or closes the selected folder. The toolbar button with the
arrows closes every folder, or opens them all again.

The preview panel shows the selected entry as soon as it is selected: the
file's content, or an overview of a folder.

## Changing folder

- `Ctrl+Enter` on a folder makes it the root of the tree.
- `Backspace` or `Alt+Up` goes to the parent folder.
- A click on a part of the breadcrumbs in the top bar goes to that folder.
- `Shift+H` / `Shift+L` (or the mouse's back and forward buttons) go back
  and forward in the folders this workspace has shown, like a web browser.
- `Alt+R` lists your recent folders and files (see [search](search.md)).

Each [workspace](workspaces.md) has its own folder and history.

## Opening files

- `Enter` or a double click opens a text file in an editor tab, next to
  the **Preview** tab (see [the editor](editor.md)).
- `t` (Vim) or `C-o` (Emacs) opens it in the dedicated editor panel, a
  second panel beside the preview. In Vim the editor gets the focus; in
  Emacs the explorer keeps it.
- The right-click menu also offers **Open with the default application**
  for files that LookinFiles does not edit (images, PDF, office
  documents...).

## Searching by name

Search the visible entries by name with the search bar at the bottom of the
panel:

| Mode | Open the search | Next / previous match | Close |
|---|---|---|---|
| Notepad | `Ctrl+F` | `Enter` or `F3` / `Shift+F3` | `Esc` |
| Vim | `/` | `n` / `N` | `Esc` |
| Emacs | `C-s` / `C-r` | `C-s` / `C-r` again | `RET` keeps the match, `C-g` goes back |

To find a file anywhere in the project, use `Ctrl+P` instead (see
[search](search.md)).

## Creating, renaming and deleting

| Key | Action |
|---|---|
| `a` | New file in the selected folder (it opens in the editor) |
| `Shift+A` | New folder |
| `F2`, `r` or `e` | Rename the selected file or folder |
| `d` or `Delete` | Move it to the trash (asks first) |
| `u` | Undo the last move to the trash or rename |

After a rename or a move to the trash, the status bar shows a message with
an **Undo** button for a few seconds; `u` does the same at any time, for
the last change only. Editor tabs follow a renamed file.

Files go to the desktop trash (the one your file manager shows), so they
can also be restored from there.

## Sorting and hidden files

- `o` cycles the sort: name, date, size, extension. Folders stay first.
- `i` (or `I`) shows or hides hidden files (names starting with `.`).

The sort and the hidden state belong to the workspace and are restored at
the next launch.

## Git status

In a git repository, files show a coloured letter after their name:

| Badge | Meaning |
|---|---|
| **M** | Modified |
| **A** | Added to the index |
| **D** | Deleted |
| **R** | Renamed |
| **U** | Untracked (new file not yet added) |
| **!** | Merge conflict |

A folder shows **M** when it holds changed files, **U** when it only holds
new ones. See [Git](git.md) for the rest of the git features.

## The right-click menu

On a file or folder: open, open in the side panel (text files) or with the
default application (other files), open a terminal in a folder, rename,
move to the trash, copy the path, bookmark, undo. On an empty area: new
file, new folder, terminal.

## Other keys

| Key | Action |
|---|---|
| `Ctrl+D` | Bookmark the selected entry (or the current folder) |
| `m` | Switch the preview between rendered and raw |
| `+` / `-` / `0` | Zoom the previewed image or PDF in, out, back to fit |

The tree updates by itself when files change on disk, whether you change
them in LookinFiles, in the terminal or in another program.
