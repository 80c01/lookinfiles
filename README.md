# Lookin — look in your files

Lookin is a keyboard-driven file explorer for Linux, written in C with
GTK 4: a file tree with live previews, a multi-mode text editor (Notepad,
Emacs, Vim), notes, bookmarks, SSH remote machines, an embedded terminal
and virtual workspaces, in a VS Code Dark+ look.

![Lookin showing a Markdown preview](docs/screenshot.png)

## Contents

- [Features](#features)
- [Requirements](#requirements)
- [Building](#building)
- [Usage](#usage)
- [Keyboard shortcuts](#keyboard-shortcuts)
- [Configuration](#configuration)
- [Development](#development)
- [Contributing](#contributing)
- [License](#license)

## Features

**Explorer panel** (left), with four views switched from its header:

- **Files**: expandable directory tree with lazy loading, VS Code file-type
  icons, Git status badges (`M`, `U`, `A`, `D`, `R`, `!`) that update on disk
  changes, hidden-files toggle, four sort orders, live search, and new file,
  new folder, rename and move-to-trash actions.
- **Bookmarks**: directories, files and web URLs organised in folders.
- **Notes**: notebooks of plain-text, Markdown and Org-mode notes, with
  manual ordering, format conversion and a trash with restore.
- **Remote**: SSH machines discovered from `~/.ssh/config` or added by hand;
  connecting mounts the machine with SSHFS and browses it like a local folder.

**Preview panel** (right), for the selected entry:

- Text and source code with syntax highlighting (GtkSourceView, Dark+ colours),
  and line numbers for source code.
- Markdown rendered like VS Code's preview: headings, lists, task lists
  (including in-progress `[/]` / `[-]` tasks), tables, quotes, GitHub alerts,
  highlighted code blocks and images.
- HTML and interactive Org-mode (folding, TODO cycling, checkboxes, links).
- Images (EXIF orientation, camera, exposure, date and GPS details, zoom with
  `+` / `-` / `0` or `Ctrl+wheel`), PDF documents (poppler), video and audio
  with player controls.
- Office documents, read without any external tool: Word (`.docx`, and
  `.doc` from Word 97-2003 or 6 / 95) and OpenDocument text (`.odt`) with
  headings, styles, lists, tables, links and images (not in `.doc`);
  spreadsheets (`.xlsx`, `.ods`, `.xls` from Excel 97-2003 or 5.0 / 95) as
  grids with column letters, number, date and percent formats; presentations
  (`.pptx`, `.odp`, `.ppt` / `.pps`) slide by slide; RTF. Page layout, fonts, headers and
  footers are not shown; long documents stop after 256 KB of text. Other OLE2
  compound files (`.msi`, `.vsd`, Works...) show their properties (title,
  author, dates, counts) and streams.
- Mail messages (`.eml`, `.emlx`, or any file starting with mail headers):
  the headers, the text with quotes and signature dimmed (or the HTML body
  with its inline images; remote content is never fetched), forwarded
  messages and the attachments; encoded headers and charsets decoded.
  Mailboxes (Thunderbird `Inbox` and other mbox files, `.mbox`, `.mbx`) as a
  list of messages, opened one by one with `PgDn` / `PgUp`; only the
  beginning and the end of very large ones are read. Maildir and MH folders
  show their newest messages (date, sender, subject, read / unread / replied)
  instead of the `cur` / `new` / `tmp` listing. Outlook messages (`.msg`)
  are shown the same way (text, HTML or RTF body, attachments), and so are
  the `winmail.dat` (TNEF) attachments Outlook adds to mails.
- EPUB books (cover, metadata, contents, first chapters) and Jupyter
  notebooks (Markdown cells, highlighted code, text and image outputs).
- Data and text formats: CSV / TSV as aligned tables, JSON (also minified,
  JSONC and JSON Lines) pretty-printed and coloured, diffs and patches with a
  per-file summary, logs coloured by level and shown from their end, manual
  pages formatted like `man` (gzipped ones too), calendars (`.ics`), contacts
  (`.vcf`) and `.desktop` entries as cards.
- Archives (`.zip`, `.jar`, `.tar`, `.tar.gz`, `.a`, `.deb`...) listed with sizes and
  dates; a gzipped file shows its content. Certificates and keys (PEM, DER):
  subject, issuer, validity, alternative names, key, fingerprints (private
  keys are only named). Fonts: names, details and samples in the font.
- Directory overview: totals, file types, branch and git changes, then each entry with
  its icon, size, date and git status, in the explorer's sort order, and the
  folder's README rendered below (one level read in the background, never
  recursively).
- Summaries of notebooks, bookmark folders and machines.
- A **Git Commits** tab listing the repository history with each commit's diff.
- Git change bars in the text preview and the editor, as in VS Code: lines
  added (green), modified (blue) or deleted (red marker) since the last commit;
  the editor updates them while typing. The minimap shows them along its
  left edge.
- A minimap that mirrors the displayed layout and scrolls the view (click,
  drag or mouse wheel). As in VS Code, a long file keeps a readable scale and
  the minimap scrolls along with the view.
- File types are detected from the content (an image or a `#!` script without
  extension, TypeScript `.ts` that is not a video); unknown binary files show a
  hex dump (offsets, bytes and an ASCII column) of their first 64 KB.
- Executables, shared libraries and object files (ELF) show their
  architecture, type, interpreter, linked libraries, soname, build ID,
  whether they are stripped, and their exported symbols; `m` shows the hex
  dump instead. Only the headers are read, even for a large library.
- `m` switches between the rendered and the raw view (text, or the hex dump
  of binary formats). Text files over 512 KB show their first 64 KB, and the
  next parts load as you scroll down (up to 8 MB). The encoding is detected
  (UTF-8, UTF-16, Windows-1252) and shown in the status bar.
- `make bench-preview` times every structured preview, and `make
  preview-coverage DIR=~` reports how many files of a folder would only get a
  hex dump.

**Editor**: files open in tabs, docked in the preview panel or in a dedicated
panel that can be split in every direction. Drag a tab with the mouse onto
another tab bar to move it there, or near an edge of an editor to split it on
that side (a blue area shows where it lands). Each tab has its own mode:
**Notepad** (default), **Emacs** or **Vim**, with Org-mode editing keys.
The Emacs and Org-mode modes use a block cursor, Vim a block cursor in normal
mode.

**Terminal**: embedded terminal panes (VTE), split beside or below each other;
in a workspace connected to a remote machine they open an SSH shell.

**Workspaces**: up to eight virtual workspaces, each with its own folder, view,
panels, editors and terminals. Panel arrangements can be saved as named
layouts and applied from a list, including the default layout new workspaces
open with.

**Keyboard first**: Vim-style navigation everywhere, one shortcut per action
in every view, a built-in cheat sheet (`?`), and dialogs drawn inside the
window.

## Requirements

| Component | Package (Debian / Ubuntu) | Needed for |
|---|---|---|
| GTK 4.12 or newer | `libgtk-4-dev` | Required |
| C compiler, make, pkg-config | `build-essential pkg-config` | Required |
| VTE for GTK 4 | `libvte-2.91-gtk4-dev` | Embedded terminal (optional) |
| GtkSourceView 5 | `libgtksourceview-5-dev` | Syntax highlighting (optional) |
| poppler-glib | `libpoppler-glib-dev` | PDF preview (optional) |
| SQLite | `libsqlite3-dev` | Database preview: tables and rows (optional) |
| libarchive | `libarchive-dev` | 7z, RAR, xz, bzip2, zstd, ISO, RPM listings (optional) |
| librsvg | `librsvg2-common` | Toolbar icons (present on any GTK desktop) |
| Git | `git` | Status badges and the Git Commits tab (at run time) |
| SSHFS | `sshfs` | Browsing remote machines (at run time) |

Optional libraries are detected by the Makefile: without them the application
still builds and shows a notice where the feature would be.

## Building

```bash
sudo apt install build-essential pkg-config libgtk-4-dev
sudo apt install libvte-2.91-gtk4-dev libgtksourceview-5-dev libpoppler-glib-dev  # optional
make
```

The binary `lookin` is created in the project directory (objects
in `build/`). `make clean` removes both.

## Usage

```bash
./lookin                     # current directory, 1000×600 window
./lookin /path/to/directory  # start in another directory
./lookin path/to/file.txt    # edit a file in the dedicated editor panel only
./lookin --editor vim        # default editor mode: emacs, vim or notepad (also -e)
./lookin --note Work/Ideas   # open (or create) a note, see below (also -n)
./lookin --size 1920x1080    # window size (also -s)
./lookin --fullscreen        # start in fullscreen (also -f)
./lookin --help              # list the options (also -h)
```

`--editor` sets the mode of new editor tabs; Org files always open in
Org-mode.

With `--note`, the argument names a note instead of a file: `Title` (searched
in every notebook) or `Notebook/Title`. A `.org`, `.md` or `.txt` extension
matches the note's file name and sets the type of a new note (Rich Format
otherwise). A missing note, or notebook, is created. The note opens like a
file, with the explorer switched to the NOTES view.

Press `?` at any time for the list of shortcuts, `Alt+Left` / `Alt+Right` to
move between panels, and `Ctrl+Q` to quit.

## Keyboard shortcuts

The same list is available inside the application with `?`.

### Global

| Key | Action |
|---|---|
| `Ctrl+Q` | Quit |
| `?` | Show / hide the shortcut cheat sheet |
| `f` | Toggle fullscreen (`Ctrl+F` keeps its paging role) |
| `Alt+Left` / `Alt+Right` | Move the focus between the open panels |
| `Alt+1` … `Alt+8` | Switch virtual workspace (or click the top-bar workspace map) |
| `Alt++` / `Alt+-` | Add a workspace with the default layout / close the current one |
| `Alt+L` | Workspace layouts: apply a saved layout, save the current panels under a name (`Default` for new workspaces), delete one or reset the default |
| `Ctrl+T` | Open a terminal pane beside the running ones (exit the shell to close it) |
| `Ctrl+Shift+T` | Open a terminal pane below the running ones |
| `Ctrl+Shift+L` | Stack the terminal under the viewer, or put it back beside it |
| `Ctrl+A` | Switch between the files and bookmarks views |
| `Ctrl+U` | Switch between the files and notes views |
| `Ctrl+Shift+R` | Switch between the files and remote machines views |

### Explorer panel

| Key | Action |
|---|---|
| `j` / `k`, `Down` / `Up` | Next / previous visible entry |
| `gg` / `G`, `Home` / `End` | First / last entry |
| `Ctrl+F` / `Ctrl+B`, `Page Down` / `Page Up` | One page down / up |
| `zz` / `zt` / `zb` | Scroll the selected entry to the center / top / bottom |
| `Space` | Expand / collapse the selected directory |
| `l` / `Right` | Expand the selected directory, or step into its first child |
| `h` / `Left` | Collapse the selected directory, or jump to its parent |
| `Enter` / double click | Expand a directory, open a file in the editor, open a bookmark |
| `Ctrl+Enter` | Enter the selected directory as the new root |
| `Backspace` | Parent directory (or back to the files view) |
| `/`, then `n` / `N` | Search entries by name, next / previous match |
| `a` / `Shift+A` | New file / new folder in the selected directory |
| `F2` / `r` / `e` | Rename the selected file or folder |
| `d` / `Delete` | Move the selected file or folder to the trash (asks first) |
| `i` / `I` | Toggle hidden files |
| `o` | Cycle the sort: name, date, size, extension |
| `Ctrl+D` | Bookmark the selected entry or the current directory |
| `m` | Toggle the rendered / raw preview of the selected file |
| `+` / `-` / `0` | Zoom the previewed image or PDF in / out / back to the fitted size |
| Right click | Context menu (bookmarks, notes and remote views) |

**Bookmarks view**

| Key | Action |
|---|---|
| `Enter` / `l` | Open the bookmark (directory, file or URL) / expand the folder |
| `a` / `Shift+A` | Add a bookmark / a folder (inside the selected folder) |
| `F2` / `r` / `e` | Edit name, location and folder |
| `d` / `Delete` | Delete the selected bookmark or folder |
| `Shift+K` / `Shift+J` | Move the selected item up / down |
| `o` | Cycle the sort: A–Z / Z–A |

**Notes view**

| Key | Action |
|---|---|
| `a` / `Shift+A` | New note (Plain Text, Rich Format or Org-mode) / new notebook |
| `F2` / `r` / `e` | Edit the note (title and format) or notebook (name and icon) |
| `i` | Change the notebook icon |
| `m` | Move the note to another notebook |
| `d` / `Delete` | Move the note to the trash, or delete the notebook |
| `Shift+R` | Restore the note from the trash to its notebook |
| `Shift+K` / `Shift+J` | Move the selected note or notebook up / down |
| `o` | Cycle the sort: manual / A–Z / Z–A |

**Remote view**

| Key | Action |
|---|---|
| `Enter` / `c` | Connect the machine (SSHFS) and browse it in the explorer |
| `t` | Open an SSH terminal to the machine |
| `a` | Add a machine |
| `F2` / `r` / `e` | Edit the selected machine |
| `d` / `Delete` | Delete the selected machine |
| `F5` | Re-scan `~/.ssh/config` |

### Preview panel

| Key | Action |
|---|---|
| `j` / `k` | Scroll one line down / up |
| `Ctrl+F` / `Ctrl+B` | Scroll one page down / up |
| `gg` / `G` | Start / end of the file |
| `/`, then `n` / `N` | Search in the content, next / previous match |
| `e` | Open the file in the editor (text files) |
| `m` | Rendered / raw view: Markdown, HTML, Org, CSV, JSON, diffs, logs, calendars, contacts (rendered / text); fitted / 1:1 (images); pages / text (PDF); player / file details (video, audio); document, archive listing, certificate, ELF summary / hex dump |
| `+` / `-` / `0`, `Ctrl+wheel` | Zoom an image or PDF in / out / back to the fitted size |
| `PgDn` / `PgUp` | PDF: next / previous page; mailbox: from the list into each message and back |
| `Space` / `p` | Play / pause the video or audio |
| `Tab` / `Shift+Tab` | Org-mode: fold the heading / cycle the global visibility |
| `t` | Org-mode: cycle the TODO keyword (the file's `#+TODO:` keywords; `TODO` → `DONE` → none by default) |
| `Space` / `Enter` | Org-mode: toggle the checkbox `[ ]` ↔ `[X]` (parents and `[n/m]` cookies follow) |
| Click | Org-mode: fold a heading, cycle a TODO, toggle a checkbox, open a link |
| `Esc` | Close the search bar |

### Editor tabs

Opening a file (`Enter` or double click in the explorer) adds a tab next to the
**Preview** tab. A bullet in the tab title marks unsaved changes.

| Key | Action |
|---|---|
| `Ctrl+S` | Save the file |
| `Ctrl+W` | Close the tab (asks first when there are unsaved changes) |
| `Ctrl+E` | Move the tab to the dedicated editor panel, or dock it back (not in Emacs and Org-mode tabs: `C-e` is end of line) |
| `Ctrl+Alt+Arrows` / `Ctrl+Alt+H J K L` | Move / split the tab left, down, up or right |
| `Ctrl+R` / `F2` | Rename the current text buffer (only `F2` in Emacs and Org-mode tabs: `C-r` searches backward) |
| `Ctrl+Alt+N` | Show or hide the line numbers of the tab (also `:set number` / `:set nonumber` in Vim mode, `M-x display-line-numbers-mode` in Emacs mode) |
| Right click on the tab | Change the mode, show line numbers, move, split or close the tab |
| Drag the tab | Onto a tab bar: move it there; near an editor edge: split that side |

The editor mode is changed from the status bar button, the tab menu,
`:set mode=vim|emacs|notepad` in Vim mode or `M-x vim-mode` / `emacs-mode` /
`notepad-mode` in Emacs mode. While the preview is the active page, the
status bar button shows and sets the default mode of new editor tabs
(initially the `--editor` one). In Emacs mode, `M-x org-mode` turns on Org-mode
editing and highlighting for any file, and `M-x text-mode` turns them off
(even for a `.org` file, whose folded headings are then shown).

#### Code navigation

In source files (C, C++, Java, C#, Python, Shell, JavaScript / TypeScript,
Go, Rust, Lua, Ruby, Makefiles), go to the definition of the identifier under
the cursor (functions, macros, types, fields, enum constants, global
variables...) and come back. The lookup reads the files of the project (its
git repository, else the file's folder) in the background, unsaved tabs as
edited; when nothing is found, it goes to the first use of the name in the
current function. A language server is used first when one is installed
(see [Configuration](#configuration)). When [Universal Ctags](https://ctags.io) is installed, its
index is used (kept in `~/.cache/lookin/tags/`, updated for the files that
changed), which also brings Objective-C, PHP, Perl, Kotlin, Swift, Scala,
Dart, Haskell, Elixir, Erlang, Julia, R and Tcl.

| Action | Notepad | Emacs | Vim |
|---|---|---|---|
| Go to the definition | `F12`, `Ctrl+Click` | `M-.`, `Ctrl+Click` | `gd`, `Ctrl+]`, `Ctrl+Click`, `:tag name` |
| Go to the declaration (prototype, `extern`) | `Ctrl+F12` | `C-u M-.` | `gD`, `:tag! name` |
| Back / forward | `Ctrl+Alt+-` / `Ctrl+Shift+-` | `M-,` / `C-M-,` | `Ctrl+O` / `Tab` (in the file, then across files) |
| Help card (signature, documentation, location) | `Ctrl+K Ctrl+I`, rest the mouse on a name | `C-h .`, mouse | `K`, mouse |
| Signature of the call being typed | automatic after `(` and `,`, `Ctrl+Shift+Space` | automatic, in the echo area | automatic in Insert mode |
| References (References tab) | `Shift+F12` | `M-?` | `gr`, `:References` |
| Symbols of the file (filtered list) | `Ctrl+Shift+O` | `M-g i` | `:Symbols` |

Repeated on a definition, the lookup goes to the next candidate (other
definitions, then the declarations). The message line tells which one is shown
and how many there are.

The references are listed in a **References** tab next to **Git Commits**
(grouped by file, `Enter` or a double click opens one, the cross closes the
tab). The other occurrences of the identifier under the cursor are
highlighted. `Ctrl+Click` on a name in the preview of a source file opens it
at its definition. With a language server, errors and warnings are underlined
(red, yellow), counted in the status bar, and explained when the mouse rests
on them.

#### Org-mode editing

Tabs showing an `.org` file open in the **Org-mode** editor mode (Emacs keys)
and get the Emacs Org-mode commands. The file opens folded to its top-level
headings, unless a `#+STARTUP:` line says `content` (all headings) or
`showall` / `nofold` / `showeverything`. Keys without `C-c` also work in the
Notepad and Vim modes; commands that ask a question (dates, tags, links) use
the Emacs minibuffer (`Tab` completes tags and `*Heading` links). `M-x org-…` runs the commands by their Emacs name
(`org-todo`, `org-schedule`, `org-clock-in`, `org-show-todo-tree`…).
`Alt` is Emacs' Meta (`M-`), `C-c C-t` means `Ctrl+C` then `Ctrl+T`.

| Key | Action |
|---|---|
| `Tab` | On a heading: cycle folded → children → subtree; on a drawer or `#+begin_` block: fold it; in a table: next cell; after `<s`, `<e`, `<q`, `<c`, `<v`, `<x`, `<C`, `<l`, `<h`, `<a`: insert a block; on an empty list item: change its level |
| `Shift+Tab` | Global visibility: overview → contents → show all; in a table: previous cell |
| `Alt+Enter` / `Alt+Shift+Enter` | New heading (TODO heading), or new list item (with a checkbox) in a list |
| `Ctrl+Enter` / `Ctrl+Shift+Enter` | New heading (TODO heading) after the current subtree |
| `Alt+Left` / `Alt+Right` | Promote / demote the heading; outdent / indent the list item; move the table column |
| `Alt+Shift+Left` / `Alt+Shift+Right` | Promote / demote the subtree; delete / insert a table column |
| `Alt+Up` / `Alt+Down` | Move the subtree or list item past its sibling; move the table row |
| `Alt+Shift+Up` / `Alt+Shift+Down` | Delete / insert a table row |
| `Shift+Left` / `Shift+Right` | Cycle the TODO keyword; cycle the list bullet; on a timestamp: previous / next day |
| `Shift+Up` / `Shift+Down` | Raise / lower the priority `[#A]`; on a timestamp: change the year, month, day, hour, minute or repeater under the cursor |
| `Enter` | In a table: next row (added when needed) |
| `Ctrl+Space` | On a checkbox item: toggle it |
| `C-c C-c` | Toggle the checkbox, align the table, update the cookie, or set the heading's tags |
| `C-c C-t` | Cycle the TODO keyword from anywhere in the entry |
| `C-c ,` then `A`–`Z` / `Space` | Set / remove the priority |
| `C-c q` | Set the heading's tags (aligned on column 77) |
| `C-c C-s` / `C-c C-d` | Schedule / set a deadline (`today`, `+3`, `+2w`, `mon`, `2026-11-02 10:00`, `none`) |
| `C-c .` / `C-c !` | Insert (or replace) an active `<…>` / inactive `[…]` timestamp |
| `C-u C-c .` / `C-u C-c !` | Same, with the time of day (the current time unless one is typed) |
| `C-c C-x C-i` / `C-c C-x C-o` | Clock in (into a `:LOGBOOK:` drawer) / clock out |
| `C-c C-l` / `C-c C-o` | Insert a link / open the link under the cursor (URL, `file:`, `*Heading`, `#custom-id`, `<<target>>`) |
| `C-c C-n` / `C-c C-p` | Next / previous visible heading |
| `C-c C-f` / `C-c C-b` / `C-c C-u` | Next / previous heading at the same level / parent heading |
| `C-c -` | Turn the line into a list item, cycle the bullet, or add a table rule |
| `C-c *` | Turn the line into a heading, or a heading into text |
| `C-c #` | Update the `[n/m]` and `[n%]` statistics cookies |
| `C-c / t` / `C-c / /` | Sparse tree: show only open TODO entries / lines matching a regexp |

TODO keywords come from `#+TODO:` lines (`#+TODO: TODO NEXT | DONE CANCELLED`),
`TODO` / `DONE` by default; `#+STARTUP: logdone` adds `CLOSED: [date]` when an
entry is done, and a repeating date (`SCHEDULED: <2026-11-02 Mon +1w>`) moves
on instead. A keyword written `RUNNING(r!)` logs a line such as
`- State "RUNNING"    from "TODO"       [2026-10-29 Thu 19:48]` under the
heading when an entry enters it (`WAIT(w/!)`: when it leaves it);
`#+STARTUP: logdrawer` puts these lines in a `:LOGBOOK:` drawer. `RUNNING`
is shown in yellow. Checking a box updates its parents (`[-]` when partly done) and
the `[n/m]` / `[n%]` cookies, which also count the TODO children of a heading.
Tables realign on `Tab`, `Enter` and `C-c C-c`; a line starting with `|-`
becomes a rule. Folds survive edits, and moving the cursor into folded text
(search, go to line) unfolds it.

<details>
<summary><strong>Notepad mode</strong> (default)</summary>

| Operation | Keys |
|---|---|
| Edit | Typing, `Ctrl+X` / `Ctrl+C` / `Ctrl+V`, `Ctrl+A` |
| Move | Arrows, `Ctrl+Left` / `Ctrl+Right`, `Home` / `End`, `Ctrl+Home` / `Ctrl+End`, `Page Up` / `Page Down`, go to line `Ctrl+G` |
| Find and replace | `Ctrl+F`, `Ctrl+H`, `F3` / `Shift+F3` |
| Format | Align left / center / right / justify `Ctrl+L` / `E` / `R` / `J`, indent `Tab`, unindent `Shift+Tab` |
| Undo and redo | `Ctrl+Z`, `Ctrl+Y` / `Ctrl+Shift+Z` |
| Save | `Ctrl+S`, Save As `Ctrl+Shift+S` |
| Code | Definition `F12` / `Ctrl+Click`, declaration `Ctrl+F12`, back / forward `Ctrl+Alt+-` / `Ctrl+Shift+-`, help `Ctrl+K Ctrl+I`, signature `Ctrl+Shift+Space`, references `Shift+F12`, symbols `Ctrl+Shift+O` |

</details>

<details>
<summary><strong>Emacs mode</strong></summary>

GNU Emacs mode line and minibuffer (echo area).

| Operation | Keys |
|---|---|
| Edit | `C-d` delete char, `M-d` / `M-DEL` kill word, `C-k` kill line, `C-SPC` / `C-@` set mark (twice: deactivate it), `C-g` cancel, `C-w` kill region, `M-w` copy, `C-y` yank |
| Move | `C-f` / `C-b`, `C-n` / `C-p`, `M-f` / `M-b`, `C-a` / `C-e`, `M-<` / `M->`, `C-v` / `M-v`, `C-l` recenter (middle, top, bottom), `M-g g` go to line |
| Find and replace | `C-s` / `C-r` incremental search: the match grows as you type (case-insensitive unless the query has a capital), other matches highlighted with a `[n/total]` count; `C-s` / `C-r` again for the next / previous match (on an empty query: the last search), wraps after a failing search; `DEL` undoes a step, `C-w` adds the next word, `RET` stops at the match (the mark stays where the search started), `C-g` drops the failing part then goes back, other command keys stop and run. `M-%` query replace |
| Format | `M-q` fill paragraph (72 columns), `M-u` / `M-l` / `M-c` case, `M-x set-justification-left` / `center` / `right` / `full` |
| Undo and redo | `C-/`, `C-_`, `C-x u` / `C-Shift-_`, `M-_` |
| Save | `C-x C-s`, write file (Save As) `C-x C-w` |
| Code | `M-.` definition (`C-u M-.` declaration), `M-,` back, `C-M-,` forward, `M-?` references, `M-g i` symbols, `C-h .` help; signatures in the echo area |
| Commands | `M-x` (`goto-line`, `query-replace`, `fill-paragraph`, `save-buffer`, `write-file`, `undo`, `redo`, `display-line-numbers-mode`, `org-mode`, `text-mode`, mode switches) |
| Minibuffer | `Tab` completes (`M-x` command names, Org tags, `*Heading` links) and lists the candidates; `C-g` / `Esc` quits |

</details>

<details>
<summary><strong>Vim mode</strong></summary>

A Vim engine with the real grammar: `["x][count]operator[count]motion`,
text objects, registers, marks, dot repeat, macros, Visual block and Ex
commands on ranges. Status line with mode badge, pending keys, `recording
@x` and ruler; command line (`:`, `/`, `?`) with history (`Up` / `Down`),
`Tab` / `Shift+Tab` completion of commands and `:set` options (all matches
listed over the statusline, Vim's wildmenu) and `Ctrl+R` register insert.

| Operation | Keys |
|---|---|
| Operators | `d` `c` `y` `>` `<` `=` `g~` `gu` `gU` `g?` `gq` `gw` + any motion or text object; doubled for lines (`dd`, `cc`, `>>`, `gUU`); `v` / `V` / `Ctrl+V` after the operator force the kind |
| Move | `h` `j` `k` `l`, `w` `b` `e` `ge` (and `W` `B` `E` `gE`), `0` `^` `$` `g_` `\|`, `f` `t` `F` `T` `;` `,`, `gg` `G` `[count]%`, `%`, `{` `}`, `(` `)`, `H` `M` `L`, `+` `-` `_`, `[(` `[{` `])` `]}`, `[[` `]]` |
| Text objects | `iw` `aw` `iW` `aW`, `is` `as`, `ip` `ap`, `i(` `a(` (`b`), `i[` `i{` (`B`) `i<`, `i"` `i'` `` i` ``, `it` `at` (tags), with counts |
| Insert | `i` `a` `I` `A` `o` `O` `gi` `gI`, `s` `S` `C`, `R` Replace; `[count]` repeats the text; in Insert `Ctrl+W` `Ctrl+U` word / line, `Ctrl+T` `Ctrl+D` indent, `Ctrl+R x` register, `Ctrl+A` last insert, `Ctrl+N` / `Ctrl+P` complete, `Ctrl+E` / `Ctrl+Y` copy from below / above, `Ctrl+V` literal, `Ctrl+O` one command |
| Edit | `x` `X` `D` `J` `gJ`, `r<char>` (`r<CR>` splits), `~`, `Ctrl+A` / `Ctrl+X` (decimal, hex, negative), `.` repeat the last change (with a new count) |
| Yank and put | `y` `Y`, `p` `P` `gp` `gP`; registers `"a`–`"z` (`"A` appends), `"0` yank, `"1`–`"9` deletes, `"-` small delete, `"_` black hole, `"+` / `"*` system clipboard (also the default), `".` `":` `"/` `"%` |
| Visual | `v` `V` `Ctrl+V`, `o` / `O` other end / corner, `gv` reselect, text objects extend; then any operator, `r`, `J`, `p` (replace), `I` / `A` / `c` / `$A` on every line of a block, `:` for `'<,'>`, `Ctrl+A` / `g Ctrl+A` |
| Undo and redo | `u` / `Ctrl+R`: one whole command per step (an Insert session is one), `:undo`, `:redo` |
| Marks and jumps | `m{a-z}`, `` `a `` / `'a`, ``` `` ``` / `''`, `` `[ `` `` `] `` `` `< `` `` `. ``, `Ctrl+O` / `Tab` jump list, `:marks`, `:jumps` |
| Macros | `qa` … `q` record, `@a` run (with a count; stops at the first failure), `@@` again, `@:` repeat the last Ex command |
| Search | `/` `?` (Vim regex: `\<` `\>` `\(` `\)` `\\|` `\{n,m}` `\zs` `\ze` `\v` `\c`), incremental with all matches highlighted and a `[n/total]` count, `n` `N`, `*` `#` `g*` `g#`, `gd` (first occurrence, when the file is not code); `:noh` |
| Code | `gd` / `Ctrl+]` definition, `gD` declaration, `:tag[!] name`, `K` help, `gr` / `:References`, `:Symbols`; `Ctrl+O` / `Tab` continue across files |
| Ex commands | Ranges `%` `.` `$` `N` `'a` `'<,'>` `/pat/` `?pat?` `+N` `-N` `;`; `:s/pat/rep/[gine]` (`&` `\1` `\u` `\U` `\r` `~`), `:&` `:&&` `&` `g&`, `:g/pat/cmd` `:v`, `:d [x] [N]`, `:y`, `:pu`, `:m`, `:t` / `:co`, `:j[!]`, `:>` `:<`, `:normal`, `:sort [n i u] [!]`, `:retab`, `:center` / `:right` / `:left [width]`, `:k`, `:reg`, `:[N]` |
| Options | `:set` `ignorecase` `smartcase` `hlsearch` `incsearch` `wrapscan` `expandtab` `autoindent` `shiftwidth` `tabstop` `softtabstop` (default `-1`: same as `shiftwidth`) `textwidth` `clipboard`, `number`, `wrap`, `mode=vim\|emacs\|notepad` (`no` / `inv` / `!` / `?` / `+=`) |
| Scroll | `zz` `zt` `zb` (`z.` `z<CR>` `z-`), `Ctrl+E` / `Ctrl+Y` lines, `Ctrl+D` / `Ctrl+U` half pages, `Ctrl+F` / `Ctrl+B` pages |
| Files and tabs | `:w [file]`, `:saveas`, `:up`, `:wq` / `:x` / `ZZ`, `:q` / `:q!` / `ZQ`, `:e[!] [file]`, `gt` / `gT` / `:tabn` / `:tabp`, `Ctrl+G` / `g Ctrl+G` file and word count, `ga` / `g8` character code |

</details>

## Configuration

Settings and data are kept in `~/.config/lookin/`
(`$XDG_CONFIG_HOME` is honoured):

| Path | Content |
|---|---|
| `bookmarks.xml` | Bookmarks and bookmark folders |
| `notes/` | Notebooks (one directory each), notes, `notes.xml` and the `.trash/` |
| `remotes.xml` | Remote machines added by hand (`~/.ssh/config` hosts are read directly) |
| `layouts.ini` | Saved workspace layouts and the default layout of new workspaces |
| `lsp.ini` | Language server commands for code navigation (optional, see below) |

Remote machines are mounted under `~/.cache/lookin/remotes/`.

Code navigation starts a language server when one is installed for the
file's language: `clangd` (C, C++, Objective-C), `pylsp` /
`pyright-langserver` / `jedi-language-server` (Python), `gopls`,
`rust-analyzer`, `typescript-language-server`, `bash-language-server`,
`lua-language-server`, `solargraph`, `jdtls`, `csharp-ls`, `intelephense`...
`lsp.ini` replaces a command or turns a server off (keys are the language ids:
`c`, `cpp`, `python`, `javascript`, `go`, `rust`...):

```ini
[servers]
c=clangd --background-index
python=pyright-langserver --stdio
rust=none
```

The status bar shows what serves the current editor tab: the server name,
`ctags` or `built-in index`.

Toolbar icons are bundled in the binary, so they look the same with any icon
theme. If buttons show no icon, `LOOKIN_DEBUG_ICONS=1 ./lookin`
prints which icon file each button uses.

## Development

```bash
make check     # build with zero warnings, unit tests, module layering rules
make test      # unit tests only (GLib, no display needed)
make ui-test   # headless UI test (needs xvfb, xdotool and imagemagick)
```

`make ui-test` runs the application on a virtual X display, drives it with the
keyboard and mouse, and saves a screenshot of every step in `build/ui/shots/`.
It fails when the window does not open, a dialog opens a separate window, GTK
logs a warning, or `Ctrl+Q` does not quit cleanly. `UI_BASELINE=<directory>`
compares the screenshots with those of another run.

The code lives in `src/`, one module per feature, in layers checked by
`make check` (`tools/check_layers.sh`):

| Layer | Modules |
|---|---|
| Foundation (no widgets, unit-tested) | `file_types`, `zip_reader`, `docx`, `git_utils`, `line_diff`, `pane_geometry`, `minimap_geometry`, `explorer_row`, `layout_store`, `vim_keys`, `vim_parse`, `vim_motion`, `vim_regex`, `vim_ex`, `vim_engine` |
| Shared helpers | `diff_gutter`, `tab_drop`, `app_events`, `panels`, `fs_watch`, `remote_store`, `preview_summary`, `text_style`, `theme`, `icons`, `dialog`, `image_utils`, `explorer_tree`, `tree_nav`, `markdown`, `html`, `minimap` |
| Features | `file_list`, `file_actions`, `bookmarks`, `notes`, `remote`, `viewer`, `orgmode`, `pdf`, `video`, `audio`, `git_tab`, `editor`, `editor_modes` (+ `editor_notepad`, `editor_emacs`, `editor_vim`, `editor_org`), `terminal`, `search` |
| Window | `topbar`, `status_bar`, `sidebar`, `help`, `workspace`, `layouts`, `ui`, `main` |

[AGENTS.md](AGENTS.md) describes the architecture, the conventions and the
rules in detail. Release history is in [CHANGELOG.md](CHANGELOG.md).

## Contributing

Contributions are welcome. Before opening a pull request:

1. Read [AGENTS.md](AGENTS.md): module layers, UI conventions (same key for
   the same action in every view, no separate windows, no deprecated GTK API).
2. Run `make check` and, for interface changes, `make ui-test`.
3. Keep commits focused, with a short imperative subject
   (e.g. `Add Ctrl+F paging in the explorer`).
4. Update this README and the in-app cheat sheet (`src/help.c`) together
   when a shortcut changes.

## License

No license has been chosen for this project yet; until one is added, the
code may not be reused without the authors' permission. The bundled toolbar
icons come from the GNOME Adwaita icon theme and keep their own license
(CC-BY-SA 3.0 or LGPL 3, and CC-BY-SA 4.0; see [icons/COPYING](icons/COPYING)).
