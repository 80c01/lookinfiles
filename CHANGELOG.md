# Changelog

All notable changes to this project are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Changed

- The application is renamed **Lookin** ("look in your files"): the binary
  is `lookin`, settings live in `~/.config/lookin/` (an existing
  `~/.config/explorateur_fichiers/` is moved there on first start), remote
  mounts in `~/.cache/lookin/remotes/`, and the icon diagnostic variable is
  `LOOKIN_DEBUG_ICONS`.

### Added

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
