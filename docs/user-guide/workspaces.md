# Workspaces and layouts

## Panels

The window has four panels: the explorer, the preview, the dedicated
editor panel and the terminal. The buttons of the top bar show or hide
them:

- **Explorer** and **Preview** show or hide those panels (at least one
  panel always stays visible);
- **Terminal** shows or hides the terminal (see [the terminal](terminal.md));
- the button at the start of the tab bar moves editor tabs to the
  dedicated editor panel and back (see [the editor](editor.md)).

Drag the border between two panels to resize them; a small label shows
the size while you drag. `f` toggles fullscreen.

## Workspaces

A workspace is a complete set of panels: its own folder, expanded folders,
selection, sort, hidden files, panel sizes, editor tabs and terminals.
There can be up to eight, like virtual desktops.

| Key | Action |
|---|---|
| `Alt+1` … `Alt+8` | Switch to workspace 1 … 8 |
| `Alt++` | Add a workspace, with the default layout |
| `Alt+-` | Close the current workspace |

The workspace map in the top bar shows one small box per workspace, drawn
like its panels. Click a box to switch, click **+** to add one, right-click
or middle-click a box to close it.

## Saved layouts: `Alt+L`

A layout is an arrangement of panels: which ones are visible, their sizes,
whether the terminal is beside or under the viewer, and the terminal panes.
`Alt+L` (or the grid button of the top bar) opens the layouts dialog:

- **apply** a saved layout to the current workspace;
- **save** the current panels under a name;
- save it as **Default** to use it for new workspaces;
- **delete** a layout, or reset the default.

Layouts are saved in `~/.config/lookinfiles/layouts.ini`.

## The session

When you quit (`Ctrl+Q` or closing the window), LookinFiles saves every
workspace: its folder, expanded folders, selection, hidden files, sort and
open files. Launching it without an argument restores them. Folders and
files that no longer exist are skipped.

Launching it with a folder or a file starts with that instead
(`lookinfiles .` always shows the current folder).

The session is saved in `~/.config/lookinfiles/session.ini`.
