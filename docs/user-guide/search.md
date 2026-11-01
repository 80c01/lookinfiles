# Finding files and text

Three tools find things beyond the visible tree. They work on the
**project**: the git repository of the current folder, or the folder
itself when it is not in a repository.

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

## Search in a panel or a tab

| Where | Notepad | Vim | Emacs |
|---|---|---|---|
| Explorer (entry names) | `Ctrl+F` | `/` | `C-s` / `C-r` |
| Preview (content) | `Ctrl+F` | `/` | `C-s` / `C-r` |
| Editor tab | `Ctrl+F`, `Ctrl+H` to replace | `/`, `?`, `:s` | `C-s`, `C-r`, `M-%` to replace |

See [the explorer](explorer.md#searching-by-name) and
[the editor](editor.md) for the details.
