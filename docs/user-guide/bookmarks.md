# Bookmarks

Bookmarks are named links to folders, files or web pages, sorted into
folders of your own. Open the bookmarks view with `Ctrl+A` (again to go
back to the files) or the bookmark button at the top of the explorer.

## Adding a bookmark

- From any view, `Ctrl+D` bookmarks the selected entry, or the current
  folder.
- In the bookmarks view, `a` adds a bookmark inside the selected folder.
  The dialog asks for a **Name**, a **Location** (a path or a URL; the
  **Current Directory** button fills in the explorer's folder) and a
  **Folder**.
- `Shift+A` creates a bookmark folder, with a **Name**, an **Icon** and a
  **Parent folder**.

## Saving a workspace

Right-click in the bookmarks view and choose **Save Workspace as
Bookmark…** to keep the current workspace under a name: its folder,
expanded folders, selection, hidden files, sort, open editor tabs and where
they were, the panels, their sizes and the terminal panes. The bookmark goes
in the selected folder and shows a workspace icon.

Opening it (`Enter`) restores that workspace in a **new** workspace; the
current one is left alone, and all eight workspaces must not be in use.
Terminals start new shells. A missing folder stops it with a message; files
that no longer exist are skipped. Rename or move it with `F2`; its location
cannot be edited. Deleting the bookmark deletes the saved workspace.

## Using bookmarks

| Key | Action |
|---|---|
| `Enter` | Open: a folder becomes the explorer's folder, a file opens, a URL opens in your web browser, a saved workspace opens in a new workspace |
| `Right` / `Left` | Open / close a bookmark folder |
| `F2`, `r` or `e` | Edit the name, location and folder (a folder: its name, icon and parent folder) |
| `d` or `Delete` | Delete the bookmark or folder |
| `Shift+K` / `Shift+J` | Move the item up / down |
| `o` | Sort: A–Z, Z–A |

Selecting a bookmark folder shows a summary of its content in the preview.

When a bookmark's folder or file no longer exists, opening it offers to
remove it.

Bookmarks are saved in `~/.config/lookinfiles/bookmarks.xml`; the saved
workspaces are in `~/.config/lookinfiles/sessions/`.
