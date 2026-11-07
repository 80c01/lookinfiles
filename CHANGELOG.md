# Changelog

All notable changes to this project are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Changed

- The `git` program is no longer needed: status badges, change bars, the
  **Git Commits** tab and the project file list read the repository with
  libgit2, now required to build (`libgit2-dev`).
- The global mode (Notepad, Emacs, Vim) is shown and picked from a pill in
  the top bar instead of the bottom status bar.
- The git branch and the short messages (with Undo) move to a bar at the
  bottom of the explorer; while the explorer is hidden, messages show in
  the top bar.
- A bar at the bottom of the preview page shows the name, size, type and
  text encoding of the previewed entry.
- The bar of an editor tab shows, in the style of its mode, the git branch,
  the path from the repository root, the unsaved mark, the language, the
  line ending, the code navigation source and the language server's error
  and warning counts.
- The window-wide status bar at the bottom is gone: each panel has its own.
- Icons follow VS Code: files show the VS Code (Seti) file-type icons
  everywhere they are listed (explorer, tabs, bookmarks, Quick Open,
  references, folder previews), and every other icon is a theme icon in the
  same style. Emoji are gone from the interface; notebooks pick their icon
  from a set of theme icons (icons chosen as emoji before are converted).

- The application is renamed **LookinFiles**: the binary is `lookinfiles`,
  settings live in `~/.config/lookinfiles/` and the cache in
  `~/.cache/lookinfiles/`.

- Word documents (`.docx`) look like they do in Word: fonts, sizes,
  colours and highlights from the styles, theme and direct formatting;
  paragraph alignment, indents and spacing; list labels as Word numbers them
  (`1.`, `a.`, `III.2.`, bullets from symbol fonts); tables as grids with
  wrapped cells; pictures at their size; page breaks; the page header and
  footer. Text colours are adjusted to stay readable on the theme.
- PowerPoint presentations (`.pptx`) are drawn slide by slide like
  PowerPoint shows them: positions, rotation, shapes and freeforms, fills
  (gradients included), outlines, pictures, groups and tables (with the
  banded table style), text with its fonts, sizes, colours, bullets and
  numbering, inherited from the slide layout, master and theme; the speaker
  notes under each slide. The first 20 slides are drawn, the others listed.
- PowerPoint 97-2003 presentations (`.ppt`, `.pps`) are drawn the same way
  from their drawings: shape positions, groups, geometry, fills, outlines,
  rotation, pictures, backgrounds and the master's shapes, with the text
  styled by its runs over the master's text styles and colour scheme.
- Word 97-2003 documents (`.doc`) are shown the same way: the formatting of
  their styles (with base styles) and runs (fonts from the font table,
  sizes, colours, highlights, capitals), paragraph alignment, indents and
  spacing, tables as grids, page breaks, the header and footer.

- Animated GIFs are decoded by a built-in decoder instead of
  GdkPixbufAnimation, deprecated in gdk-pixbuf 2.44 (the build is free of
  warnings again); animated WebP images now show their first frame.

- The application is renamed **Lookin** ("look in your files"): the binary
  is `lookin`, settings live in `~/.config/lookin/`, remote mounts in
  `~/.cache/lookin/remotes/`, and the icon diagnostic variable is
  `LOOKIN_DEBUG_ICONS`.

### Added

- The preview of a Canvas note draws its page exactly as the Canvas tab
  does (shapes, arrows, text, tables, pictures), instead of a text summary.
