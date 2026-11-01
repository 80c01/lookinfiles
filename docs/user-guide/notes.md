# Notes

The notes view is a notebook manager inside LookinFiles: notes sorted into
notebooks, with a trash. Open it with `Ctrl+U` (again to go back to the
files) or the notes button at the top of the explorer.

A first notebook, **General**, holds a welcome note.

## Note formats

| Format | File | Edited in |
|---|---|---|
| Markdown | `.md` | An editor tab; the preview shows it rendered |
| Org-mode | `.org` | An Org-mode editor tab (see [Org-mode](org-mode.md)) |
| Plain Text | `.txt` | An editor tab |
| Canvas | a `.canvas` folder | The **Canvas** tab (see below) |

The right-click menu converts a note to another text format, and exports a
Canvas note to a Markdown note.

## Working with notes

| Key | Action |
|---|---|
| `a` | New note in the selected notebook (asks for a title and a format) |
| `Shift+A` | New notebook |
| `Enter` | Edit the note |
| `F2`, `r` or `e` | Edit the note's title, format and notebook, or the notebook's name and icon |
| `i` | Change the notebook's icon |
| `m` | Move the note to another notebook |
| `d` or `Delete` | Move the note to the trash, or delete the notebook |
| `Shift+R` | Restore the selected note from the trash to its notebook |
| `Shift+K` / `Shift+J` | Move the note or notebook up / down |
| `o` | Sort: manual order, A–Z, Z–A |

Selecting a note shows it in the preview; selecting a notebook shows a
summary. The **Trash** at the bottom of the tree keeps deleted notes until
you empty it (right-click the Trash, **Empty Trash…**).

## Opening a note from the command line

```bash
lookinfiles -n Ideas            # the note "Ideas", in any notebook
lookinfiles -n Work/Meeting.org # "Meeting" in the notebook "Work", Org-mode
```

A note that does not exist is created, with its notebook. The extension
(`.md`, `.org`, `.txt`) sets the format of a new note.

## Canvas notes

A Canvas note is a free page of text boxes and pictures, placed where you
want them. It opens in the **Canvas** tab of the preview panel (one canvas
at a time) and is saved automatically.

| Action | How |
|---|---|
| New text box | Double click on the page, or `Ctrl+Enter` |
| New picture | `Ctrl+V` with a picture copied, or drop image files on the page |
| Move a block | Drag the bar above it (a picture: drag it anywhere) |
| Resize a block | Drag its corner (pictures keep their proportions) |
| Bold, italic, underline, strikethrough, code, heading | `Ctrl+B`, `Ctrl+I`, `Ctrl+U`, `Ctrl+Shift+S`, `Ctrl+E`, `Ctrl+Alt+1` |
| Select the block you are typing in | `Esc` |
| Next / previous block | `Tab` / `Shift+Tab` |
| Move the selected block | Arrows (`Shift`: further) |
| Edit the selected text box | `Enter` |
| Delete the selected block | `Delete` or `Backspace` |
| Close the Canvas tab | `Ctrl+W`, or middle click on the tab |

## Where notes are kept

Each notebook is a folder in `~/.config/lookinfiles/notes/`, and each note
a normal file in it, so you can back them up, sync them or open them with
other programs.
