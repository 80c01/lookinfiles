# Finding files and text

Three tools find things beyond the visible tree. They work on the
**project**: the git repository of the current folder, or the folder
itself when it is not in a repository. A fourth one, the
[deep search](#deep-search-shiftf), searches any folder by name, size,
date and content, with no limit.

## Go to a file: `Ctrl+P`

`Ctrl+P` (Emacs: `C-x C-f`) lists every file of the project. Type part of
its name or path: the list is filtered as you type, best matches first, and
the letters do not need to be next to each other (`edvim` finds
`editor_vim.c`). With nothing typed, the files you opened recently come
first.

| Key | Action |
|---|---|
| `Enter` | Show the file in the explorer and the preview |
| `Ctrl+Enter` | Open it in an editor tab |
| `Esc` | Close the list |

Vim editor tabs keep `Ctrl+P` for Vim: press it from the explorer or the
preview.

## Recent folders and files: `Alt+R`

`Alt+R` lists the folders you visited, then the files you opened, most
recent first, from every session. Type to filter.

- `Enter` on a folder makes it the explorer's folder.
- `Enter` on a file shows it; `Ctrl+Enter` opens it in an editor tab.

## Search in files: `Ctrl+Shift+F`

`Ctrl+Shift+F` asks for a text and searches it in every file of the
project, in the background. The selection of the current editor tab (or
your last search) is filled in.

- The search ignores the case. The buttons right of the text change how
  it matches:

  | Button | Key | Effect |
  |---|---|---|
  | `Aa` Match case | `Alt+M` | `Total` no longer finds `total` |
  | `ab` Whole word | `Alt+W` | `foo` no longer finds `foobar` |
  | `.*` Regular expression | `Alt+R` | PCRE syntax (`get_\w+`, `^#include`), one line at a time; an invalid expression is shown under the text |
  | `a…z` Letters in order | `Alt+L` | the letters do not need to be next to each other (`edvim` finds `editor_vim`); spaces are ignored, the shortest part of each line is shown |

  Regular expression and letters in order exclude each other; whole word
  does not apply to letters in order.
- **Files to include** (`Alt+N`) and **Files to exclude** (`Alt+X`) take
  patterns separated by commas, as in `.gitignore`: `*.c` or `tests` match
  a name at any depth, `src/ui` or `/build` match from the project root,
  `build/` matches folders only; `*` also crosses folders. Empty: every
  file.
- **Include ignored files** (`Alt+I`) also searches the files git ignores
  (outside git: hidden and build folders).
- The text, the options and the patterns are kept for the next search
  of the workspace: each workspace has its own last search and its own
  Search tab. A search keeps running when you switch workspace; its
  results wait in the Search tab of the workspace that started it.
- Limits: 100 000 files, 512 MB read in all, 5 000 results; files over
  4 MB and binary files are skipped. When a limit stops the search, or
  files are skipped, the header of the Search tab says so (in the warning
  colour); narrow the files to include to search the rest.
- The results go to a **Search** tab, grouped by file, with the matching
  lines. `Enter` or a double click opens one at its line; the cross closes
  the tab.
- Files are read from disk: unsaved changes in editor tabs are not
  searched.

## Deep search: `Shift+F`

`Shift+F` in the explorer (or **Search Here** in the right-click menu of a
folder, or the **Search** button in the top right corner of a folder's
preview) opens the **Deep Search** tab in the preview panel, on the
selected folder (on a file: its folder). Unlike the tools above, it is not
limited to the project, nor in the number of files listed or read: it goes
through everything below the folder.

Fill in any of the criteria, then press `Enter` in a field (or the
**Search** button); an empty field means "any".

| Field | What it matches |
|---|---|
| **Folder** | Where to search, with everything below it (`~` is your home folder); the arrow goes up to its parent, and when it is not the explorer's folder a folder button puts that one back. As you type (or with `Ctrl+Space`), folders are suggested below it: the subfolders of the path typed (`~/pro` → `~/projects`), then the recent folders going on from it; any other text matches the recent folders as in `Ctrl+P`. `Down` / `Up` choose one, `Tab` completes with it and lists its subfolders, `Enter` takes it, `Escape` hides them |
| **Name** | A part of the name, any case (`report`); with `*` or `?`, a pattern of the whole name (`IMG_????.jpg`, `*.tar.gz`) |
| **Extensions** | Separated by commas, any case: `c, h, md`, `tar.gz` |
| **or** a category | Instead of extensions, a kind of file: documents, images, videos, audio, archives, text and code, programs (from the extension, as in [Disk Usage](preview-panel.md#disk-usage)); choosing one empties and dims the extensions; a click in them (or typing) sets **Any category** back |
| **Contains** | A text in the file, with the same buttons as Search in Files (`Aa`, `ab`, `.*`, `a…z`; `Alt+M`, `Alt+W`, `Alt+R`, `Alt+L`); binary files are skipped, large files are read in full |
| **Type** | Files and folders, files only (links count as files) or folders only |
| **Size** | From / to, both included: `42`, `500k`, `10M`, `2G` (powers of 1024); files only |
| **Last change** | From / to: a day (`2026-11-01`, `2026-11-01 14:30`, `today`, `yesterday`) or a time ago (`30min`, `3h`, `7d`, `2w`, `6m` for months, `1y`); a day given as the second date is included; the calendar button right of each field picks a day, or clears the date |
| **Exclude** | Paths left out, separated by commas, as in Search in Files: `node_modules`, `.git`, `build/`; the folders left out are not entered |
| **Depth** | Levels below the folder: `1` for its own entries only |
| **Hidden entries** | Also the names starting with a dot (off: they are skipped, with what is below them) |
| **This filesystem only** | Do not enter the other disks mounted below the folder |

The search runs in the background: the results are listed as they are
found, with the counters (entries found, entries tested, folders read,
bytes read for a content) and the folder being read. `Escape` (or the
**Stop** button) stops it and keeps what was found; `F5` searches again.
The **Reset** button empties the criteria (the folder stays) and the
results.
Symbolic links are listed but never followed, and `/proc`, `/sys` and
`/dev` are skipped. The search stops at 1 000 000 results, to keep the
memory in check.

The results show the name, the folder (below the one searched), the size
and the last change; with a content, also the number of matches and the
first matching line. Click a column header to sort by it. `Down` in a field
goes to the results; there, `Enter` or a double click shows the entry in
the explorer, `Ctrl+Enter` opens a file in an editor tab at its first
match, `Space` previews it at its first match (`Escape` in the preview
comes back to the results), and a right click also copies the path.

To see the files while going through the results, move the tab out of the
preview panel, as an editor tab: `Ctrl+Alt+Arrow`, the right-click menu
of the tab, or a drag of the tab to the editor panel or the edge of a pane.
In one click, the button with two panes at the top right of the tab (shown
while it is in the preview panel) puts the results on the left and the
preview on the right, with the focus in the results.
Beside the preview, the result you move to (arrows, click) is previewed
there, at its first match; the explorer keeps its selection, and shows it
again in the preview when it gets the focus back.

Under the results, two charts (side by side when the panel is wide
enough, else one over the other) follow them as they come: the files by size
(empty, under 1k, 1k to 10k... 10G and more) and by last change (by day,
month or year, whichever fits; the oldest years together in the first
bar), each with one bar for the number of files and one for the room they
take (the tooltip of a bar gives both). Click a bar, or move to it with
`Left` / `Right` and press `Enter`, to list only its files, without
searching again; a size and a date can be chosen together. Click it again,
or **Show All**, to list every result. Drag the line above the charts to
make them smaller or to hide them.

The tab keeps its criteria while it is open: opening it on another folder
only changes the folder. Each workspace has its own. Click the bookmark
button in the header (or right-click the tab and choose **Save as
Bookmark…**) to keep the criteria in your [bookmarks](bookmarks.md#saving-a-search)
and run them again later — either in the saved folder or in the explorer's
current folder. `Ctrl+W`, a middle click on the tab or its cross close it
and stop the search.

## Search in a panel or a tab

| Where | Notepad | Vim | Emacs |
|---|---|---|---|
| Explorer (entry names) | `Ctrl+F` | `/` | `C-s` / `C-r` |
| Preview (content) | `Ctrl+F` | `/` | `C-s` / `C-r` |
| Editor tab | `Ctrl+F`, `Ctrl+H` to replace | `/`, `?`, `:s` | `C-s`, `C-r`, `M-%` to replace |

See [the explorer](explorer.md#searching-by-name) and
[the editor](editor.md) for the details.
