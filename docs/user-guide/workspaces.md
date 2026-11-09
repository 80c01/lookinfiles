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
selection, sort, hidden files, panel sizes, editor tabs, terminals,
Search tab and Help tab.
There can be up to eight, like virtual desktops.

| Key | Action |
|---|---|
| `Alt+1` … `Alt+8` | Switch to workspace 1 … 8 |
| `Alt++` | Add a workspace, with the default layout |
| `Alt+-` | Close the current workspace |

The workspace map in the top bar shows one small box per workspace, drawn
like its panels. Click a box to switch, click **+** to add one, right-click
a box for its menu (**Save as Bookmark…**, **Close Workspace**) or
middle-click it to close it.

## Saved layouts: `Alt+L`

A layout is an arrangement of panels: which ones are visible, their sizes,
whether the terminal is beside or under the viewer, the terminal panes, the
splits of the editor panel with their terminal tabs, and the tagbars (shown
in the editor tabs, in the preview, and their width; layouts saved before
this keep the current tagbars). It keeps no files:
applying it moves the open files into the editor panes that held files (an
empty one takes a file from the fullest), reuses the running terminals and
starts the missing shells.
`Alt+L` (or the grid button of the top bar) opens the layout list under
the button: the current arrangement, the default layout, the saved ones,
then the built-in layouts. Moving through the list (hovering with the mouse, arrows, or `Alt+L`
again for the next one) shows each layout exactly as it would be applied:
panels, editor splits and tabs, terminal panes. Nothing closes meanwhile: terminals a layout has
no room for are only set aside, and the shells started for a preview end
with it. `Enter` or a click applies the layout (asking first when it
closes running terminals); `Escape` puts everything back as it was, the
same tabs in the same places and the same terminals.

**Edit Layouts…** at the bottom of the list opens the layouts dialog:

- **apply** a saved layout to the current workspace;
- **save** the current panels under a name;
- save it as **Default** to use it for new workspaces;
- **delete** a layout, or reset the default.

Three layouts come built in, below a line at the end of the list:

- **Code**: explorer, preview and editor panel, a terminal below;
- **Terminal**: explorer and preview, two terminals beside them;
- **Focus**: the preview alone.

They cannot be deleted. Saving a layout under the name of a built-in one
replaces it; deleting yours brings the built-in one back.

With files open, **Auto** comes first among them: the panels stay as they
are (except the preview, which hides with two files or more), and the open
files go to the editor panel in a grid made for their number and for the
room the panel gets in the window:

- one file: one pane;
- two: side by side when the panel is wider than tall and has room for two
  editors of about 90 columns, one above the other otherwise;
- three: one on the left and two stacked on the right (wide panel), or one
  on top and two side by side below (tall panel);
- four or more: two by two.

No pane gets smaller than about 90 columns by 12 lines: without room for
two by two, the panes make one row (wide panel) or one column (tall one),
and with room for fewer panes than files, the files share the panes as
tabs. The current file stays in the first pane. When Preferences show the
preview beside two files or more, it narrows (down to a quarter of the room
it shares with the editor panel) when that leaves room for two editors side
by side.

Preferences (`Ctrl+,`) → **Layouts** sets what Auto does with the terminal
and the preview: leave them as they are (the default; the preview still
hides with two files or more), show them, or hide them; and where it puts the terminal: where it is (the default), below, or
on the right (as `Ctrl+Shift+L` does). The grid is made for the room left
once they are placed.

Layouts are saved in `~/.config/lookinfiles/layouts.ini`. Applying a
layout keeps its tagbars on or off afterwards, as their toggle keys do; a
new workspace keeps the current tagbars, since they are shared by all
workspaces.

## The session

When you quit (`Ctrl+Q` or closing the window), LookinFiles saves every
workspace: its folder, expanded folders, selection, hidden files, sort,
open files and terminal tabs and where they were (preview or editor
panel, split views, current tab of each group), the panels shown, their sizes and the terminal
panes. Launching it without an argument restores them; the terminals start
new shells. Folders and files that no longer exist are skipped.

Launching it with a folder or a file starts with that instead
(`lookinfiles .` always shows the current folder).

The session is saved in `~/.config/lookinfiles/session.ini`.