- `make windows-exe` builds a single Windows executable holding the whole
  application: it extracts itself once into
  `%LOCALAPPDATA%\lookinfiles-runtime\` and starts from there.
- Themes can pick an icon set with `icons=<set>` in `[Theme]`. Sets are built
  in (**desktop**, pixel icons of a 1990s grey desktop, used by the 1990s
  reference theme; **textmode**, code page 437 style icons in the CGA colours,
  used by the 1980s reference theme; **phosphor**, the same icons in green
  phosphor intensities, used by the 1970s reference theme) or folders of SVG files in `~/.config/lookinfiles/icons/<set>/`.
- Bookmark folders have an icon of their own, chosen in the New / Edit
  Folder dialog from the same set as notebooks.
- OLE2 compound files are detected by content and read without any external
  tool: their document properties (title, author, dates, page and word
  counts) and their streams are shown; encrypted ones say so.
- Word 97-2003 documents (`.doc`) like `.docx`: headings, bold / italic /
  underline / strike / superscript, lists with their levels, tables and
  hyperlinks (Word 6 / 95: the text as paragraphs).
- Excel 97-2003 and 5.0 / 95 workbooks (`.xls`) as grids like `.xlsx`:
  shared strings, numbers, booleans, errors and formula results with their
  number, percent and date formats (1904 date system included).
- PowerPoint 97-2003 presentations (`.ppt`, `.pps`) slide by slide like
  `.pptx`: titles, body text and speaker notes.
- Mail messages (`.eml`, `.emlx`, files without extension starting with mail
  headers): header card, text or HTML body (inline images, no remote
  content), forwarded messages and attachments, any charset.
- Mailboxes (mbox: Thunderbird `Inbox`, `.mbox`, `.mbx`; mboxrd and mboxcl)
  as a list of messages (date, sender, subject, size, attachments), each
  message opened with `PgDn` / `PgUp`; large ones are read at both ends.
- Maildir (with Maildir++ subfolders) and MH folders: the directory preview
  lists their newest messages with their read / unread / replied / flagged
  status.
- Outlook messages (`.msg`) like `.eml`: sender, recipients, date, the text,
  HTML or compressed RTF body and the attachments.
- TNEF attachments of Outlook mails (`winmail.dat`, detected by content):
  the message body and the attachments with their real names and sizes.
- Outlook data files (`.pst`, `.ost`) and Outlook Express folders (`.dbx`):
  a card with the format, ANSI or Unicode, the size, the encryption and the
  message count of a `.dbx` folder (their messages are not read).
- Code navigation in editor tabs, in the three modes: go to the definition or
  declaration of the identifier under the cursor and back (Notepad `F12` /
  `Ctrl+F12` / `Ctrl+Click`, `Ctrl+Alt+-` / `Ctrl+Shift+-`; Emacs `M-.` /
  `C-u M-.` / `M-,` / `C-M-,`; Vim `gd` / `Ctrl+]` / `gD` / `:tag`, `Ctrl+O` /
  `Tab` across files). A built-in index reads the project in the background
  (C, C++, Java, C#, Python, Shell, JavaScript / TypeScript, Go, Rust, Lua,
  Ruby, Makefiles); see docs/code-nav.md. With Universal Ctags installed, its
  index is used instead (cached, updated per changed file) and more languages
  are covered. Language servers (clangd, pylsp, gopls, rust-analyzer...) are
  used first when installed (`lsp.ini` to choose them).
- Help cards in editor tabs: what the identifier is (signature, documentation,
  location) on `Ctrl+K Ctrl+I` / `C-h .` / `K` or when the mouse rests on it,
  and the signature of the call being typed with the current parameter.
- References in a References tab (`Shift+F12` / `M-?` / `gr`), symbols of the
  file in a filtered list (`Ctrl+Shift+O` / `M-g i` / `:Symbols`), highlight
  of the other occurrences of the identifier, `Ctrl+Click` in source previews,
  and language server diagnostics (underlined, counted in the status bar).
- Previews for many more formats, each built in the background and shown as
  close as possible to its own application (see docs/previews.md):
  CSV / TSV tables; pretty-printed JSON, JSONC and JSON Lines; diffs and
  patches with a change summary; logs coloured by level, shown from their
  end; archives (ZIP, JAR, tar, tar.gz, ar / .deb) and the content of gzipped
  files; certificates, keys and certificate requests (PEM, DER); calendars,
  contacts and `.desktop` entries; fonts with samples; manual pages; Jupyter
  notebooks; Excel and OpenDocument spreadsheets; OpenDocument text;
  PowerPoint and OpenDocument presentations; EPUB books; RTF documents.
- File types are detected from the content for all of these (a `.txt` that is
  a ZIP, a JSON file without extension, a gzipped manual page).
- Images: photo details (camera, lens, exposure, date, GPS) under the image,
  EXIF orientation applied, and zoom (`+` / `-` / `0`, `Ctrl+wheel`), also
  for PDF pages.
- `make bench-preview` (time of every structured preview against its budget),
  `make preview-coverage DIR=...` (share of files that would only get a hex
  dump), and `LOOKIN_DEBUG_PREVIEW=1` (time from selection to display).

- The minimap scrolls over long files, as in VS Code: below a readable scale
  it stops shrinking the document and scrolls in proportion to the view (the
  slider reaches its bottom with the end of the file). Dragging the slider
  moves it with the pointer; a click elsewhere centers the clicked line. Only
  the part of the document around what is shown is drawn, and lines too
  small for glyphs are drawn as word blocks on whole pixel rows.

- Vim editor mode rewritten on a Vim engine (`vim_engine`, unit-tested
  without a display): the `["x][count]operator[count]motion` grammar with
  every common motion and text object (`iw`, `ap`, `i(`, `a"`, `it`...),
  registers (named, numbered, `"-`, `"_`, `"+` clipboard), marks and the
  jump list, dot repeat, macros, Visual block (with block `I` / `A` / `c`),
  Replace mode, Insert-mode keys (`Ctrl+R`, `Ctrl+N`, `Ctrl+O`...), one undo
  step per command, Vim regular expressions with incremental and
  highlighted search, and Ex commands on ranges (`:s`, `:g`, `:v`, `:d`,
  `:m`, `:t`, `:j`, `:normal`, `:sort`, `:retab`, `:set`, `:reg`), with
  command-line history and completion.

- Org-mode: a yellow `RUNNING` TODO keyword, and timestamped state changes
  (`- State "RUNNING"    from "TODO"       [2026-10-29 Thu 19:48]`) for
  keywords marked `!` in `#+TODO:` (`/!` when leaving), into a `:LOGBOOK:`
  drawer with `#+STARTUP: logdrawer`.

- Command line: `lookin <file>` opens the file in the dedicated editor
  panel, with the explorer and the preview hidden, `-e` / `--editor emacs|vim|notepad` chooses the default
  mode of new editor tabs, `-n` / `--note` opens (or creates) a note by
  name (`Title` or `Notebook/Title`), and `-h` / `--help` lists the options.
- Emacs editor tabs: `M-x org-mode` turns on Org-mode editing and
  highlighting for any file, `M-x text-mode` turns them off (also for a
  `.org` file, unfolding it).
- Emacs and Org-mode editor tabs use a block cursor. Org files open folded
  to their top-level headings (`#+STARTUP: overview`), or as their
  `#+STARTUP:` line says (`content`, `showall`, `nofold`, `showeverything`).

- Emacs mode incremental search works like Emacs: the match grows in place
  as you type, `C-s` / `C-r` move to the next / previous match and wrap
  after a failing search, `DEL` undoes a step, `C-w` adds a word, `RET`
  leaves the cursor at the match with the mark at the start point, `C-g`
  goes back. Case-insensitive for lower-case queries; all matches are
  highlighted with a match count. `Tab` completes in the minibuffer.

- Org-mode editor rework, following Emacs Org-mode: three-state heading
  folding (also drawers and blocks) that survives edits and unfolds where the
  cursor goes; promote / demote and move headings and subtrees; new headings
  and TODO headings; TODO keywords from `#+TODO:` with `logdone` and
  repeating dates; priorities; aligned tags; `SCHEDULED` / `DEADLINE` with a
  date prompt; timestamps changed field by field with `Shift+arrows`; clock
  in / out; lists (new item, indent, move, bullet cycling, renumbering);
  checkboxes with parent states and `[n/m]` / `[n%]` cookies; tables
  (alignment, cell navigation, row and column moves, inserts, deletes,
  rules); links (insert, open URLs, files and internal targets); `<s` block
  templates; heading navigation; sparse trees; `M-x org-…` commands. Every
  command is one undo step. Richer Org syntax colors in editor tabs. The
  preview's TODO and checkbox clicks follow the same rules.

- Word document preview (`.docx`, also detected from the content):
  headings, paragraphs, bold / italic / underline / strike, superscript,
  bulleted and numbered lists (with Word's numbering), tables, links,
  embedded images and equations as text, and the author. Read in the
  background with no external tool; long documents stop after 256 KB of
  text. `m` switches to the hex dump. The legacy binary `.doc` format is not
  supported.

- ELF preview for executables, shared libraries (`.so`) and object files
  (`.o`): architecture, type, interpreter, linked libraries, soname,
  RPATH/RUNPATH, build ID, stripped or not, and exported symbols. Only the
  headers and tables needed are read. `m` switches to the hex dump.
- Binary files of an unknown format are previewed as a hex dump of their
  first 64 KB: offsets, 16 bytes per line and an ASCII column.
- Directory preview: header with the number of files and folders, their
  size and the last change, then a table of the entries (icon, name, size,
  relative date) in the explorer's sort order. Only the selected folder is
  read, in the background after a short pause, so moving through the
  explorer stays fast.
- Directory preview shows the git branch and the changed files below the
  folder, the git status of each entry, and renders the folder's README
  (Markdown, Org-mode or plain text) under the table.
- Directory preview counts the documents, images, video and audio files of
  the folder (from their names).
- Editor tabs can be dragged with the mouse: onto a tab bar to move them
  there, or near an edge of an editor to split it on that side; a blue area
  shows where the tab lands.
- Mouse wheel over the minimap scrolls the preview or editor view.
- Git change bars in the gutter of the text preview and the editor, as in
  VS Code: added (green), modified (blue) and deleted (red marker) lines
  since the last commit, updated while editing and after a commit; the
  minimap shows them along its left edge.
- Preview detects file types from their content (magic numbers, text or
  binary) instead of the extension: images, PDFs, video and audio without or
  with a wrong extension, `#!` scripts highlighted, binary files noticed.
- Markdown preview shows in-progress tasks (`- [/]` or `- [-]`) with a
  half-filled amber box.
- Workspace layouts (`Alt+L`): save the panel arrangement and terminal panes
  under a name, apply one from a list, and customize the default layout used
  by new workspaces and at startup.
- Minimap drawn from the displayed layout (wrapped lines, heading sizes, code
  panels, tables, images), acting as the vertical scrollbar.
- Markdown preview in the style of VS Code: plain headings with rules, joined
  paragraphs, highlighted code panels, quote bars, GitHub alerts, tables as
  grids with column alignment.
- `make check` rejects any separate top-level window; a UI test step checks
  that dialogs stay inside the window.
- Bundled Adwaita toolbar icons, identical with every icon theme;
  `EXPLORATEUR_DEBUG_ICONS=1` diagnostic.

### Changed

- The explorer is a `GtkListView` over a `GtkTreeListModel`; the Git Commits
  list a `GtkColumnView`. No deprecated GTK API is used and GTK 4.12 or newer
  is required.
- Dialogs are drawn inside the main window instead of separate windows.
- New workspaces start from the default view instead of a copy of the current
  one.
- The focused panel is shown by its tab and selection colours instead of a
  frame.
- Editors and previews keep the cursor and the last line clear of the
  scrollbars.

### Fixed

- Canvas: in a locale with a decimal comma (French...), the points of lines
  and arrows and the picture crops were saved as "16,3,17,0", so the preview
  and the reopened note drew other shapes. They are now written with a dot,
  and notes saved the old way load as drawn.
- Crashes: a terminal whose shell could not start freed VTE's error a
  second time (heap corruption); selecting a row whose folders had to be
  expanded first could use the row after a bookmarks or notes rebuild freed it.
- Removing a workspace could free its folder twice (crash): the teardown
  saved the workspace being removed.
- Saving a file in the editor replaced it: a symbolic link became a plain
  file, a script lost its executable bit and read-only files were
  overwritten. Files are now written in place, read-only ones are refused,
  and a failed save is reported instead of passing silently.
- Closing a terminal tab right after opening it called VTE on the closed
  terminal, and every shell was watched twice (`waitid ... No child
  processes` warnings).
- Opening a binary file in the editor gave an empty tab (saving it erased
  the file); files in other encodings are converted to UTF-8 with a notice.
- Closing the window with unsaved changes no longer loses them: it asks like
  `Ctrl+Q`; with a dialog open, the dialog closes first (the window used to
  hang).
- Files changing on disk no longer rebuild the whole explorer tree: new and
  removed entries are merged in, the rest keeps its expansion, selection and
  scroll position.
- Git status scans no longer pile up while files keep changing (one at a
  time); switching branches no longer freezes the window; change bars skip
  the line diff when the committed version did not change.
- Selecting a huge commit in the Git Commits tab (a large file added) froze
  the window while its whole diff was laid out: the first 5000 lines show.
- Canvas notes logged GTK CSS warnings (`gap`, `@bg_warning`).
- Saving an Emacs or Vim tab updated the modified flag of its mode line
  (`-U:**-`, `[+]`) only after the next cursor move.
- Moving an editor tab right / left / up / down picked a notebook in another
  row or column of the split editors.
- Moving the last editor tab out of the preview panel showed the Git tab
  instead of the preview.
- The preview showed the previous workspace's file after switching workspaces.
- Escape on a dialog logged a GTK critical (dialog destroyed twice).
- Empty bookmark folders were summarised as holding one item.

## [2.0]

- Migrated to GTK 4 (`gtk4`, `gtksourceview-5`, `vte-2.91-gtk4`) with GSK
  rendering, `GdkPaintable` / `GdkTexture` image and PDF rendering, GTK 4 event
  controllers, and `GTask` + `GCancellable` background work for directory
  scans, file reading, image scaling and PDF rasterization.

## [1.17]

- Hanging indent for wrapped list items in Markdown and HTML previews, at
  every nesting level, including multi-paragraph items.

## [1.16]

- Richer HTML preview: base64 images, code blocks with language banners,
  description lists, `<details>` / `<summary>`, task lists, `<kbd>`, `<abbr>`,
  table captions and numeric alignment, metadata banner, math, Greek and arrow
  entities.

## [1.15]

- Panel backgrounds (`#191A1B` explorer, `#121314` preview and editor,
  `#1f2021` terminal) and high-contrast selection.

## [1.14]

- VS Code Dark+ palette across all panels and VS Code file icon mappings.

## [1.13]

- Comfortable text padding in the preview and editor tabs.

## [1.12]

- PDF preview with poppler-glib: metadata banner, page rendering, text mode.

## [1.11]

- Image preview with metadata, fitted to the window or at 1:1 scale.

## [1.10]

- Monospace font for code files and raw markup views.

## [1.9]

- Expandable directory tree with lazy loading, folder icons and keyboard
  expand / collapse.

## [1.8]

- Resizable multi-terminal layout: panes side by side (`Ctrl+T`) or stacked
  (`Ctrl+Shift+T`).

## [1.7]

- Rendered HTML preview with an `m` toggle to the raw markup.

## [1.6]

- Modular code: the program split into modules in `src/`.

## [1.5]

- Rendered Markdown preview with an `m` toggle to the raw source.

## [1.4]

- File-type icons, embedded terminal (`Ctrl+T`), fullscreen start (`-f`),
  panel focus cycling (`Alt+Left` / `Alt+Right`), viewer with Vim keys, search
  and optional syntax highlighting.

## [1.3]

- Hidden files toggle, sort cycling, directory listing in the preview, large
  file preview, dark theme.

## [1.2]

- Vim keys (`j` / `k`, `gg`, `G`, `/`, `n` / `N`) and tree navigation
  (`Enter` / `Backspace`).

## [1.1]

- Listing of the current directory, resizable panel, preview linked to the
  selection, `Ctrl+Q`.

## [1.0]

- First version with two panels.
