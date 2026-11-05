# The terminal

LookinFiles has an embedded terminal, so you can run commands without
leaving the window. On Linux it needs the VTE library (see
[Getting started](getting-started.md#installing)); without it, the
terminal panel says "Terminal panel disabled" and how to enable it. On
Windows (10 version 1809 or later) the terminal is built in: the shell is
PowerShell 7 when installed, else Windows PowerShell, else `cmd.exe`.

## Opening and closing

| Key | Action |
|---|---|
| `Ctrl+T` | Open a terminal pane beside the running ones |
| `Ctrl+Shift+T` | Open a terminal pane below the running ones |
| `Ctrl+Shift+L` | Put the terminal under the viewer, or back beside it |
| **Terminal** button (top bar) | Show or hide the terminal panel |

A new shell starts in the explorer's current folder. The right-click menu
of the explorer can also open a terminal in a given folder.

Select text with the mouse, then `Ctrl+Shift+C` copies it; `Ctrl+Shift+V`
pastes the clipboard into the shell.

To close a pane, exit its shell (`exit` or `Ctrl+D`). Hiding the panel with
the **Terminal** button keeps the shells running.

## Moving the focus

`Alt+Left` / `Alt+Right` move the focus between the terminal and the other
panels, in every mode. Inside a terminal, the other keys go to the shell,
so the Vim, Emacs and Notepad panel keys are not used there.

The borders between panes can be dragged like every other border.

## Remote machines

While you browse a connected [remote machine](remote.md), the **Terminal**
button shows the machine's name and opens an SSH terminal to it. In the
remote view, `t` opens an SSH terminal to the selected machine.

## Workspaces

Each [workspace](workspaces.md) has its own terminal panes. A saved layout
remembers how many panes there are and how they are arranged, and the
terminal tabs of the editor panel; applying it starts the missing shells.

The terminal follows the theme's colours and the code font (see
[themes](themes.md)).
