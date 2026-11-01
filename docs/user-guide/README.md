# LookinFiles User Guide

LookinFiles ("look in your files") is a keyboard-driven file explorer: a
file tree on the left, a live preview of the selected file on the right, a
tabbed editor and an embedded terminal, all in one window.

This guide explains how to use it, one topic per page. Inside the
application, `F1` (or a click on the **Help** button of the top bar) shows
it in a **Help** tab; `?` (or a right click on that button) shows the keys
of the current mode. The complete list of keys is in the
[keyboard reference](../keyboard.md).

## Contents

**Start here**

- [Getting started](getting-started.md): launching, the window, the global
  mode, the first steps.

**Browsing**

- [The explorer](explorer.md): moving around, opening, creating, renaming
  and deleting files, sorting, hidden files.
- [The preview panel](preview-panel.md): what each kind of file looks like,
  rendered and raw views, zoom, media.
- [Finding files and text](search.md): go to a file (`Ctrl+P`), recent
  places (`Alt+R`), search in files (`Ctrl+Shift+F`).
- [Bookmarks](bookmarks.md): named links to folders, files and web pages.
- [Notes](notes.md): notebooks of Markdown, Org-mode, plain text and Canvas
  notes.
- [Remote machines](remote.md): browse SSH machines as local folders.

**Editing**

- [The editor](editor.md): tabs, splits, the dedicated editor panel,
  Notepad and Emacs modes.
- [Vim mode](vim.md): the Vim engine, Ex commands and options.
- [Org-mode](org-mode.md): outlines, TODO items, dates, clocks and tables.
- [Code navigation](code-navigation.md): go to definition, references,
  help cards, language servers.

**Working**

- [The terminal](terminal.md): terminal panes beside or below the viewer.
- [Git](git.md): status badges, change bars, the commit history.
- [Workspaces and layouts](workspaces.md): eight workspaces, saved panel
  layouts, the session restored at launch.
- [Themes and fonts](themes.md): built-in themes, your own themes, font
  sizes.

**Help**

- [Troubleshooting](troubleshooting.md): missing features, common problems.

## Reading this guide in LookinFiles

Press `F1`, or click the **Help** button of the top bar, to open this guide
in a **Help** tab of the preview panel; click the button again to close it.

| Key | Action |
|---|---|
| Arrows, `Page Down` / `Page Up` (and the keys of your mode) | Scroll |
| `Tab` / `Shift+Tab` | Select the next / previous link |
| `Enter`, or a click | Follow the link |
| `Shift+H` / `Shift+L`, mouse back / forward buttons | Back / forward |
| `Ctrl+W` (Vim: `Ctrl+W c`), middle click on the tab | Close the tab |

The buttons above the page go back, forward and to these contents, show
the keyboard cheat sheet and close the tab. On a long page, the minimap on
the right shows the whole page in small: click or drag it to move.

## Reference pages

- [Keyboard reference](../keyboard.md): every shortcut, by panel and mode.
- [Supported previews](../previews.md): every previewed format and its
  limits.
- [Configuration](../configuration.md): the files in
  `~/.config/lookinfiles/`, language servers, the theme format.
